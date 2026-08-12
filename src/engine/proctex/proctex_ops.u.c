/* proctex_ops.u.c — included by proctex_generator.c
 *
 * Mechanical ports of rs-map-viewer TextureOperation subclasses. Fixed-point
 * arithmetic is kept verbatim; Math.fround becomes proctex_fround.
 */

/* --- aux helpers ---------------------------------------------------------------- */

void
proctex_mark_all_done(struct proctex_cache* cache, int height)
{
    memset(cache->done, 1, (size_t)height);
}

/* Permutation tables keyed by seed (0..255). Cached for the process lifetime. */
static int8_t* g_perm_cache[256];
static int8_t g_perm_ready[256];

const int8_t*
proctex_permutations(uint8_t seed)
{
    struct proctex_jrand rnd;
    int8_t* p;
    int i;

    if( g_perm_ready[seed] )
        return g_perm_cache[seed];

    /* calloc, not malloc: the writes below cover 0..255 and 257..511, so p[256]
     * is never assigned. The source is a Java `byte[512]`, which the JVM
     * zero-fills, so p[256] is a defined 0 there and every lookup that lands on
     * it is deterministic. Under malloc it was whatever the heap held, which is
     * what made the bake nondeterministic: 246 of 512 material frames differed
     * between two runs on identical arguments, and 392 of 660 models with them. */
    p = calloc(512, 1);
    assert(p);
    proctex_jrand_seed(&rnd, seed);
    for( i = 0; i < 255; i++ )
        p[i] = (int8_t)i;
    for( i = 0; i < 255; i++ )
    {
        int index0 = 255 - i;
        int index1 = proctex_next_int_jagex(&rnd, index0);
        int8_t perm1 = p[index1];
        p[index1] = p[index0];
        p[index0] = p[511 - i] = perm1;
    }
    g_perm_cache[seed] = p;
    g_perm_ready[seed] = 1;
    return p;
}

static int32_t g_perlin_noise[4096];
static bool g_perlin_noise_ready = false;

static void
proctex_init_perlin_noise(void)
{
    if( g_perlin_noise_ready )
        return;
    for( int i = 0; i < 4096; i++ )
    {
        int i9 = (((i * i) >> 12) * i) >> 12;
        int i10 = i * 6 - 61440;
        int i11 = 40960 + ((i * i10) >> 12);
        g_perlin_noise[i] = (i9 * i11) >> 12;
    }
    g_perlin_noise_ready = true;
}

/* --- mixer ---------------------------------------------------------------------- */

bool
proctex_op_mixer(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    int width = gen->width;
    size_t offset = (size_t)line * (size_t)width;
    int32_t* out0 = cache->plane[0] + offset;
    int x;

    if( op->is_monochrome )
    {
        const int32_t* a = proctex_input_mono(gen, op, 0, line);
        const int32_t* b = proctex_input_mono(gen, op, 1, line);
        const int32_t* c = proctex_input_mono(gen, op, 2, line);
        if( !a || !b || !c )
            return false;
        for( x = 0; x < width; x++ )
        {
            int w = c[x];
            if( w == PROCTEX_ONE )
                out0[x] = a[x];
            else if( w == 0 )
                out0[x] = b[x];
            else
                out0[x] = (((PROCTEX_ONE - w) * b[x]) + w * a[x]) >> 12;
        }
    }
    else
    {
        const int32_t* a[3];
        const int32_t* b[3];
        const int32_t* c;
        int32_t* out1 = cache->plane[1] + offset;
        int32_t* out2 = cache->plane[2] + offset;
        c = proctex_input_mono(gen, op, 2, line);
        if( !c || !proctex_input_colour(gen, op, 0, line, a) ||
            !proctex_input_colour(gen, op, 1, line, b) )
            return false;
        for( x = 0; x < width; x++ )
        {
            int w = c[x];
            if( w == PROCTEX_ONE )
            {
                out0[x] = a[0][x];
                out1[x] = a[1][x];
                out2[x] = a[2][x];
            }
            else if( w == 0 )
            {
                out0[x] = b[0][x];
                out1[x] = b[1][x];
                out2[x] = b[2][x];
            }
            else
            {
                int bw = PROCTEX_ONE - w;
                out0[x] = (w * a[0][x] + bw * b[0][x]) >> 12;
                out1[x] = (w * a[1][x] + bw * b[1][x]) >> 12;
                out2[x] = (w * a[2][x] + bw * b[2][x]) >> 12;
            }
        }
    }
    return true;
}

/* --- blur ----------------------------------------------------------------------- */

bool
proctex_op_blur(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    int width = gen->width;
    int he = op->u.blur.h_extent;
    int ve = op->u.blur.v_extent;
    int n_passes = 1 + ve + ve;
    int inv_passes = 65536 / n_passes;
    int n_pixels = 1 + he + he;
    int inv_pixels = 65536 / n_pixels;
    size_t offset = (size_t)line * (size_t)width;
    int pass;
    int x;

    if( op->is_monochrome )
    {
        int32_t* out0 = cache->plane[0] + offset;
        int32_t** passes = calloc((size_t)n_passes, sizeof(*passes));
        if( !passes )
            return false;
        for( pass = -ve + line; pass <= line + ve; pass++ )
        {
            const int32_t* input =
                proctex_input_mono(gen, op, 0, pass & gen->height_mask);
            int32_t* pass_out;
            int sum = 0;
            int ptr;
            int idx;
            if( !input )
            {
                for( idx = 0; idx < n_passes; idx++ )
                    free(passes[idx]);
                free(passes);
                return false;
            }
            pass_out = malloc((size_t)width * sizeof(int32_t));
            if( !pass_out )
            {
                for( idx = 0; idx < n_passes; idx++ )
                    free(passes[idx]);
                free(passes);
                return false;
            }
            for( x = -he; x <= he; x++ )
                sum += input[x & gen->width_mask];
            ptr = 0;
            while( ptr < width )
            {
                pass_out[ptr] = (sum * inv_pixels) / 65536;
                sum -= input[(ptr - he) & gen->width_mask];
                ptr++;
                sum += input[(ptr + he) & gen->width_mask];
            }
            passes[ve - line + pass] = pass_out;
        }
        for( x = 0; x < width; x++ )
        {
            int sum = 0;
            for( pass = 0; pass < n_passes; pass++ )
                sum += passes[pass][x];
            out0[x] = (sum * inv_passes) / 65536;
        }
        for( pass = 0; pass < n_passes; pass++ )
            free(passes[pass]);
        free(passes);
    }
    else
    {
        int32_t* out0 = cache->plane[0] + offset;
        int32_t* out1 = cache->plane[1] + offset;
        int32_t* out2 = cache->plane[2] + offset;
        int32_t*** passes = calloc((size_t)n_passes, sizeof(*passes));
        if( !passes )
            return false;
        for( pass = -ve + line; pass <= line + ve; pass++ )
        {
            const int32_t* in[3];
            int32_t** pass_out;
            int sum_r = 0, sum_g = 0, sum_b = 0;
            int ptr;
            int idx;
            int p;
            if( !proctex_input_colour(gen, op, 0, pass & gen->height_mask, in) )
            {
                for( idx = 0; idx < n_passes; idx++ )
                {
                    if( passes[idx] )
                    {
                        for( p = 0; p < 3; p++ )
                            free(passes[idx][p]);
                        free(passes[idx]);
                    }
                }
                free(passes);
                return false;
            }
            pass_out = calloc(3, sizeof(*pass_out));
            if( !pass_out )
            {
                free(passes);
                return false;
            }
            for( p = 0; p < 3; p++ )
            {
                pass_out[p] = malloc((size_t)width * sizeof(int32_t));
                if( !pass_out[p] )
                {
                    free(pass_out);
                    free(passes);
                    return false;
                }
            }
            for( x = -he; x <= he; x++ )
            {
                int m = x & gen->width_mask;
                sum_r += in[0][m];
                sum_g += in[1][m];
                sum_b += in[2][m];
            }
            ptr = 0;
            while( ptr < width )
            {
                pass_out[0][ptr] = (sum_r * inv_pixels) / 65536;
                pass_out[1][ptr] = (sum_g * inv_pixels) / 65536;
                pass_out[2][ptr] = (sum_b * inv_pixels) / 65536;
                sum_r -= in[0][(ptr - he) & gen->width_mask];
                sum_g -= in[1][(ptr - he) & gen->width_mask];
                sum_b -= in[2][(ptr - he) & gen->width_mask];
                ptr++;
                sum_r += in[0][(ptr + he) & gen->width_mask];
                sum_g += in[1][(ptr + he) & gen->width_mask];
                sum_b += in[2][(ptr + he) & gen->width_mask];
            }
            passes[ve + pass - line] = pass_out;
        }
        for( x = 0; x < width; x++ )
        {
            int sum_r = 0, sum_g = 0, sum_b = 0;
            for( pass = 0; pass < n_passes; pass++ )
            {
                sum_r += passes[pass][0][x];
                sum_g += passes[pass][1][x];
                sum_b += passes[pass][2][x];
            }
            out0[x] = (sum_r * inv_passes) / 65536;
            out1[x] = (sum_g * inv_passes) / 65536;
            out2[x] = (sum_b * inv_passes) / 65536;
        }
        for( pass = 0; pass < n_passes; pass++ )
        {
            int p;
            for( p = 0; p < 3; p++ )
                free(passes[pass][p]);
            free(passes[pass]);
        }
        free(passes);
    }
    return true;
}


/* --- emboss --------------------------------------------------------------------- */

struct proctex_emboss_aux
{
    int32_t table[3];
    bool ready;
};

