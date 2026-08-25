/* d3d9_mem_probe -- settle where a D3DPOOL_DEFAULT resource is charged.
 *
 * The client's page map says the 48 MB static batch arena, the 24 MB static
 * group VB and the 16 MB world atlas are FULLY RESIDENT private pages inside
 * our address space.  Two different mechanisms produce that same page map:
 *
 *   H1  the D3D9 allocation itself lives in system RAM (integrated GPU, or
 *       the aperture) and is mapped into us.  Nothing we do to flags moves it;
 *       only allocating less helps.
 *   H2  the allocation lives in the GPU-local segment AND the user-mode driver
 *       keeps a full-size system-memory shadow because of how we Lock it
 *       (non-DYNAMIC DEFAULT buffer, partial range, D3DLOCK flags == 0).  Then
 *       the bytes are stored TWICE and the shadow is ours to remove.
 *
 * This probe separates them: for each case it reports the delta in working
 * set, in private committed bytes, and in GetAvailableTextureMem.
 *
 *   ws +N and availtex -N        -> one copy and it is charged to us (H1).
 *   ws +N and availtex -N, while PDH "Local Usage" also rises by N
 *                                -> the bytes are stored twice (H2).
 *   ws flat and availtex -N      -> it lives on the GPU side; free to us.
 *   ws +N and availtex flat      -> pure system-memory allocation, no GPU
 *                                   allocation at all (what SWVP should do).
 *
 * Build (win64 lane):
 *   gcc -O2 -o build/d3d9_mem_probe.exe tools/perf/d3d9_mem_probe.c \
 *       -ld3d9 -lgdi32 -luser32 -lpsapi
 * Build (win32/XP lane): same source, i686 compiler from lib/.
 *
 * Run:
 *   build/d3d9_mem_probe.exe            # the client's own device flags
 *   build/d3d9_mem_probe.exe --swvp     # the Windows XP target's device
 *   build/d3d9_mem_probe.exe --pure
 *   build/d3d9_mem_probe.exe --ex       # Direct3DCreate9Ex, where present
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <d3d9.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* Mirrors the client: 32 pages x 65536 verts x 24 B = 48 MB.
 * D3D9_VBO_PAGE and D3D9_BATCH_PAGE_QUANTUM live in
 * src/platform/platform_win32_renderer_d3d9_core.h:62,88; the 24 B vertex is
 * struct TRSPK_VertexD3D9 in 3rd/trspk/d3d9/d3d9_vertex.h. */
#define PROBE_VERTEX_BYTES 24u
#define PROBE_PAGE_VERTS 65536u
#define PROBE_PAGES 32u
#define PROBE_VB_BYTES ((UINT)(PROBE_PAGES * PROBE_PAGE_VERTS * PROBE_VERTEX_BYTES))
/* One page per Lock, which is the client's chunk granularity. */
#define PROBE_CHUNK_BYTES ((UINT)(PROBE_PAGE_VERTS * PROBE_VERTEX_BYTES))

#define PROBE_ATLAS_DIM 2048u
#define PROBE_TILE 128u
#define PROBE_TILES_USED 28u /* what the measured session actually filled */

/* "Never lock it at all" -- the control that tells CreateVertexBuffer's own
 * cost apart from the Lock's. */
#define PROBE_NEVER_LOCK 0xFFFFFFFFu

#define PROBE_FILL_NONE 0
#define PROBE_FILL_LOCKRECT 1
#define PROBE_FILL_UPDATE 2

struct ProbeSample
{
    SIZE_T working_set;
    SIZE_T peak_working_set;
    uint64_t private_committed;
    UINT available_texture_mem;
};

/* Sum every MEM_COMMIT / MEM_PRIVATE region in our own address space.  These
 * are committed bytes, not resident bytes -- reported next to the working set
 * so an allocation the driver commits but never touches is still visible. */
