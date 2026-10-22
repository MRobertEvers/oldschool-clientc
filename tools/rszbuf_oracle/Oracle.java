// Drives the rev-241 deob's class162 over a deterministic stream of triangles
// and prints one hash of the pixel buffer and one of the depth buffer per case.
//
// The C side, 3rd/toridraw/toridraw_rszbuf_test.c, replays the same stream
// through the rszbuf rasterizer: the two must agree on every case. Both the
// stream (xorshift32, the order every value is drawn) and the hash are part of
// that contract -- change one side and the other with it.
public class Oracle {
	static final int W = 240, H = 180;
	static int s;

	static int next() { s ^= s << 13; s ^= s >>> 17; s ^= s << 5; return s; }
	static int rand(int n) { return (next() & 0x7fffffff) % n; }

	static float depthKey(float z) {
		float d = (z - 75.0F) * 1.0100503F;
		return 150.75377F / d + 1.0100503F;
	}

	static int hash(int h, int v) { h ^= v; return h * 16777619; }

	public static void main(String[] args) {
		int cases = Integer.parseInt(args[0]);
		s = 0x2545F491;
		int[] palette = new int[65536];
		for (int i = 0; i < 65536; i++) palette[i] = (i * 0x9E3779B1 ^ (i << 7)) & 0xFFFFFF;
		int[] texels = new int[128 * 128];
		for (int i = 0; i < texels.length; i++) texels[i] = next() & 0xFFFFFF;
		int[] keyed = new int[128 * 128];
		for (int i = 0; i < keyed.length; i++) keyed[i] = (i % 7 == 0) ? 0 : texels[i];

		class162 r = new class162();
		r.field2156 = palette;
		class175 st = r.field2157;
		st.field2456 = W;
		st.field2457 = H;
		st.field2454 = W / 2;
		st.field2455 = H / 2;
		for (int y = 0; y < 1024; y++) st.field2462[y] = y * W;
		Statics.field7153 = W;
		Statics.field7152 = new int[W * H];
		Statics.field7156 = new float[W * H];

		for (int c = 0; c < cases; c++) {
			for (int i = 0; i < W * H; i++) {
				Statics.field7152[i] = (i * 0x9E3779B1) ^ c;
				Statics.field7156[i] = (rand(4) == 0) ? 0.0F : (((i * 7919 + c) % 1000) / 700.0F + 1.0F);
			}
			int mode = rand(4);
			st.field2450 = rand(2) == 0;
			st.field2465 = rand(5) == 0 ? 0 : -1;
			st.field2442 = rand(5) == 0 ? 0 : -1;
			int a = rand(6);
			st.field2446 = a < 3 ? 0 : (a == 3 ? 254 : rand(253) + 1);

			float[] ys = new float[3], xs = new float[3], ks = new float[3];
			int grid = rand(3);
			for (int v = 0; v < 3; v++) {
				if (grid == 0) {
					ys[v] = (rand(H + 80) - 40) + rand(4) / 4.0F;
					xs[v] = (rand(W + 80) - 40) + rand(8) / 8.0F;
				} else if (grid == 1) {
					ys[v] = rand(H + 80) - 40;
					xs[v] = rand(W + 80) - 40;
				} else {
					ys[v] = (rand(12) - 1) * 20;
					xs[v] = (rand(16) - 1) * 20;
				}
				ks[v] = depthKey(60 + rand(4000));
			}
			st.field2459 = xs[0] < 0 || xs[1] < 0 || xs[2] < 0 || xs[0] > W || xs[1] > W || xs[2] > W;

			if (mode == 0) {
				int rgb = next() & 0xFFFFFF;
				r.method5169(ys[0], ys[1], ys[2], xs[0], xs[1], xs[2], ks[0], ks[1], ks[2], rgb);
			} else if (mode == 1) {
				r.method5165(ys[0], ys[1], ys[2], xs[0], xs[1], xs[2], ks[0], ks[1], ks[2],
					rand(65536), rand(65536), rand(65536));
			} else {
				st.field2453.opaque = rand(2) == 0;
				st.field2453.texels = st.field2453.opaque ? texels : keyed;
				int[] f = new int[9];
				for (int i = 0; i < 3; i++) {
					f[i] = rand(1000) - 500;
					f[3 + i] = rand(1000) - 500;
					f[6 + i] = 100 + rand(2000);
				}
				int s0 = rand(128), s1 = rand(128), s2 = rand(128);
				if (mode == 2)
					r.method5170(ys[0], ys[1], ys[2], xs[0], xs[1], xs[2], ks[0], ks[1], ks[2], s0, s1, s2,
						f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], 1);
				else
					r.method5171(ys[0], ys[1], ys[2], xs[0], xs[1], xs[2], ks[0], ks[1], ks[2], s0, s1, s2,
						f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], 1);
			}

			int hp = 0x811C9DC5, hz = 0x811C9DC5;
			for (int i = 0; i < W * H; i++) {
				hp = hash(hp, Statics.field7152[i]);
				hz = hash(hz, Float.floatToRawIntBits(Statics.field7156[i]));
			}
			System.out.printf("%d %d %08x %08x%n", c, mode, hp, hz);
		}
	}
}