bool
proctex_op_emboss(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    struct proctex_emboss_aux* aux = gen->aux[op_index];
    int width = gen->width;
    size_t offset = (size_t)line * (size_t)width;
    int32_t* out0 = cache->plane[0] + offset;
    const int32_t* prev;
    const int32_t* curr;
    const int32_t* next;
    int width_mult;
    int x;

    if( !aux )
    {
        aux = calloc(1, sizeof(*aux));
        if( !aux )
            return false;
        gen->aux[op_index] = aux;
    }
    if( !aux->ready )
    {
        float d = cosf(proctex_fround((double)op->u.emboss.field2 / 4096.0));
        float s1 = sinf(proctex_fround((double)op->u.emboss.field1 / 4096.0));
        float c1 = cosf(proctex_fround((double)op->u.emboss.field1 / 4096.0));
        float s2 = sinf(proctex_fround((double)op->u.emboss.field2 / 4096.0));
        int t0, t1, t2, scale;
        aux->table[0] = (int32_t)(4096.0 * (d * s1));
        aux->table[1] = (int32_t)(4096.0 * (d * c1));
        aux->table[2] = (int32_t)(4096.0 * s2);
        t0 = (aux->table[0] * aux->table[0]) >> 12;
        t1 = (aux->table[1] * aux->table[1]) >> 12;
        t2 = (aux->table[2] * aux->table[2]) >> 12;
        scale = (int)(sqrt((double)((t0 + t1 + t2) >> 12)) * 4096.0);
        if( scale != 0 )
        {
            aux->table[0] = (aux->table[0] << 12) / scale;
            aux->table[1] = (aux->table[1] << 12) / scale;
            aux->table[2] = (aux->table[2] << 12) / scale;
        }
        aux->ready = true;
    }

    width_mult = (op->u.emboss.field0 * gen->width_times_32) >> 12;
    prev = proctex_input_mono(gen, op, 0, (line - 1) & gen->height_mask);
    curr = proctex_input_mono(gen, op, 0, line);
    next = proctex_input_mono(gen, op, 0, (line + 1) & gen->height_mask);
    if( !prev || !curr || !next )
        return false;

    for( x = 0; x < width; x++ )
    {
        int prev_px = curr[(x - 1) & gen->width_mask];
        int next_px = curr[(x + 1) & gen->width_mask];
        int i10 = (width_mult * (next[x] - prev[x])) >> 12;
        int i11 = (width_mult * (prev_px - next_px)) >> 12;
        int i12 = i11 >> 4;
        int i13 = i10 >> 4;
        int i14;
        int v0, v1, v2;
        if( i12 < 0 )
            i12 = -i12;
        if( i12 > 255 )
            i12 = 255;
        if( i13 < 0 )
            i13 = -i13;
        if( i13 > 255 )
            i13 = 255;
        i14 = PROCTEX_INV_SQRT[i12 + (((i13 + 1) * i13) >> 1)] & 0xff;
        v0 = (i14 * i11) >> 8;
        v1 = (i14 * i10) >> 8;
        v2 = (i14 * 4096) >> 8;
        v0 = (aux->table[0] * v0) >> 12;
        v1 = (aux->table[1] * v1) >> 12;
        v2 = (aux->table[2] * v2) >> 12;
        out0[x] = v0 + v1 + v2;
    }
    return true;
}

/* --- perlin --------------------------------------------------------------------- */

struct proctex_perlin_aux
{
    bool ready;
    int octaves;
    bool field0;
    uint8_t scale_x;
    uint8_t scale_y;
    int16_t* nin0;
    int16_t* nin1;
    const int8_t* perm;
};

static int
proctex_perlin_noise1(
    const int8_t* perm,
    int h_grad,
    int v_grad,
    int perm0,
    int perm1,
    int noise_index,
    int noise)
{
    int i8 = h_grad >> 12;
    int i9 = i8 + 1;
    int i10, i11, i12, i13, i14, i15, i16, i17;
    h_grad &= 0xfff;
    if( i9 >= v_grad )
        i9 = 0;
    i8 &= 0xff;
    i10 = noise_index - 4096;
    i11 = h_grad - 4096;
    i9 &= 0xff;
    i12 = perm[perm0 + i8] & 0x3;
    i13 = g_perlin_noise[h_grad];
    if( i12 > 1 )
        i14 = i12 == 2 ? -noise_index + h_grad : -noise_index + -h_grad;
    else
        i14 = i12 == 0 ? noise_index + h_grad : -h_grad + noise_index;
    i12 = perm[perm0 + i9] & 0x3;
    if( i12 <= 1 )
        i15 = i12 == 0 ? noise_index + i11 : noise_index - i11;
    else
        i15 = i12 == 2 ? i11 - noise_index : -i11 + -noise_index;
    i12 = perm[perm1 + i8] & 0x3;
    i16 = ((i13 * (i15 - i14)) >> 12) + i14;
    if( i12 <= 1 )
        i14 = i12 != 0 ? i10 - h_grad : h_grad + i10;
    else
        i14 = i12 != 2 ? -i10 + -h_grad : h_grad - i10;
    i12 = perm[i9 + perm1] & 0x3;
    if( i12 <= 1 )
        i15 = i12 == 0 ? i11 + i10 : i10 - i11;
    else
        i15 = i12 == 2 ? -i10 + i11 : -i10 + -i11;
    i17 = i14 + ((i13 * (i15 - i14)) >> 12);
    return i16 + ((noise * (i17 - i16)) >> 12);
}

bool
proctex_op_perlin(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    struct proctex_perlin_aux* aux = gen->aux[op_index];
    int width = gen->width;
    size_t offset = (size_t)line * (size_t)width;
    int32_t* out0 = cache->plane[0] + offset;
    int v_grad;
    int pixel;

    proctex_init_perlin_noise();

    if( !aux )
    {
        aux = calloc(1, sizeof(*aux));
        if( !aux )
            return false;
        gen->aux[op_index] = aux;
    }
    if( !aux->ready )
    {
        int oct = op->u.perlin.octaves;
        int i;
        aux->field0 = op->u.perlin.field0;
        aux->scale_x = op->u.perlin.scale_x;
        aux->scale_y = op->u.perlin.scale_y;
        aux->perm = proctex_permutations(op->u.perlin.seed);
        aux->nin0 = calloc((size_t)oct, sizeof(int16_t));
        aux->nin1 = calloc((size_t)oct, sizeof(int16_t));
        if( !aux->nin0 || !aux->nin1 )
            return false;
        if( op->u.perlin.field2 <= 0 )
        {
            int n = op->u.perlin.amplitude_count;
            if( n > oct )
                n = oct;
            for( i = 0; i < n; i++ )
                aux->nin0[i] = op->u.perlin.amplitudes[i];
            for( i = 0; i < oct; i++ )
                aux->nin1[i] = (int16_t)(1 << i); /* Math.pow(2,i) */
        }
        else
        {
            float base = proctex_fround((double)op->u.perlin.field2 / 4096.0);
            for( i = 0; i < oct; i++ )
            {
                aux->nin0[i] = (int16_t)(powf(base, (float)i) * 4096.0f);
                aux->nin1[i] = (int16_t)(1 << i);
            }
        }
        /* Trim trailing tiny amplitudes like the reference init(). */
        aux->octaves = oct;
        for( i = oct - 1; i >= 1; i-- )
        {
            int v = aux->nin0[i];
            if( v > 8 || v < -8 )
                break;
            aux->octaves--;
        }
        aux->ready = true;
    }

    v_grad = aux->scale_y * gen->v_gradient[line];
    if( aux->octaves == 1 )
    {
        int nin0 = aux->nin0[0];
        int nin1 = aux->nin1[0] << 12;
        int n1f5 = (nin1 * aux->scale_x) >> 12;
        int n1f6 = (nin1 * aux->scale_y) >> 12;
        int noise_index = (nin1 * v_grad) >> 12;
        int perm_index0 = noise_index >> 12;
        int perm_index1 = perm_index0 + 1;
        int noise, perm0, perm1;
        if( n1f6 <= perm_index1 )
            perm_index1 = 0;
        noise_index &= 0xfff;
        noise = g_perlin_noise[noise_index];
        perm0 = aux->perm[perm_index0 & 0xff] & 0xff;
        perm1 = aux->perm[perm_index1 & 0xff] & 0xff;
        if( aux->field0 )
        {
            for( pixel = 0; pixel < width; pixel++ )
            {
                int h = aux->scale_x * gen->h_gradient[pixel];
                int v = proctex_perlin_noise1(
                    aux->perm, (nin1 * h) >> 12, n1f5, perm0, perm1, noise_index, noise);
                v = (nin0 * v) >> 12;
                out0[pixel] = (v >> 1) + 2048;
            }
        }
        else
        {
            for( pixel = 0; pixel < width; pixel++ )
            {
                int h = aux->scale_x * gen->h_gradient[pixel];
                int v = proctex_perlin_noise1(
                    aux->perm, (nin1 * h) >> 12, n1f5, perm0, perm1, noise_index, noise);
                out0[pixel] = (v * nin0) >> 12;
            }
        }
    }
    else
    {
        int i45 = aux->nin0[0];
        int i58;
        memset(out0, 0, (size_t)width * sizeof(int32_t));
        if( i45 > 8 || i45 < -8 )
        {
            int i46 = aux->nin1[0] << 12;
            int i47 = (i46 * v_grad) >> 12;
            int i48 = (i46 * aux->scale_x) >> 12;
            int i49 = (i46 * aux->scale_y) >> 12;
            int i50 = i47 >> 12;
            int i51 = i50 + 1;
            int i52, i53, i54;
            i47 &= 0xfff;
            if( i49 <= i51 )
                i51 = 0;
            i52 = aux->perm[i50 & 0xff] & 0xff;
            i53 = g_perlin_noise[i47];
            i54 = aux->perm[i51 & 0xff] & 0xff;
            for( pixel = 0; pixel < width; pixel++ )
            {
                int i56 = aux->scale_x * gen->h_gradient[pixel];
                int i57 = proctex_perlin_noise1(
                    aux->perm, (i56 * i46) >> 12, i48, i52, i54, i47, i53);
                out0[pixel] = (i45 * i57) >> 12;
            }
        }
        for( i58 = 1; i58 < aux->octaves; i58++ )
        {
            i45 = aux->nin0[i58];
            if( i45 > 8 || i45 < -8 )
            {
                int i59 = aux->nin1[i58] << 12;
                int i60 = (aux->scale_y * i59) >> 12;
                int i61 = (aux->scale_x * i59) >> 12;
                int i62 = (v_grad * i59) >> 12;
                int i63 = i62 >> 12;
                int i64 = i63 + 1;
                int i65, i66, i67;
                i62 &= 0xfff;
                if( i60 <= i64 )
                    i64 = 0;
                i65 = aux->perm[i64 & 0xff] & 0xff;
                i66 = aux->perm[i63 & 0xff] & 0xff;
                i67 = g_perlin_noise[i62];
                if( aux->field0 && aux->octaves - 1 == i58 )
                {
                    for( pixel = 0; pixel < width; pixel++ )
                    {
                        int i69 = gen->h_gradient[pixel] * aux->scale_x;
                        int i70 = proctex_perlin_noise1(
                            aux->perm, (i59 * i69) >> 12, i61, i66, i65, i62, i67);
                        i70 = out0[pixel] + ((i70 * i45) >> 12);
                        out0[pixel] = 2048 + (i70 >> 1);
                    }
                }
                else
                {
                    for( pixel = 0; pixel < width; pixel++ )
                    {
                        int i72 = gen->h_gradient[pixel] * aux->scale_x;
                        int i73 = proctex_perlin_noise1(
                            aux->perm, (i59 * i72) >> 12, i61, i66, i65, i62, i67);
                        out0[pixel] += (i45 * i73) >> 12;
                    }
                }
            }
        }
    }
    return true;
}