static uint64_t
probe_private_committed(void)
{
    MEMORY_BASIC_INFORMATION mbi;
    uint64_t total = 0;
    uintptr_t addr = 0;

    while( VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == sizeof(mbi) )
    {
        uintptr_t base = (uintptr_t)mbi.BaseAddress;
        if( mbi.RegionSize == 0 )
            break;
        if( mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE )
            total += (uint64_t)mbi.RegionSize;
        if( base + mbi.RegionSize <= addr )
            break;
        addr = base + mbi.RegionSize;
    }
    return total;
}

static void
probe_sample(IDirect3DDevice9* device, struct ProbeSample* out)
{
    PROCESS_MEMORY_COUNTERS pmc;

    assert(device);
    assert(out);
    memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    /* Some paths defer the real allocation to the next flush; make the numbers
     * describe a settled state rather than a queue. */
    (void)IDirect3DDevice9_BeginScene(device);
    (void)IDirect3DDevice9_EndScene(device);
    (void)IDirect3DDevice9_Present(device, NULL, NULL, NULL, NULL);
    Sleep(60);
    if( !GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)) )
    {
        out->working_set = 0;
        out->peak_working_set = 0;
    }
    else
    {
        out->working_set = pmc.WorkingSetSize;
        out->peak_working_set = pmc.PeakWorkingSetSize;
    }
    out->private_committed = probe_private_committed();
    out->available_texture_mem = IDirect3DDevice9_GetAvailableTextureMem(device);
}

static void
probe_report(
    const char* label,
    const struct ProbeSample* before,
    const struct ProbeSample* after)
{
    const double mb = 1048576.0;

    assert(label);
    assert(before);
    assert(after);
    printf(
        "%-48s ws %+8.2f  priv %+8.2f  availtex %+9.2f  | abs ws %8.2f  peak %8.2f\n",
        label,
        ((double)after->working_set - (double)before->working_set) / mb,
        ((double)after->private_committed - (double)before->private_committed) / mb,
        ((double)after->available_texture_mem -
         (double)before->available_texture_mem) /
            mb,
        (double)after->working_set / mb,
        (double)after->peak_working_set / mb);
}

static void
probe_fill_vb(IDirect3DVertexBuffer9* vb, DWORD lock_flags, int whole_buffer)
{
    UINT offset;
    void* mapped = NULL;

    assert(vb);
    if( lock_flags == PROBE_NEVER_LOCK )
        return;
    if( whole_buffer )
    {
        if( SUCCEEDED(IDirect3DVertexBuffer9_Lock(
                vb, 0u, PROBE_VB_BYTES, &mapped, lock_flags)) )
        {
            memset(mapped, 0x5a, PROBE_VB_BYTES);
            IDirect3DVertexBuffer9_Unlock(vb);
        }
        return;
    }
    for( offset = 0u; offset < PROBE_VB_BYTES; offset += PROBE_CHUNK_BYTES )
    {
        mapped = NULL;
        if( FAILED(IDirect3DVertexBuffer9_Lock(
                vb, offset, PROBE_CHUNK_BYTES, &mapped, lock_flags)) )
            continue;
        memset(mapped, 0x5a, PROBE_CHUNK_BYTES);
        IDirect3DVertexBuffer9_Unlock(vb);
    }
}

static void
probe_case_vb(
    IDirect3DDevice9* device,
    const char* label,
    DWORD usage,
    D3DPOOL pool,
    DWORD lock_flags,
    int whole_buffer)
{
    IDirect3DVertexBuffer9* vb = NULL;
    struct ProbeSample before;
    struct ProbeSample after;
    HRESULT hr;

    assert(device);
    assert(label);
    probe_sample(device, &before);
    hr = IDirect3DDevice9_CreateVertexBuffer(
        device, PROBE_VB_BYTES, usage, 0u, pool, &vb, NULL);
    if( FAILED(hr) )
    {
        printf("%-48s CreateVertexBuffer failed 0x%08lx\n", label, (unsigned long)hr);
        return;
    }
    probe_fill_vb(vb, lock_flags, whole_buffer);
    probe_sample(device, &after);
    probe_report(label, &before, &after);
    IDirect3DVertexBuffer9_Release(vb);
}