/* --- voronoi -------------------------------------------------------------------- */

struct proctex_voronoi_aux
{
    bool ready;
    const int8_t* perm;
    int16_t random_ns[512];
};

bool
proctex_op_voronoi(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    struct proctex_voronoi_aux* aux = gen->aux[op_index];
    int width = gen->width;
    size_t offset = (size_t)line * (size_t)width;
    int32_t* out0 = cache->plane[0] + offset;
    int i1, i2, i3, pixel;
    uint8_t sx = op->u.voronoi.scale_x;
    uint8_t sy = op->u.voronoi.scale_y;

    if( !aux )
    {
        aux = calloc(1, sizeof(*aux));
        if( !aux )
            return false;
        gen->aux[op_index] = aux;
    }
    if( !aux->ready )
    {
        struct proctex_jrand rnd;
        int i;
        aux->perm = proctex_permutations(op->u.voronoi.rng_seed);
        proctex_jrand_seed(&rnd, op->u.voronoi.rng_seed);
        if( op->u.voronoi.field2 > 0 )
        {
            for( i = 0; i < 512; i++ )
                aux->random_ns[i] = (int16_t)proctex_next_int_jagex(&rnd, op->u.voronoi.field2);
        }
        aux->ready = true;
    }

    i1 = 2048 + sy * gen->v_gradient[line];
    i2 = i1 >> 12;
    i3 = i2 + 1;
    for( pixel = 0; pixel < width; pixel++ )
    {
        int temp0 = 2147483647, temp1 = 2147483647, temp2 = 2147483647, temp3 = 2147483647;
        int i5 = sx * gen->h_gradient[pixel] + 2048;
        int i6 = i5 >> 12;
        int i7 = i6 + 1;
        int i8;
        for( i8 = i2 - 1; i8 <= i3; i8++ )
        {
            int i9 = aux->perm[(i8 >= sy ? i8 - sy : i8) & 0xff] & 0xff;
            int i10;
            for( i10 = i6 - 1; i10 <= i7; i10++ )
            {
                int i11 = (aux->perm[((i10 >= sx ? i10 - sx : i10) + i9) & 0xff] & 0xff) * 2;
                int i12 = i5 - (aux->random_ns[i11++] + (i10 << 12));
                int i13 = i1 - (aux->random_ns[i11] + (i8 << 12));
                int i14;
                switch( op->u.voronoi.field4 )
                {
                case 1:
                    i14 = (i12 * i12 + i13 * i13) >> 12;
                    break;
                case 2:
                    i14 = (i13 < 0 ? -i13 : i13) + (i12 < 0 ? -i12 : i12);
                    break;
                case 3:
                    i12 = i12 < 0 ? -i12 : i12;
                    i13 = i13 < 0 ? -i13 : i13;
                    i14 = i12 > i13 ? i12 : i13;
                    break;
                case 4:
                    i12 = (int)(sqrtf(proctex_fround((i12 < 0 ? -i12 : i12) / 4096.0)) * 4096.0f);
                    i13 = (int)(sqrtf(proctex_fround((i13 < 0 ? -i13 : i13) / 4096.0)) * 4096.0f);
                    i14 = i13 + i12;
                    i14 = (i14 * i14) >> 12;
                    break;
                case 5:
                    i12 *= i12;
                    i13 *= i13;
                    i14 = (int)(sqrtf(sqrtf(proctex_fround((i12 + i13) / 1.6777216e7f))) * 4096.0f);
                    break;
                default:
                    i14 = (int)(sqrtf(proctex_fround((i13 * i13 + i12 * i12) / 1.6777216e7f)) *
                                4096.0f);
                    break;
                }
                if( i14 < temp3 )
                {
                    temp0 = temp1;
                    temp1 = temp2;
                    temp2 = temp3;
                    temp3 = i14;
                }
                else if( i14 < temp2 )
                {
                    temp0 = temp1;
                    temp1 = temp2;
                    temp2 = i14;
                }
                else if( i14 < temp1 )
                {
                    temp0 = temp1;
                    temp1 = i14;
                }
                else if( i14 < temp0 )
                {
                    temp0 = i14;
                }
            }
        }
        switch( op->u.voronoi.field3 )
        {
        case 0:
            out0[pixel] = temp3;
            break;
        case 1:
            out0[pixel] = temp2;
            break;
        case 2:
            out0[pixel] = temp2 - temp3;
            break;
        case 3:
            out0[pixel] = temp1;
            break;
        case 4:
            out0[pixel] = temp0;
            break;
        }
    }
    return true;
}

/* --- trig_warp ------------------------------------------------------------------ */

bool
proctex_op_trig_warp(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    int width = gen->width;
    size_t offset = (size_t)line * (size_t)width;
    const int32_t* input_a;
    const int32_t* input_b;
    int x;

    input_a = proctex_input_mono(gen, op, 1, line);
    input_b = proctex_input_mono(gen, op, 2, line);
    if( !input_a || !input_b )
        return false;

    if( op->is_monochrome )
    {
        int32_t* out0 = cache->plane[0] + offset;
        for( x = 0; x < width; x++ )
        {
            int angle = (input_a[x] >> 4) & 0xff;
            int hyp = (input_b[x] * op->u.trig_warp.hypotenuse_multiplier) >> 12;
            int cosine = (PROCTEX_COSINE[angle] * hyp) >> 12;
            int sine = (PROCTEX_SINE[angle] * hyp) >> 12;
            int nx = (x + (cosine >> 12)) & gen->width_mask;
            int ny = (line + (sine >> 12)) & gen->height_mask;
            const int32_t* input = proctex_input_mono(gen, op, 0, ny);
            if( !input )
                return false;
            out0[x] = input[nx];
        }
    }
    else
    {
        int32_t* out0 = cache->plane[0] + offset;
        int32_t* out1 = cache->plane[1] + offset;
        int32_t* out2 = cache->plane[2] + offset;
        for( x = 0; x < width; x++ )
        {
            int angle = ((input_a[x] * 255) >> 12) & 0xff;
            int hyp = (input_b[x] * op->u.trig_warp.hypotenuse_multiplier) >> 12;
            int cosine = (PROCTEX_COSINE[angle] * hyp) >> 12;
            int sine = (PROCTEX_SINE[angle] * hyp) >> 12;
            int nx = (x + (cosine >> 12)) & gen->width_mask;
            int ny = (line + (sine >> 12)) & gen->height_mask;
            const int32_t* in[3];
            if( !proctex_input_colour(gen, op, 0, ny, in) )
                return false;
            out0[x] = in[0][nx];
            out1[x] = in[1][nx];
            out2[x] = in[2][nx];
        }
    }
    return true;
}

/* --- pseudo_random -------------------------------------------------------------- */

static int
proctex_pseudo_noise(int x, int y)
{
    int n = x + y * 57;
    n ^= n << 1;
    return 4096 - ((((1376312589 + (789221 + 15731 * (n * n)) * n) & 0x7fffffff) / 262144));
}

bool
proctex_op_pseudo_random(struct ProcTexGenerator* gen, int op_index, int line)
{
    struct proctex_cache* cache = &gen->caches[op_index];
    int width = gen->width;
    size_t offset = (size_t)line * (size_t)width;
    int32_t* out0 = cache->plane[0] + offset;
    int vert = gen->v_gradient[line];
    int x;
    (void)op_index;
    for( x = 0; x < width; x++ )
        out0[x] = proctex_pseudo_noise(gen->h_gradient[x], vert) % 4096;
    return true;
}

/* --- tiling --------------------------------------------------------------------- */

bool
proctex_op_tiling(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    int width = gen->width;
    size_t offset = (size_t)line * (size_t)width;
    int tile_w = width / op->u.tiling.tile_count_h;
    int tile_h = gen->height / op->u.tiling.tile_count_v;
    int x;

    if( op->is_monochrome )
    {
        const int32_t* input;
        int32_t* out0 = cache->plane[0] + offset;
        if( tile_h <= 0 )
            input = proctex_input_mono(gen, op, 0, 0);
        else
        {
            int ty = line % tile_h;
            input = proctex_input_mono(gen, op, 0, (gen->height * ty) / tile_h);
        }
        if( !input )
            return false;
        for( x = 0; x < width; x++ )
        {
            if( tile_w <= 0 )
                out0[x] = input[0];
            else
            {
                int tx = x % tile_w;
                out0[x] = input[(tx * width) / tile_w];
            }
        }
    }
    else
    {
        const int32_t* in[3];
        int32_t* out0 = cache->plane[0] + offset;
        int32_t* out1 = cache->plane[1] + offset;
        int32_t* out2 = cache->plane[2] + offset;
        if( tile_h <= 0 )
        {
            if( !proctex_input_colour(gen, op, 0, 0, in) )
                return false;
        }
        else
        {
            int ty = line % tile_h;
            if( !proctex_input_colour(gen, op, 0, (gen->height * ty) / tile_h, in) )
                return false;
        }
        for( x = 0; x < width; x++ )
        {
            int ix = 0;
            if( tile_w > 0 )
            {
                int tx = x % tile_w;
                ix = (tx * width) / tile_w;
            }
            out0[x] = in[0][ix];
            out1[x] = in[1][ix];
            out2[x] = in[2][ix];
        }
    }
    return true;
}