/* 28 tiles of 128x128 written through LockRect, which is only legal on a
 * DEFAULT texture that was created D3DUSAGE_DYNAMIC. */
static void
probe_fill_tex_lockrect(IDirect3DTexture9* tex, UINT tiles)
{
    const UINT per_row = PROBE_ATLAS_DIM / PROBE_TILE;
    UINT i;

    assert(tex);
    for( i = 0u; i < tiles; i++ )
    {
        D3DLOCKED_RECT locked;
        RECT r;
        UINT y;
        r.left = (LONG)((i % per_row) * PROBE_TILE);
        r.top = (LONG)((i / per_row) * PROBE_TILE);
        r.right = r.left + (LONG)PROBE_TILE;
        r.bottom = r.top + (LONG)PROBE_TILE;
        if( FAILED(IDirect3DTexture9_LockRect(tex, 0u, &locked, &r, 0u)) )
            continue;
        for( y = 0u; y < PROBE_TILE; y++ )
            memset(
                (uint8_t*)locked.pBits + (size_t)y * locked.Pitch,
                0x7f,
                PROBE_TILE * 4u);
        IDirect3DTexture9_UnlockRect(tex, 0u);
    }
}

/* The same 28 tiles delivered by UpdateSurface from ONE 128x128 SYSTEMMEM
 * staging surface (64 KB), which is what a non-DYNAMIC DEFAULT atlas needs. */
static void
probe_fill_tex_updatesurface(
    IDirect3DDevice9* device, IDirect3DTexture9* tex, UINT tiles)
{
    const UINT per_row = PROBE_ATLAS_DIM / PROBE_TILE;
    IDirect3DSurface9* staging = NULL;
    IDirect3DSurface9* dst = NULL;
    D3DLOCKED_RECT locked;
    UINT i;
    UINT y;
    HRESULT hr;

    assert(device);
    assert(tex);
    hr = IDirect3DDevice9_CreateOffscreenPlainSurface(
        device,
        PROBE_TILE,
        PROBE_TILE,
        D3DFMT_A8R8G8B8,
        D3DPOOL_SYSTEMMEM,
        &staging,
        NULL);
    if( FAILED(hr) )
    {
        printf(
            "    CreateOffscreenPlainSurface(SYSTEMMEM) failed 0x%08lx\n",
            (unsigned long)hr);
        return;
    }
    if( SUCCEEDED(IDirect3DSurface9_LockRect(staging, &locked, NULL, 0u)) )
    {
        for( y = 0u; y < PROBE_TILE; y++ )
            memset(
                (uint8_t*)locked.pBits + (size_t)y * locked.Pitch,
                0x7f,
                PROBE_TILE * 4u);
        IDirect3DSurface9_UnlockRect(staging);
    }
    hr = IDirect3DTexture9_GetSurfaceLevel(tex, 0u, &dst);
    if( FAILED(hr) )
    {
        printf("    GetSurfaceLevel failed 0x%08lx\n", (unsigned long)hr);
        IDirect3DSurface9_Release(staging);
        return;
    }
    for( i = 0u; i < tiles; i++ )
    {
        POINT p;
        p.x = (LONG)((i % per_row) * PROBE_TILE);
        p.y = (LONG)((i / per_row) * PROBE_TILE);
        hr = IDirect3DDevice9_UpdateSurface(device, staging, NULL, dst, &p);
        if( FAILED(hr) && i == 0u )
            printf(
                "    UpdateSurface failed 0x%08lx -- this case is not viable here\n",
                (unsigned long)hr);
    }
    IDirect3DSurface9_Release(dst);
    IDirect3DSurface9_Release(staging);
}