/* --- hsl ------------------------------------------------------------------------ */

static void
proctex_hsl_set(
    int r,
    int g,
    int b,
    int* hue,
    int* sat,
    int* light)
{
    int max_v = r > g ? (r > b ? r : b) : (g > b ? g : b);
    int min_v = r < g ? (r < b ? r : b) : (g < b ? g : b);
    int delta = max_v - min_v;
    *light = (max_v + min_v) / 2;
    if( delta > 0 )
    {
        int inv_r = ((max_v - r) << 12) / delta;
        int inv_g = ((max_v - g) << 12) / delta;
        int inv_b = ((max_v - b) << 12) / delta;
        if( r == max_v )
            *hue = g == min_v ? inv_b + 0x5000 : 4096 - inv_g;
        else if( g == max_v )
            *hue = b == min_v ? inv_r + 4096 : 0x3000 - inv_b;
        else
            *hue = min_v == r ? inv_g + 0x3000 : 0x5000 - inv_r;
        *hue = *hue / 6;
    }
    else
        *hue = 0;
    if( *light > 0 && *light < 4096 )
        *sat = (delta << 12) / (*light > 2048 ? 8192 - *light * 2 : *light * 2);
    else
        *sat = 0;
}

static void
proctex_hsl_to_rgb(int hue, int sat, int light, int* r, int* g, int* b)
{
    int i = light > 2048 ? sat + light - ((sat * light) >> 12)
                         : (light * (4096 + sat)) >> 12;
    if( i > 0 )
    {
        int j = light - i + light;
        int k = ((i - j) << 12) / i;
        int l, j1, i1, k1, l1;
        hue *= 6;
        l = hue >> 12;
        j1 = i;
        i1 = hue - (l << 12);
        j1 = (j1 * k) >> 12;
        j1 = (i1 * j1) >> 12;
        k1 = j + j1;
        l1 = i - j1;
        if( l == 0 )
        {
            *r = i;
            *g = k1;
            *b = j;
        }
        else if( l == 1 )
        {
            *r = l1;
            *g = i;
            *b = j;
        }
        else if( l == 2 )
        {
            *r = j;
            *g = i;
            *b = k1;
        }
        else if( l == 3 )
        {
            *r = j;
            *g = l1;
            *b = i;
        }
        else if( l == 4 )
        {
            *r = k1;
            *g = j;
            *b = i;
        }
        else if( l == 5 )
        {
            *r = i;
            *g = j;
            *b = l1;
        }
    }
    else
        *r = *g = *b = light;
}

bool
proctex_op_hsl(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    int width = gen->width;
    size_t offset = (size_t)line * (size_t)width;
    const int32_t* in[3];
    int32_t* out0 = cache->plane[0] + offset;
    int32_t* out1 = cache->plane[1] + offset;
    int32_t* out2 = cache->plane[2] + offset;
    int x;

    if( !proctex_input_colour(gen, op, 0, line, in) )
        return false;
    for( x = 0; x < width; x++ )
    {
        int hue, sat, light, r, g, b;
        proctex_hsl_set(in[0][x], in[1][x], in[2][x], &hue, &sat, &light);
        hue += op->u.hsl.delta_hue;
        sat += op->u.hsl.delta_saturation;
        light += op->u.hsl.delta_light;
        while( hue < 0 )
            hue += 4096;
        while( hue > 4096 )
            hue -= 4096;
        if( sat < 0 )
            sat = 0;
        if( sat > 4096 )
            sat = 4096;
        if( light < 0 )
            light = 0;
        if( light > 4096 )
            light = 4096;
        proctex_hsl_to_rgb(hue, sat, light, &r, &g, &b);
        out0[x] = r;
        out1[x] = g;
        out2[x] = b;
    }
    return true;
}

/* --- mirror --------------------------------------------------------------------- */

bool
proctex_op_mirror(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    int width = gen->width;
    size_t offset = (size_t)line * (size_t)width;
    int src_line = op->u.mirror.invert_vertical ? gen->height_mask - line : line;
    int x;

    if( op->is_monochrome )
    {
        const int32_t* input = proctex_input_mono(gen, op, 0, src_line);
        int32_t* out0 = cache->plane[0] + offset;
        if( !input )
            return false;
        if( op->u.mirror.invert_horizontal )
        {
            for( x = 0; x < width; x++ )
                out0[x] = input[gen->width_mask - x];
        }
        else
            memcpy(out0, input, (size_t)width * sizeof(int32_t));
    }
    else
    {
        const int32_t* in[3];
        int32_t* out0 = cache->plane[0] + offset;
        int32_t* out1 = cache->plane[1] + offset;
        int32_t* out2 = cache->plane[2] + offset;
        if( !proctex_input_colour(gen, op, 0, src_line, in) )
            return false;
        if( op->u.mirror.invert_horizontal )
        {
            for( x = 0; x < width; x++ )
            {
                int sx = gen->width_mask - x;
                out0[x] = in[0][sx];
                out1[x] = in[1][sx];
                out2[x] = in[2][sx];
            }
        }
        else
        {
            memcpy(out0, in[0], (size_t)width * sizeof(int32_t));
            memcpy(out1, in[1], (size_t)width * sizeof(int32_t));
            memcpy(out2, in[2], (size_t)width * sizeof(int32_t));
        }
    }
    return true;
}

/* --- square_waveform ------------------------------------------------------------ */

struct proctex_sqwave_aux
{
    bool ready;
    int count;
    int32_t* table0;
    int32_t* table1;
};

bool
proctex_op_square_waveform(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    struct proctex_sqwave_aux* aux = gen->aux[op_index];
    int width = gen->width;
    size_t offset = (size_t)line * (size_t)width;
    int32_t* out0 = cache->plane[0] + offset;
    int vert = gen->v_gradient[line];
    int x;

    if( !aux )
    {
        aux = calloc(1, sizeof(*aux));
        if( !aux )
            return false;
        gen->aux[op_index] = aux;
    }
    if( !aux->ready )
    {
        int n = op->u.square_waveform.field0;
        int i = 0;
        int j = 4096 / n;
        int k = (j * op->u.square_waveform.field1) >> 12;
        int l;
        aux->count = n;
        aux->table0 = malloc((size_t)(n + 1) * sizeof(int32_t));
        aux->table1 = malloc((size_t)(n + 1) * sizeof(int32_t));
        if( !aux->table0 || !aux->table1 )
            return false;
        for( l = 0; l < n; l++ )
        {
            aux->table1[l] = i;
            aux->table0[l] = i + k;
            i += j;
        }
        aux->table1[n] = 4096;
        aux->table0[n] = aux->table0[0] + 4096;
        aux->ready = true;
    }

    if( op->u.square_waveform.field2 == 0 )
    {
        int value = 0;
        int i;
        for( i = 0; i < aux->count; i++ )
        {
            if( vert >= aux->table1[i] && vert < aux->table1[i + 1] )
            {
                if( vert < aux->table0[i] )
                    value = 4096;
                break;
            }
        }
        for( x = 0; x < width; x++ )
            out0[x] = value;
    }
    else
    {
        for( x = 0; x < width; x++ )
        {
            int index = 0;
            int value = 0;
            int horz = gen->h_gradient[x];
            int i;
            switch( op->u.square_waveform.field2 )
            {
            case 3:
                index = ((horz - vert) >> 1) + 2048;
                break;
            case 2:
                index = ((horz - (4096 - vert)) >> 1) + 2048;
                break;
            case 1:
                index = horz;
                break;
            }
            for( i = 0; i < aux->count; i++ )
            {
                if( index >= aux->table1[i] && index < aux->table1[i + 1] )
                {
                    if( index < aux->table0[i] )
                        value = 4096;
                    break;
                }
            }
            out0[x] = value;
        }
    }
    return true;
}

/* --- brightness ----------------------------------------------------------------- */

bool
proctex_op_brightness(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    int width = gen->width;
    size_t offset = (size_t)line * (size_t)width;
    const int32_t* in[3];
    int32_t* out0 = cache->plane[0] + offset;
    int32_t* out1 = cache->plane[1] + offset;
    int32_t* out2 = cache->plane[2] + offset;
    int x;

    if( !proctex_input_colour(gen, op, 0, line, in) )
        return false;
    for( x = 0; x < width; x++ )
    {
        int r = in[0][x];
        int abs_r = r - op->u.brightness.colour_delta[0];
        if( abs_r < 0 )
            abs_r = -abs_r;
        if( abs_r <= op->u.brightness.max_value )
        {
            int g = in[1][x];
            int abs_g = g - op->u.brightness.colour_delta[1];
            if( abs_g < 0 )
                abs_g = -abs_g;
            if( abs_g <= op->u.brightness.max_value )
            {
                int b = in[2][x];
                int abs_b = b - op->u.brightness.colour_delta[2];
                if( abs_b < 0 )
                    abs_b = -abs_b;
                if( abs_b <= op->u.brightness.max_value )
                {
                    out0[x] = (r * op->u.brightness.red_factor) >> 12;
                    out1[x] = (g * op->u.brightness.green_factor) >> 12;
                    out2[x] = (b * op->u.brightness.blue_factor) >> 12;
                }
                else
                {
                    out0[x] = r;
                    out1[x] = g;
                    out2[x] = b;
                }
            }
            else
            {
                out0[x] = r;
                out1[x] = g;
                out2[x] = in[2][x];
            }
        }
        else
        {
            out0[x] = r;
            out1[x] = in[1][x];
            out2[x] = in[2][x];
        }
    }
    return true;
}

/* --- edge detect ---------------------------------------------------------------- */