static void
probe_case_tex(
    IDirect3DDevice9* device,
    const char* label,
    DWORD usage,
    D3DPOOL pool,
    int fill_mode)
{
    IDirect3DTexture9* tex = NULL;
    struct ProbeSample before;
    struct ProbeSample after;
    HRESULT hr;

    assert(device);
    assert(label);
    probe_sample(device, &before);
    hr = IDirect3DDevice9_CreateTexture(
        device,
        PROBE_ATLAS_DIM,
        PROBE_ATLAS_DIM,
        1u,
        usage,
        D3DFMT_A8R8G8B8,
        pool,
        &tex,
        NULL);
    if( FAILED(hr) )
    {
        printf("%-48s CreateTexture failed 0x%08lx\n", label, (unsigned long)hr);
        return;
    }
    if( fill_mode == PROBE_FILL_LOCKRECT )
        probe_fill_tex_lockrect(tex, PROBE_TILES_USED);
    else if( fill_mode == PROBE_FILL_UPDATE )
        probe_fill_tex_updatesurface(device, tex, PROBE_TILES_USED);
    probe_sample(device, &after);
    probe_report(label, &before, &after);
    IDirect3DTexture9_Release(tex);
}

/* Hold all four of the client's big DEFAULT resources at once.  A per-case
 * delta can hide a driver heap that is reused between cases; only the
 * simultaneous shape reproduces what the 447 MB peak is made of. */
static void
probe_case_all(IDirect3DDevice9* device)
{
    IDirect3DVertexBuffer9* arena = NULL;
    IDirect3DVertexBuffer9* group = NULL;
    IDirect3DTexture9* world = NULL;
    IDirect3DTexture9* ui = NULL;
    struct ProbeSample before;
    struct ProbeSample after;
    void* mapped = NULL;

    assert(device);
    probe_sample(device, &before);
    if( SUCCEEDED(IDirect3DDevice9_CreateVertexBuffer(
            device, PROBE_VB_BYTES, D3DUSAGE_WRITEONLY, 0u, D3DPOOL_DEFAULT, &arena, NULL)) )
        probe_fill_vb(arena, 0u, 0);
    if( SUCCEEDED(IDirect3DDevice9_CreateVertexBuffer(
            device,
            PROBE_VB_BYTES / 2u,
            D3DUSAGE_WRITEONLY,
            0u,
            D3DPOOL_DEFAULT,
            &group,
            NULL)) )
    {
        if( SUCCEEDED(IDirect3DVertexBuffer9_Lock(
                group, 0u, PROBE_VB_BYTES / 2u, &mapped, 0u)) )
        {
            memset(mapped, 0x5a, PROBE_VB_BYTES / 2u);
            IDirect3DVertexBuffer9_Unlock(group);
        }
    }
    if( SUCCEEDED(IDirect3DDevice9_CreateTexture(
            device,
            PROBE_ATLAS_DIM,
            PROBE_ATLAS_DIM,
            1u,
            D3DUSAGE_DYNAMIC,
            D3DFMT_A8R8G8B8,
            D3DPOOL_DEFAULT,
            &world,
            NULL)) )
        probe_fill_tex_lockrect(world, PROBE_TILES_USED);
    (void)IDirect3DDevice9_CreateTexture(
        device,
        PROBE_ATLAS_DIM,
        PROBE_ATLAS_DIM,
        1u,
        D3DUSAGE_DYNAMIC,
        D3DFMT_A8R8G8B8,
        D3DPOOL_DEFAULT,
        &ui,
        NULL);
    probe_sample(device, &after);
    probe_report("ALL FOUR at once (48+24+16+16 = 104 MB)", &before, &after);
    if( ui )
        IDirect3DTexture9_Release(ui);
    if( world )
        IDirect3DTexture9_Release(world);
    if( group )
        IDirect3DVertexBuffer9_Release(group);
    if( arena )
        IDirect3DVertexBuffer9_Release(arena);
}

typedef HRESULT(WINAPI* PFN_Direct3DCreate9Ex)(UINT, IDirect3D9Ex**);