bool
proctex_op_mono_edge(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    int width = gen->width;
    size_t offset = (size_t)line * (size_t)width;
    int32_t* out0 = cache->plane[0] + offset;
    const int32_t* prev = proctex_input_mono(gen, op, 0, (line - 1) & gen->height_mask);
    const int32_t* curr = proctex_input_mono(gen, op, 0, line);
    const int32_t* next = proctex_input_mono(gen, op, 0, (line + 1) & gen->height_mask);
    int x;
    if( !prev || !curr || !next )
        return false;
    for( x = 0; x < width; x++ )
    {
        int dy = op->u.edge_detect.multiplier * (next[x] - prev[x]);
        int dx = op->u.edge_detect.multiplier *
                 (curr[(x + 1) & gen->width_mask] - curr[(x - 1) & gen->width_mask]);
        int dx0 = dx >> 12;
        int dy0 = dy >> 12;
        int dy2 = (dy0 * dy0) >> 12;
        int dx2 = (dx0 * dx0) >> 12;
        int local117 = (int)(sqrt((dy2 + dx2 + 4096) / 4096.0) * 4096.0);
        int local128 = local117 == 0 ? 0 : (16777216 / local117);
        out0[x] = 4096 - local128;
    }
    return true;
}

bool
proctex_op_colour_edge(struct ProcTexGenerator* gen, int op_index, int line)
{
    /* Reference uses getMonochromeInput (red of a colour source) for the gradient. */
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    int width = gen->width;
    size_t offset = (size_t)line * (size_t)width;
    int32_t* out0 = cache->plane[0] + offset;
    int32_t* out1 = cache->plane[1] + offset;
    int32_t* out2 = cache->plane[2] + offset;
    const int32_t* prev = proctex_input_mono(gen, op, 0, (line - 1) & gen->height_mask);
    const int32_t* curr = proctex_input_mono(gen, op, 0, line);
    const int32_t* next = proctex_input_mono(gen, op, 0, (line + 1) & gen->height_mask);
    int x;
    if( !prev || !curr || !next )
        return false;
    for( x = 0; x < width; x++ )
    {
        int dy = op->u.edge_detect.multiplier * (next[x] - prev[x]);
        int dx = op->u.edge_detect.multiplier *
                 (curr[(x + 1) & gen->width_mask] - curr[(x - 1) & gen->width_mask]);
        int dy0 = dy >> 12;
        int dx0 = dx >> 12;
        int dy2 = (dy0 * dy0) >> 12;
        int dx2 = (dx0 * dx0) >> 12;
        int local137 = (int)(sqrt((dy2 + dx2 + 4096) / 4096.0) * 4096.0);
        int red, green, blue;
        if( local137 == 0 )
        {
            red = green = blue = 0;
        }
        else
        {
            red = dx / local137;
            green = dy / local137;
            blue = 16777216 / local137;
        }
        if( op->u.edge_detect.field1 )
        {
            red = (red >> 1) + 2048;
            green = (green >> 1) + 2048;
            blue = (blue >> 1) + 2048;
        }
        out0[x] = red;
        out1[x] = green;
        out2[x] = blue;
    }
    return true;
}

/* --- weave / herringbone / mandelbrot / op37 ------------------------------------ */

bool
proctex_op_weave(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    int width = gen->width;
    size_t offset = (size_t)line * (size_t)width;
    int32_t* out0 = cache->plane[0] + offset;
    int thick = op->u.weave.thickness;
    int grad_v = gen->v_gradient[line];
    int x;
    for( x = 0; x < width; x++ )
    {
        int grad_h = gen->h_gradient[x];
        if( grad_h > thick && 4096 - thick > grad_h && grad_v > 2048 - thick &&
            grad_v < thick + 2048 )
        {
            int v = 2048 - grad_h;
            v = v < 0 ? -v : v;
            v <<= 12;
            v /= 2048 - thick;
            out0[x] = 4096 - v;
        }
        else if( 2048 - thick < grad_h && 2048 + thick > grad_h )
        {
            int v = grad_v - 2048;
            v = v < 0 ? -v : v;
            v -= thick;
            v <<= 12;
            out0[x] = v / (2048 - thick);
        }
        else if( thick > grad_v || grad_v > 4096 - thick )
        {
            int v = grad_h - 2048;
            v = v < 0 ? -v : v;
            v -= thick;
            v <<= 12;
            out0[x] = v / (2048 - thick);
        }
        else if( grad_h < thick || 4096 - thick < grad_h )
        {
            int v = 2048 - grad_v;
            v = v < 0 ? -v : v;
            v <<= 12;
            v /= 2048 - thick;
            out0[x] = 4096 - v;
        }
        else
            out0[x] = 0;
    }
    return true;
}

bool
proctex_op_herringbone(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    int width = gen->width;
    size_t offset = (size_t)line * (size_t)width;
    int32_t* out0 = cache->plane[0] + offset;
    int sx = op->u.herringbone.scale_x;
    int sy = op->u.herringbone.scale_y;
    int ratio = op->u.herringbone.ratio;
    int x;
    for( x = 0; x < width; x++ )
    {
        int v_grad = gen->h_gradient[x];
        int h_grad = gen->v_gradient[line];
        int local40 = (sx * v_grad) >> 12;
        int local51 = (sy * h_grad) >> 12;
        int local61 = sx * (v_grad % (4096 / sx));
        int local71 = sy * (h_grad % (4096 / sy));
        if( local71 < ratio )
        {
            for( local40 -= local51; local40 < 0; local40 += 4 )
                ;
            while( local40 > 3 )
                local40 -= 4;
            if( local40 != 1 )
            {
                out0[x] = 0;
                continue;
            }
            if( local61 < ratio )
            {
                out0[x] = 0;
                continue;
            }
        }
        if( local61 < ratio )
        {
            int local131;
            for( local131 = local40 - local51; local131 < 0; local131 += 4 )
                ;
            while( local131 > 3 )
                local131 -= 4;
            if( local131 > 0 )
            {
                out0[x] = 0;
                continue;
            }
        }
        out0[x] = 4096;
    }
    return true;
}

bool
proctex_op_mandelbrot(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    int width = gen->width;
    size_t offset = (size_t)line * (size_t)width;
    int32_t* out0 = cache->plane[0] + offset;
    int f0 = op->u.mandelbrot.field[0];
    int f1 = op->u.mandelbrot.field[1];
    int f2 = op->u.mandelbrot.field[2];
    int f3 = op->u.mandelbrot.field[3];
    int x;
    for( x = 0; x < width; x++ )
    {
        int local42 = f2 + ((gen->h_gradient[x] << 12) / f0);
        int local54 = f3 + ((gen->v_gradient[line] << 12) / f0);
        int local58 = local54;
        int local60 = local42;
        int local64 = 0;
        int local70 = (local42 * local42) >> 12;
        int local76 = (local54 * local54) >> 12;
        while( local70 + local76 < 16384 && local64 < f1 )
        {
            local64++;
            local58 = local54 + ((local58 * local60) >> 12) * 2;
            local60 = local42 + local70 - local76;
            local76 = (local58 * local58) >> 12;
            local70 = (local60 * local60) >> 12;
        }
        out0[x] = local64 >= f1 - 1 ? 0 : ((local64 << 12) / f1);
    }
    return true;
}

bool
proctex_op_op37(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    int width = gen->width;
    size_t offset = (size_t)line * (size_t)width;
    int32_t* out0 = cache->plane[0] + offset;
    const int32_t* f = op->u.op37.field;
    int local22 = gen->v_gradient[line] - 2048;
    int x;

    for( x = 0; x < width; x++ )
    {
        int local33 = gen->h_gradient[x] - 2048;
        int local38 = local33 + f[0];
        int local53, local87, local92;
        int ok1, ok2;
        local38 = local38 >= -2048 ? local38 : local38 + 4096;
        local53 = local22 + f[1];
        local38 = local38 <= 2048 ? local38 : local38 - 4096;
        local53 = local53 >= -2048 ? local53 : local53 + 4096;
        local53 = local53 <= 2048 ? local53 : local53 - 4096;
        local87 = local33 + f[2];
        local92 = local22 + f[3];
        local87 = local87 >= -2048 ? local87 : local87 + 4096;
        local87 = local87 <= 2048 ? local87 : local87 - 4096;
        local92 = local92 >= -2048 ? local92 : local92 + 4096;
        local92 = local92 <= 2048 ? local92 : local92 - 4096;
        {
            int local9 = ((local53 - local38) * f[4]) >> 12;
            int local24 = PROCTEX_COSINE[((local9 * 255) >> 12) & 0xff];
            local24 = (local24 << 12) / f[4];
            local24 = (local24 << 12) / f[6];
            local24 = (f[5] * local24) >> 12;
            ok1 = local24 > local38 + local53 && -local24 < local38 + local53;
        }
        {
            int local13 = ((local92 + local87) * f[4]) >> 12;
            int local23 = PROCTEX_COSINE[((local13 * 255) >> 12) & 0xff];
            local23 = (local23 << 12) / f[4];
            local23 = (local23 << 12) / f[6];
            local23 = (local23 * f[5]) >> 12;
            ok2 = local23 > local92 - local87 && local92 - local87 > -local23;
        }
        out0[x] = (ok1 || ok2) ? 4096 : 0;
    }
    return true;
}

/* --- sprite source / tiling sprite ------------------------------------------------------ */

/*
 * Sample a sprite dependency, resolved by the host through ProcTexSpriteFn.
 *
 * `tiling` selects the TilingSpriteOperation variant, which repeats the sprite at its native
 * size instead of stretching it across the output. Both are the same sampling loop; only the
 * source coordinates differ, which is why the reference has one as a subclass of the other.
 *
 * The reference's two size branches (`width == sprite width` vs not) look different but
 * compute the same channel extraction — checked term by term — so one path serves both.
 */
bool
proctex_op_sprite_source(struct ProcTexGenerator* gen, int op_index, int line, int tiling)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    size_t offset = (size_t)line * (size_t)gen->width;
    int32_t* out_r = cache->plane[0] + offset;
    int32_t* out_g = cache->plane[1] + offset;
    int32_t* out_b = cache->plane[2] + offset;
    const int32_t* argb = NULL;
    int sprite_w = 0;
    int sprite_h = 0;
    int row;

    if( !gen->sprite_fn ||
        !gen->sprite_fn(
            gen->user, op->u.sprite_source.sprite_id, &argb, &sprite_w, &sprite_h) )
        return false;
    if( !argb || sprite_w <= 0 || sprite_h <= 0 )
        return false;

    /* A tiling sprite wraps at the sprite's own height; a stretching one maps the output line
     * onto the sprite's height. Both index by the sprite's row stride. */
    if( tiling )
        row = sprite_w * (line % sprite_h);
    else
        row = sprite_w * (gen->height == sprite_h ? line : (line * sprite_h) / gen->height);

    for( int x = 0; x < gen->width; x++ )
    {
        int src_x;
        int32_t value;

        if( tiling )
            src_x = x % sprite_w;
        else
            src_x = (gen->width == sprite_w) ? x : (sprite_w * x) / gen->width;

        {
            size_t index = (size_t)row + (size_t)src_x;
            if( index >= (size_t)sprite_w * (size_t)sprite_h )
                index = 0;
            value = argb[index];
        }

        out_r[x] = (value >> 12) & 0xFF0;
        out_g[x] = (value & 0xFF00) >> 4;
        out_b[x] = (value & 0xFF) << 4;
    }
    return true;
}

/* --- texture source -------------------------------------------------------------------- */

/*
 * Sample another procedural texture, resolved by the host through ProcTexTextureFn.
 *
 * The reference picks 64 when the material marks the texture `small` and 128 otherwise. We ask
 * for our own output size, which keeps the sampling 1:1 and avoids a resample; the nested
 * program is resolution-independent by construction, so this changes nothing except that we
 * skip a scaling step the reference needs only because it caches at fixed sizes.
 */
bool
proctex_op_texture_source(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    size_t offset = (size_t)line * (size_t)gen->width;
    int32_t* out_r = cache->plane[0] + offset;
    int32_t* out_g = cache->plane[1] + offset;
    int32_t* out_b = cache->plane[2] + offset;
    const int32_t* argb = NULL;
    int size = gen->width;

    if( !gen->texture_fn ||
        !gen->texture_fn(gen->user, op->u.texture_source.texture_id, size, &argb) )
        return false;
    if( !argb )
        return false;

    for( int x = 0; x < gen->width; x++ )
    {
        int32_t value = argb[(size_t)line * (size_t)size + (size_t)x];
        out_r[x] = (value & 0xFF0000) >> 12;
        out_g[x] = (value & 0xFF00) >> 4;
        out_b[x] = (value & 0xFF) << 4;
    }
    return true;
}

/* --- fill helpers ----------------------------------------------------------------- */

/*
 * The reference fills spans through `Int32Array.prototype.fill(value, start, end)`, whose
 * out-of-range behaviour is load-bearing rather than incidental: it CLAMPS both ends to the
 * array and writes nothing when `end <= start`. Several call sites below rely on that —
 * `local97 + local104` in the irregular-brick filler can exceed the row width, and the
 * reference simply lets the clamp absorb it.
 *
 * A plain C loop would run past the row instead, so the clamp is reproduced here rather than
 * left implicit. (JS also treats a negative index as relative to the end; no call site
 * produces one, and clamping to zero is the reading that cannot corrupt memory.)
 */
static void
proctex_fill_range(int32_t* row, int lo, int hi, int width, int32_t value)
{
    if( lo < 0 )
        lo = 0;
    if( hi > width )
        hi = width;
    for( int i = lo; i < hi; i++ )
        row[i] = value;
}

static void
proctex_fill_len(int32_t* row, int start, int len, int width, int32_t value)
{
    proctex_fill_range(row, start, start + len, width, value);
}

/* --- line noise ------------------------------------------------------------------- */

/*
 * `count` short strokes at random positions and angles, each a linear ramp along its own
 * length. Whole-image: the reference renders every scanline on the first request (its cache is
 * `dirty` only until the first `getAll`), so this fills the operation's cache in one pass and
 * marks every line resident.
 *
 * The transposed write in the `!flag` branch is the reference's, not a slip. It indexes
 * `pixels[x][y]` where every other operation indexes `pixels[y][x]`, which is only harmless
 * because a texture is always square here — proctex_setup takes a single `size` for both
 * dimensions. Straightening it would rotate half the strokes by 90 degrees.
 */
bool
proctex_op_line_noise(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    int width = gen->width;
    int height = gen->height;
    int32_t* pixels = cache->plane[0];
    struct proctex_jrand rnd;
    int mid_angle = op->u.line_noise.max_angle >> 1;

    (void)line;
    if( width <= 0 || height <= 0 )
        return false;

    proctex_jrand_seed(&rnd, op->u.line_noise.seed);
    for( int i = 0; i < op->u.line_noise.count; i++ )
    {
        int angle;
        int x0, y0, x1, y1;
        int delta_x, delta_y;
        bool steep;

        if( op->u.line_noise.max_angle > 0 )
            angle = op->u.line_noise.min_angle - mid_angle +
                    proctex_next_int_jagex(&rnd, op->u.line_noise.max_angle);
        else
            angle = op->u.line_noise.min_angle;
        angle = (angle >> 4) & 0xFF;

        x0 = proctex_next_int_jagex(&rnd, width);
        y0 = proctex_next_int_jagex(&rnd, height);
        x1 = ((PROCTEX_COSINE[angle] * op->u.line_noise.length) >> 12) + x0;
        y1 = ((PROCTEX_SINE[angle] * op->u.line_noise.length) >> 12) + y0;
        delta_x = x1 - x0;
        delta_y = y1 - y0;
        if( delta_x == 0 && delta_y == 0 )
            continue;

        if( delta_x < 0 )
            delta_x = -delta_x;
        if( delta_y < 0 )
            delta_y = -delta_y;

        /* Walk along whichever axis the stroke is longer on, by swapping the roles of x
         * and y. `steep` is carried to the write so the swap is undone there. */
        steep = delta_x < delta_y;
        if( steep )
        {
            int t = x0;
            x0 = y0;
            y0 = t;
            t = x1;
            x1 = y1;
            y1 = t;
        }
        if( x0 > x1 )
        {
            int t = x0;
            x0 = x1;
            x1 = t;
            t = y0;
            y0 = y1;
            y1 = t;
        }

        {
            int span_x = x1 - x0;
            int span_y = y1 - y0;
            int cursor_y = y0;
            int error;
            int ramp;
            int jitter;
            int step;

            if( span_y < 0 )
                span_y = -span_y;
            error = -span_x / 2;
            ramp = 2048 / span_x;
            jitter = 1024 - (proctex_next_int_jagex(&rnd, 4096) >> 2);
            step = y1 <= y0 ? -1 : 1;

            for( int x = x0; x < x1; x++ )
            {
                int value = ramp * (x - x0) + (1024 + jitter);
                int row = cursor_y & gen->height_mask;
                int col = x & gen->width_mask;
                error += span_y;
                if( error > 0 )
                {
                    cursor_y += step;
                    error -= span_x;
                }
                if( steep )
                    pixels[(size_t)row * (size_t)width + (size_t)col] = value;
                else
                    pixels[(size_t)col * (size_t)width + (size_t)row] = value;
            }
        }
    }

    proctex_mark_all_done(cache, height);
    return true;
}

/* --- kaleidoscope ----------------------------------------------------------------- */

/*
 * Fold the input into one octant and mirror it eight ways: the angle of the pixel about the
 * centre selects which of eight (x, y) remappings to sample through.
 *
 * `h_gradient` is indexed by y and `v_gradient` by x, which reads backwards and is the
 * reference's. It is invisible for a square texture (the two tables are then identical — the
 * reference literally aliases them), so it is kept literal rather than "corrected" on the
 * strength of a case that cannot distinguish the two.
 */
static void
proctex_kaleidoscope_pos(
    struct ProcTexGenerator* gen,
    int x,
    int y,
    int* out_x,
    int* out_y)
{
    int h_grad = gen->h_gradient[y];
    int v_grad = gen->v_gradient[x];
    /* float32, then compared against double octant boundaries — the reference's
     * Math.fround, which changes which octant a pixel on a boundary lands in. */
    double angle = (double)proctex_fround(atan2((double)(h_grad - 2048), (double)(v_grad - 2048)));
    int x0 = 0;
    int y0 = 0;

    if( angle >= -3.141592653589793 && angle <= -2.356194490192345 )
    {
        x0 = x;
        y0 = y;
    }
    else if( angle <= -1.5707963267948966 && angle >= -2.356194490192345 )
    {
        y0 = x;
        x0 = y;
    }
    else if( angle <= -0.7853981633974483 && angle >= -1.5707963267948966 )
    {
        x0 = gen->width - y;
        y0 = x;
    }
    else if( angle <= 0.0 && angle >= -0.7853981633974483 )
    {
        y0 = gen->height - y;
        x0 = x;
    }
    else if( angle >= 0.0 && angle <= 0.7853981633974483 )
    {
        x0 = gen->width - x;
        y0 = gen->height - y;
    }
    else if( angle >= 0.7853981633974483 && angle <= 1.5707963267948966 )
    {
        x0 = gen->width - y;
        y0 = gen->height - x;
    }
    else if( angle >= 1.5707963267948966 && angle <= 2.356194490192345 )
    {
        x0 = y;
        y0 = gen->height - x;
    }
    else if( angle >= 2.356194490192345 && angle <= 3.141592653589793 )
    {
        y0 = y;
        x0 = gen->width - x;
    }

    *out_x = x0 & gen->width_mask;
    *out_y = y0 & gen->height_mask;
}

bool
proctex_op_kaleidoscope(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    int width = gen->width;
    size_t offset = (size_t)line * (size_t)width;

    if( op->is_monochrome )
    {
        int32_t* out0 = cache->plane[0] + offset;
        for( int pixel = 0; pixel < width; pixel++ )
        {
            int src_x, src_y;
            const int32_t* input;
            proctex_kaleidoscope_pos(gen, pixel, line, &src_x, &src_y);
            input = proctex_input_mono(gen, op, 0, src_y);
            if( !input )
                return false;
            out0[pixel] = input[src_x];
        }
        return true;
    }

    {
        int32_t* out_r = cache->plane[0] + offset;
        int32_t* out_g = cache->plane[1] + offset;
        int32_t* out_b = cache->plane[2] + offset;
        for( int pixel = 0; pixel < width; pixel++ )
        {
            int src_x, src_y;
            const int32_t* input[3];
            proctex_kaleidoscope_pos(gen, pixel, line, &src_x, &src_y);
            if( !proctex_input_colour(gen, op, 0, src_y, input) )
                return false;
            out_r[pixel] = input[0][src_x];
            out_g[pixel] = input[1][src_x];
            out_b[pixel] = input[2][src_x];
        }
    }
    return true;
}