int
main(int argc, char** argv)
{
    WNDCLASSA wc;
    HWND hwnd;
    IDirect3D9* d3d = NULL;
    IDirect3D9Ex* d3d_ex = NULL;
    IDirect3DDevice9* device = NULL;
    D3DPRESENT_PARAMETERS pp;
    D3DCAPS9 caps;
    DWORD behavior;
    HRESULT hr;
    int use_swvp = 0;
    int use_pure = 0;
    int use_ex = 0;
    int only_case = -1;
    int case_index = 0;
    int i;
    struct ProbeSample at_start;

    for( i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--swvp") == 0 )
            use_swvp = 1;
        else if( strcmp(argv[i], "--pure") == 0 )
            use_pure = 1;
        else if( strcmp(argv[i], "--ex") == 0 )
            use_ex = 1;
        else if( strcmp(argv[i], "--case") == 0 && i + 1 < argc )
            only_case = atoi(argv[++i]);
    }

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "d3d9_mem_probe";
    RegisterClassA(&wc);
    hwnd = CreateWindowExA(
        0,
        "d3d9_mem_probe",
        "d3d9_mem_probe",
        WS_OVERLAPPEDWINDOW,
        0,
        0,
        640,
        480,
        NULL,
        NULL,
        wc.hInstance,
        NULL);
    assert(hwnd);

    if( use_ex )
    {
        HMODULE lib = LoadLibraryA("d3d9.dll");
        PFN_Direct3DCreate9Ex create_ex = NULL;
        assert(lib);
        create_ex = (PFN_Direct3DCreate9Ex)(void*)GetProcAddress(lib, "Direct3DCreate9Ex");
        if( !create_ex )
        {
            printf("Direct3DCreate9Ex is not exported here (pre-Vista); drop --ex\n");
            return 2;
        }
        hr = create_ex(D3D_SDK_VERSION, &d3d_ex);
        if( FAILED(hr) )
        {
            printf("Direct3DCreate9Ex failed 0x%08lx\n", (unsigned long)hr);
            return 2;
        }
        d3d = (IDirect3D9*)d3d_ex;
    }
    else
    {
        d3d = Direct3DCreate9(D3D_SDK_VERSION);
        assert(d3d);
    }

    memset(&caps, 0, sizeof(caps));
    (void)IDirect3D9_GetDeviceCaps(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps);

    memset(&pp, 0, sizeof(pp));
    pp.BackBufferWidth = 640;
    pp.BackBufferHeight = 480;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D16;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    /* The same behavior word the client builds at
     * src/platform/platform_win32_renderer_d3d9_core.c:6522-6526, plus the two
     * variants we want to rule in or out. */
    behavior = D3DCREATE_FPU_PRESERVE;
    if( !use_swvp && (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) )
    {
        behavior |= D3DCREATE_HARDWARE_VERTEXPROCESSING;
        if( use_pure )
            behavior |= D3DCREATE_PUREDEVICE;
    }
    else
        behavior |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;

    if( use_ex )
        hr = IDirect3D9Ex_CreateDeviceEx(
            d3d_ex,
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            hwnd,
            behavior,
            &pp,
            NULL,
            (IDirect3DDevice9Ex**)&device);
    else
        hr = IDirect3D9_CreateDevice(
            d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, behavior, &pp, &device);
    if( FAILED(hr) )
    {
        printf(
            "CreateDevice failed 0x%08lx (behavior 0x%08lx)\n",
            (unsigned long)hr,
            (unsigned long)behavior);
        return 2;
    }

    probe_sample(device, &at_start);
    printf(
        "=== d3d9_mem_probe  behavior=0x%08lx%s ===\n",
        (unsigned long)behavior,
        use_ex ? " (Ex)" : "");
    printf("device-only working set   %8.2f MB\n", at_start.working_set / 1048576.0);
    printf(
        "GetAvailableTextureMem    %8.2f MB\n",
        at_start.available_texture_mem / 1048576.0);
    printf(
        "deltas are MB: %.2f MB per vertex-buffer case, %.2f MB per texture case\n\n",
        PROBE_VB_BYTES / 1048576.0,
        (PROBE_ATLAS_DIM * PROBE_ATLAS_DIM * 4u) / 1048576.0);

    /* --- vertex buffers: the static batch arena's exact geometry ---------- */
    if( only_case < 0 || only_case == case_index++ )
        probe_case_vb(
        device,
        "VB DEFAULT WRITEONLY, never locked",
        D3DUSAGE_WRITEONLY,
        D3DPOOL_DEFAULT,
        PROBE_NEVER_LOCK,
        0);
    if( only_case < 0 || only_case == case_index++ )
        probe_case_vb(
        device,
        "VB DEFAULT WRITEONLY, 32x Lock(flags=0)   <-- ours",
        D3DUSAGE_WRITEONLY,
        D3DPOOL_DEFAULT,
        0u,
        0);
    if( only_case < 0 || only_case == case_index++ )
        probe_case_vb(
        device,
        "VB DEFAULT WRITEONLY, 1x Lock(flags=0) whole",
        D3DUSAGE_WRITEONLY,
        D3DPOOL_DEFAULT,
        0u,
        1);
    if( only_case < 0 || only_case == case_index++ )
        probe_case_vb(
        device,
        "VB DEFAULT WRITEONLY|DYNAMIC, 32x NOOVERWRITE",
        D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC,
        D3DPOOL_DEFAULT,
        D3DLOCK_NOOVERWRITE,
        0);
    if( only_case < 0 || only_case == case_index++ )
        probe_case_vb(
        device,
        "VB DEFAULT WRITEONLY|DYNAMIC, 1x DISCARD whole",
        D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC,
        D3DPOOL_DEFAULT,
        D3DLOCK_DISCARD,
        1);
    if( !use_ex && /* MANAGED does not exist on a D3D9Ex device. */
        (only_case < 0 || only_case == case_index++) )
        probe_case_vb(
            device,
            "VB MANAGED WRITEONLY, 32x Lock (known-double)",
            D3DUSAGE_WRITEONLY,
            D3DPOOL_MANAGED,
            0u,
            0);
    if( only_case < 0 || only_case == case_index++ )
        probe_case_vb(
        device,
        "VB SYSTEMMEM WRITEONLY, 32x Lock (sysmem floor)",
        D3DUSAGE_WRITEONLY,
        D3DPOOL_SYSTEMMEM,
        0u,
        0);

    printf("\n");
    /* --- textures: the world atlas's exact geometry ----------------------- */
    if( only_case < 0 || only_case == case_index++ )
        probe_case_tex(
        device,
        "TEX DEFAULT DYNAMIC, never locked         <-- ours",
        D3DUSAGE_DYNAMIC,
        D3DPOOL_DEFAULT,
        PROBE_FILL_NONE);
    if( only_case < 0 || only_case == case_index++ )
        probe_case_tex(
        device,
        "TEX DEFAULT DYNAMIC, 28 tile LockRects",
        D3DUSAGE_DYNAMIC,
        D3DPOOL_DEFAULT,
        PROBE_FILL_LOCKRECT);
    if( only_case < 0 || only_case == case_index++ )
        probe_case_tex(
        device,
        "TEX DEFAULT non-dynamic, never written",
        0u,
        D3DPOOL_DEFAULT,
        PROBE_FILL_NONE);
    if( only_case < 0 || only_case == case_index++ )
        probe_case_tex(
        device,
        "TEX DEFAULT non-dynamic, 28 UpdateSurface",
        0u,
        D3DPOOL_DEFAULT,
        PROBE_FILL_UPDATE);
    if( !use_ex && (only_case < 0 || only_case == case_index++) )
        probe_case_tex(
            device,
            "TEX MANAGED, 28 LockRects (known-double)",
            0u,
            D3DPOOL_MANAGED,
            PROBE_FILL_LOCKRECT);

    printf("\n");
    if( only_case < 0 || only_case == case_index++ )
        probe_case_all(device);

    IDirect3DDevice9_Release(device);
    IDirect3D9_Release(d3d);
    DestroyWindow(hwnd);
    return 0;
}