/* --- bricks ----------------------------------------------------------------------- */

/*
 * A regular running-bond brick grid with jittered joints.
 *
 * `seed` is both the RNG seed and the number of courses, and `field0` the bricks per course —
 * the record reuses one field for both roles, which is why a zero there is a degenerate grid
 * rather than a decode error. The reference reaches `table2[-1]` in that case and compares
 * against `undefined`, i.e. every comparison is false and the output is blank; blank is
 * reproduced directly here because C has no undefined to compare against.
 *
 * Three tables, built once per bake and held on the generator's per-op aux slot:
 *   row_edges (table2)  course boundaries down the texture, cumulative
 *   col_edges (table1)  brick boundaries across each course, cumulative, per course
 *   shade     (table0)  the value each brick is filled with
 */
struct proctex_bricks_aux
{
    bool ready;
    int courses; /* seed */
    int columns; /* field0 */
    int32_t* shade;     /* [courses][columns]     */
    int32_t* col_edges; /* [courses][columns + 1] */
    int32_t* row_edges; /* [courses + 1]          */
    int32_t data[];
};

static struct proctex_bricks_aux*
proctex_bricks_init(struct ProcTexGenerator* gen, int op_index)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_bricks_aux* aux = gen->aux[op_index];
    int courses = op->u.bricks.seed;
    int columns = op->u.bricks.field0;
    struct proctex_jrand rnd;
    int ratio_row, ratio_col, half_row, half_col;

    if( aux )
        return aux;

    if( courses <= 0 || columns <= 0 )
    {
        aux = calloc(1, sizeof(*aux));
        if( aux )
            gen->aux[op_index] = aux;
        return aux; /* courses/columns stay 0: the caller emits a blank line */
    }

    /* One allocation: the generator frees an aux slot with a single free(), so the three
     * tables have to live inside it rather than hang off it. */
    aux = calloc(
        1,
        sizeof(*aux) + (size_t)(courses * columns + courses * (columns + 1) + courses + 1) *
                           sizeof(int32_t));
    if( !aux )
        return NULL;
    aux->courses = courses;
    aux->columns = columns;
    aux->shade = aux->data;
    aux->col_edges = aux->shade + (size_t)courses * (size_t)columns;
    aux->row_edges = aux->col_edges + (size_t)courses * (size_t)(columns + 1);

    proctex_jrand_seed(&rnd, op->u.bricks.seed);
    ratio_col = PROCTEX_ONE / columns;
    half_col = ratio_col / 2;
    ratio_row = PROCTEX_ONE / courses;
    half_row = ratio_row / 2;
    aux->row_edges[0] = 0;

    for( int row = 0; row < courses; row++ )
    {
        int32_t* col_edges = aux->col_edges + (size_t)row * (size_t)(columns + 1);
        int32_t* shade = aux->shade + (size_t)row * (size_t)columns;

        if( row > 0 )
        {
            int value = ratio_row;
            int jitter = ((proctex_next_int_jagex(&rnd, PROCTEX_ONE) - 2048) * op->u.bricks.field3) >> 12;
            value += (jitter * half_row) >> 12;
            aux->row_edges[row] = value + aux->row_edges[row - 1];
        }
        col_edges[0] = 0;
        for( int col = 0; col < columns; col++ )
        {
            if( col > 0 )
            {
                int value = ratio_col;
                int jitter =
                    ((proctex_next_int_jagex(&rnd, PROCTEX_ONE) - 2048) * op->u.bricks.field2) >> 12;
                value += (jitter * half_col) >> 12;
                col_edges[col] = col_edges[col - 1] + value;
            }
            shade[col] = op->u.bricks.field7 > 0
                             ? PROCTEX_ONE - proctex_next_int_jagex(&rnd, op->u.bricks.field7)
                             : PROCTEX_ONE;
        }
        col_edges[columns] = PROCTEX_ONE;
    }
    aux->row_edges[courses] = PROCTEX_ONE;

    gen->aux[op_index] = aux;
    return aux;
}

bool
proctex_op_bricks(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    struct proctex_bricks_aux* aux = proctex_bricks_init(gen, op_index);
    int width = gen->width;
    int32_t* out0 = cache->plane[0] + (size_t)line * (size_t)width;
    int half_joint = op->u.bricks.field6 / 2;
    int ratio_col;
    int course = 0;
    int value_y;
    const int32_t* col_edges;

    if( !aux )
        return false;
    if( aux->courses <= 0 || aux->columns <= 0 )
    {
        memset(out0, 0, (size_t)width * sizeof(int32_t));
        return true;
    }
    ratio_col = PROCTEX_ONE / aux->columns;

    value_y = op->u.bricks.field5 + gen->v_gradient[line];
    while( value_y < 0 )
        value_y += PROCTEX_ONE;
    while( value_y > PROCTEX_ONE )
        value_y -= PROCTEX_ONE;

    /* row_edges[0] is 0 and value_y is non-negative, so this always advances at least once —
     * which is what makes the `course - 1` indexing below safe. */
    for( ; course < aux->courses; course++ )
        if( value_y < aux->row_edges[course] )
            break;

    /* Inside the horizontal mortar joint: the whole scanline is background. */
    if( !(value_y > half_joint + aux->row_edges[course - 1] &&
          value_y < aux->row_edges[course] - half_joint) )
    {
        memset(out0, 0, (size_t)width * sizeof(int32_t));
        return true;
    }

    col_edges = aux->col_edges + (size_t)(course - 1) * (size_t)(aux->columns + 1);
    for( int pixel = 0; pixel < width; pixel++ )
    {
        /* Alternate courses shift by field4, which is what makes it a running bond. */
        int offset = (course % 2) != 0 ? -op->u.bricks.field4 : op->u.bricks.field4;
        int value_x = ((ratio_col * offset) >> 12) + gen->h_gradient[pixel];
        int column = 0;

        while( value_x < 0 )
            value_x += PROCTEX_ONE;
        while( value_x > PROCTEX_ONE )
            value_x -= PROCTEX_ONE;

        for( ; column < aux->columns; column++ )
            if( value_x < col_edges[column] )
                break;

        if( col_edges[column - 1] + half_joint < value_x && value_x < col_edges[column] - half_joint )
            out0[pixel] = aux->shade[(size_t)(course - 1) * (size_t)aux->columns + (size_t)(column - 1)];
        else
            out0[pixel] = 0;
    }
    return true;
}

/* --- irregular bricks --------------------------------------------------------------- */

/*
 * Stone-wall courses: bricks of random width and height laid left to right, each row starting
 * at a random horizontal offset and each brick's top following whatever the row below left
 * behind. Whole-image, and genuinely stateful — a brick's vertical start is read out of the
 * previous row's span list, so lines cannot be produced independently.
 *
 * `method69` in the reference is the brick filler: it draws one brick with a bevelled edge,
 * wrapping horizontally. Named `draw_brick` here; the parameter names are the reference's
 * roles rather than its numbers.
 */
struct proctex_irregular_state
{
    struct ProcTexGenerator* gen;
    const struct RSCache_ProcTexOperation* op;
    int32_t* pixels;
    struct proctex_jrand rnd;
    int bevel; /* the reference's anInt79 */
};

static void
proctex_irregular_draw_brick(
    struct proctex_irregular_state* st,
    int rows,
    int cols,
    int x,
    int y)
{
    struct ProcTexGenerator* gen = st->gen;
    int width = gen->width;
    int height = gen->height;
    int shade = st->op->u.irregular_bricks.field8 > 0
                    ? PROCTEX_ONE -
                          proctex_next_int_jagex(&st->rnd, st->op->u.irregular_bricks.field8)
                    : PROCTEX_ONE;
    int bevel_range = (st->bevel * st->op->u.irregular_bricks.field7) >> 12;
    int bevel =
        st->bevel - (bevel_range > 0 ? proctex_next_int_jagex(&st->rnd, bevel_range) : 0);

    if( width <= x )
        x -= width;

    if( bevel <= 0 )
    {
        /* No bevel: a flat fill, split when the brick wraps the right edge. */
        if( width >= cols + x )
        {
            for( int i = 0; i < rows; i++ )
            {
                int row = i + y;
                if( row < 0 || row >= height )
                    continue;
                proctex_fill_len(st->pixels + (size_t)row * (size_t)width, x, cols, width, shade);
            }
        }
        else
        {
            int head = width - x;
            for( int i = 0; i < rows; i++ )
            {
                int row = i + y;
                int32_t* dst;
                if( row < 0 || row >= height )
                    continue;
                dst = st->pixels + (size_t)row * (size_t)width;
                proctex_fill_len(dst, x, head, width, shade);
                proctex_fill_len(dst, 0, cols - head, width, shade);
            }
        }
        return;
    }

    if( rows <= 0 || cols <= 0 )
        return;

    {
        /* The bevel cannot exceed half the brick in either axis, or the two ramps would
         * cross; both are clamped independently. */
        int bevel_x = (cols / 2) >= bevel ? bevel : (cols / 2);
        int bevel_y = bevel > (rows / 2) ? (rows / 2) : bevel;
        int inner_x = x + bevel_x;
        int inner_w = cols - bevel_x * 2;

        for( int i = 0; i < rows; i++ )
        {
            int row_index = i + y;
            int32_t* row;
            if( row_index < 0 || row_index >= height )
                continue;
            row = st->pixels + (size_t)row_index * (size_t)width;

            if( bevel_y <= i )
            {
                int from_bottom = rows - i - 1;
                if( bevel_y <= from_bottom )
                {
                    /* Middle band: full shade, left and right edges ramped. */
                    for( int k = 0; k < bevel_x; k++ )
                        row[gen->width_mask & (x + k)] = row[gen->width_mask & (cols + x - k - 1)] =
                            (shade * k) / bevel_x;
                    if( inner_x + inner_w <= width )
                        proctex_fill_len(row, inner_x, inner_w, width, shade);
                    else
                    {
                        int head = width - inner_x;
                        proctex_fill_len(row, inner_x, head, width, shade);
                        proctex_fill_len(row, 0, inner_w - head, width, shade);
                    }
                }
                else
                {
                    /* Bottom bevel: the row's own ramp, combined with the edge ramp either
                     * multiplicatively (field6 == 0) or by taking the darker of the two. */
                    int row_shade = (from_bottom * shade) / bevel_y;
                    if( st->op->u.irregular_bricks.field6 == 0 )
                    {
                        for( int k = 0; k < bevel_x; k++ )
                        {
                            int edge = (k * shade) / bevel_x;
                            row[gen->width_mask & (x + k)] =
                                row[(cols + x - k - 1) & gen->width_mask] = (row_shade * edge) >> 12;
                        }
                    }
                    else
                    {
                        for( int k = 0; k < bevel_x; k++ )
                        {
                            int edge = (shade * k) / bevel_x;
                            row[gen->width_mask & (k + x)] =
                                row[gen->width_mask & (x + cols - k - 1)] =
                                    row_shade > edge ? edge : row_shade;
                        }
                    }
                    if( width < inner_w + inner_x )
                    {
                        int head = width - inner_x;
                        proctex_fill_len(row, inner_x, head, width, row_shade);
                        proctex_fill_len(row, 0, inner_w - head, width, row_shade);
                    }
                    else
                        proctex_fill_len(row, inner_x, inner_w, width, row_shade);
                }
            }
            else
            {
                /* Top bevel: mirror of the bottom case. */
                int row_shade = (i * shade) / bevel_y;
                if( st->op->u.irregular_bricks.field6 == 0 )
                {
                    for( int k = 0; k < bevel_x; k++ )
                    {
                        int edge = (shade * k) / bevel_x;
                        row[gen->width_mask & (k + x)] =
                            row[(x + cols - k - 1) & gen->width_mask] = (edge * row_shade) >> 12;
                    }
                }
                else
                {
                    for( int k = 0; k < bevel_x; k++ )
                    {
                        int edge = (shade * k) / bevel_x;
                        row[(k + x) & gen->width_mask] =
                            row[(cols + x - k - 1) & gen->width_mask] =
                                row_shade <= edge ? row_shade : edge;
                    }
                }

                if( inner_x + inner_w > width )
                {
                    int head = width - inner_x;
                    proctex_fill_len(row, inner_x, head, width, row_shade);
                    proctex_fill_len(row, 0, inner_w - head, width, row_shade);
                }
                else
                    proctex_fill_len(row, inner_x, inner_w, width, row_shade);
            }
        }
    }
}

bool
proctex_op_irregular_bricks(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    int width = gen->width;
    int height = gen->height;
    struct proctex_irregular_state st;

    /* Span lists for the current and previous row: {x0, x1, bottom_y} each. */
    int32_t (*cur_spans)[3] = NULL;
    int32_t (*prev_spans)[3] = NULL;
    int span_capacity;

    int prev_offset_delta = 0; /* local23: this row's x offset minus the previous row's */
    int row_offset = 0;        /* local30 */
    int cursor_x = 0;          /* local32 */
    int prev_row_offset = 0;   /* local34 */
    int prev_span = 0;         /* local36 */
    bool first_row = true;     /* local38 */
    int prev_span_count = 0;   /* local40 */
    bool row_fits = true;      /* local42 */
    int cur_span_count = 0;    /* local51 */

    int min_w = (op->u.irregular_bricks.field1 * width) >> 12;
    int min_h = (op->u.irregular_bricks.field3 * height) >> 12;
    int max_w = (width * op->u.irregular_bricks.field2) >> 12;
    int max_h = (op->u.irregular_bricks.field4 * height) >> 12;

    bool ok = true;
    /* Rows laid before giving up. A malformed record can wire widths that never reach the
     * right edge; the reference would spin forever, so this bounds it and refuses the
     * texture instead. Never reached by any record in cache.643. */
    int guard = 0;
    const int GUARD_LIMIT = 1 << 20;

    (void)line;

    if( max_h <= 1 || min_w <= 0 || max_w <= min_w || max_h <= min_h )
    {
        /* The reference bails on max_h <= 1 and would throw out of nextIntJagex on the
         * others (its bound must be positive). Blank is the honest result for a course
         * height of one pixel; for the rest, refusing is. */
        if( max_h <= 1 )
        {
            proctex_mark_all_done(cache, height);
            return true;
        }
        return false;
    }

    span_capacity = width / min_w + 1;
    cur_spans = calloc((size_t)span_capacity, sizeof(*cur_spans));
    prev_spans = calloc((size_t)span_capacity, sizeof(*prev_spans));
    if( !cur_spans || !prev_spans )
    {
        free(cur_spans);
        free(prev_spans);
        return false;
    }

    st.gen = gen;
    st.op = op;
    st.pixels = cache->plane[0];
    st.bevel = ((width / 8) * op->u.irregular_bricks.field5) >> 12;
    proctex_jrand_seed(&st.rnd, op->u.irregular_bricks.seed);

    while( ok )
    {
        int brick_w = min_w + proctex_next_int_jagex(&st.rnd, max_w - min_w);
        int brick_h = proctex_next_int_jagex(&st.rnd, max_h - min_h) + min_h;
        int end_x = cursor_x + brick_w;
        int top_y;

        if( ++guard > GUARD_LIMIT )
        {
            ok = false;
            break;
        }

        if( end_x > width )
        {
            brick_w = width - cursor_x;
            end_x = width;
        }

        if( first_row )
            top_y = 0;
        else
        {
            /*
             * This brick's top is the bottom of whatever the previous row left under it.
             * Walk the previous row's spans from the last one used until one covers this
             * brick's right edge; if that took more than one step, the spans skipped over
             * are shorter than the tallest of them and the gaps above them are filled in
             * before this brick is laid.
             */
            int span = prev_span;
            int steps = 0;
            int right_edge = prev_offset_delta + end_x;
            bool found = false;

            top_y = prev_spans[prev_span][2];
            if( right_edge < 0 )
                right_edge += width;
            if( width < right_edge )
                right_edge -= width;

            for( int probe = 0; probe <= prev_span_count; probe++ )
            {
                if( prev_spans[span][0] <= right_edge && right_edge <= prev_spans[span][1] )
                {
                    found = true;
                    break;
                }
                span++;
                if( span >= prev_span_count )
                    span = 0;
                steps++;
            }
            if( !found )
            {
                /* No span in the previous row covers this edge — the row list is
                 * inconsistent, which the reference would spin on. */
                ok = false;
                break;
            }

            if( span != prev_span )
            {
                int left_edge = cursor_x + prev_offset_delta;
                if( left_edge < 0 )
                    left_edge += width;
                if( left_edge > width )
                    left_edge -= width;

                for( int i = 1; i <= steps; i++ )
                {
                    int bottom = prev_spans[(i + prev_span) % prev_span_count][2];
                    if( bottom > top_y )
                        top_y = bottom;
                }
                for( int i = 0; i <= steps; i++ )
                {
                    const int32_t* skipped = prev_spans[(i + prev_span) % prev_span_count];
                    int bottom = skipped[2];
                    int lo, hi;
                    if( bottom == top_y )
                        continue;
                    if( left_edge < right_edge )
                    {
                        lo = left_edge > skipped[0] ? left_edge : skipped[0];
                        hi = right_edge < skipped[1] ? right_edge : skipped[1];
                    }
                    else if( skipped[0] == 0 )
                    {
                        hi = right_edge < skipped[1] ? right_edge : skipped[1];
                        lo = 0;
                    }
                    else
                    {
                        lo = left_edge > skipped[0] ? left_edge : skipped[0];
                        hi = width;
                    }
                    proctex_irregular_draw_brick(
                        &st, top_y - bottom, hi - lo, prev_row_offset + lo, bottom);
                }
            }
            prev_span = span;
        }

        if( height < brick_h + top_y )
            brick_h = height - top_y;
        else
            row_fits = false;

        if( end_x == width )
        {
            proctex_irregular_draw_brick(&st, brick_h, brick_w, cursor_x + row_offset, top_y);
            if( row_fits )
                break; /* the row reached the bottom of the texture: done */

            /* Close the row: publish its spans as the previous row, pick a new random
             * horizontal offset, and find where the new row's origin falls in it. */
            if( cur_span_count >= span_capacity )
            {
                ok = false;
                break;
            }
            first_row = false;
            row_fits = true;
            cur_spans[cur_span_count][0] = cursor_x;
            cur_spans[cur_span_count][1] = end_x;
            cur_spans[cur_span_count][2] = brick_h + top_y;
            prev_span_count = cur_span_count + 1;
            prev_row_offset = row_offset;
            row_offset = proctex_next_int_jagex(&st.rnd, width);
            prev_span = 0;
            prev_offset_delta = row_offset - prev_row_offset;

            {
                int32_t (*swap)[3] = prev_spans;
                prev_spans = cur_spans;
                cur_spans = swap;
            }
            cur_span_count = 0;

            {
                int origin = prev_offset_delta;
                int probe;
                if( origin < 0 )
                    origin += width;
                if( width < origin )
                    origin -= width;
                for( probe = 0; probe <= prev_span_count; probe++ )
                {
                    if( origin >= prev_spans[prev_span][0] && prev_spans[prev_span][1] >= origin )
                        break;
                    prev_span++;
                    if( prev_span_count <= prev_span )
                        prev_span = 0;
                }
                if( probe > prev_span_count )
                {
                    ok = false;
                    break;
                }
                cursor_x = 0;
            }
        }
        else
        {
            if( cur_span_count >= span_capacity )
            {
                ok = false;
                break;
            }
            cur_spans[cur_span_count][0] = cursor_x;
            cur_spans[cur_span_count][1] = end_x;
            cur_spans[cur_span_count][2] = brick_h + top_y;
            cur_span_count++;
            proctex_irregular_draw_brick(&st, brick_h, brick_w, row_offset + cursor_x, top_y);
            cursor_x = end_x;
        }
    }

    free(cur_spans);
    free(prev_spans);
    if( !ok )
        return false;

    proctex_mark_all_done(cache, height);
    return true;
}

/* The vector rasteriser is large enough to be its own unit. */
#include "engine/proctex/proctex_raster.u.c"
