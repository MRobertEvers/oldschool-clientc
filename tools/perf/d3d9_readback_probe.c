/* d3d9_readback_probe -- can we READ a D3DPOOL_DEFAULT vertex buffer at a
 * usable speed?
 *
 * d3d9_mem_probe establishes that a DEFAULT-pool VB is ordinary private
 * committed memory in this process: 48 MB of buffer costs 48 MB of our
 * working set once written, with no second copy anywhere.  That makes
 * "delete the CPU shadow and treat the D3D9 buffer as the single copy"
 * physically possible -- the bytes are already in system RAM.
 *
 * Whether it is USABLE is a different question.  A D3DUSAGE_WRITEONLY buffer
 * is exactly the buffer a driver is entitled to map write-combined, and WC
 * memory reads at a small fraction of cached DRAM bandwidth because every read
 * misses the cache and is uncacheable.  If reads come back near DRAM speed the
 * shadow can go; if they come back at WC speed, reading retained geometry back
 * out of the driver buffer would be a large regression and the shadow has to
 * stay (or the buffer has to drop WRITEONLY, which costs render bandwidth).
 *
 * Build:
 *   toolchains/mingw64/bin/gcc.exe -O2 -o build/d3d9_readback_probe.exe \
 *       tools/perf/d3d9_readback_probe.c -ld3d9 -lgdi32 -luser32
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RB_VERTEX_BYTES 24u
#define RB_PAGE_VERTS 65536u
#define RB_PAGES 32u
#define RB_VB_BYTES ((UINT)(RB_PAGES * RB_PAGE_VERTS * RB_VERTEX_BYTES))
#define RB_CHUNK_BYTES ((UINT)(RB_PAGE_VERTS * RB_VERTEX_BYTES))
#define RB_PASSES 8

static double
rb_time_copy(const void* src, void* dst, size_t bytes, int passes)
{
    LARGE_INTEGER freq;
    LARGE_INTEGER t0;
    LARGE_INTEGER t1;
    int i;

    assert(src);
    assert(dst);
    assert(passes > 0);
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    for( i = 0; i < passes; i++ )
        memcpy(dst, src, bytes);
    QueryPerformanceCounter(&t1);
    return (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart /
           (double)passes;
}

static void
rb_row(const char* label, double seconds)
{
    assert(label);
    printf(
        "  %-46s %8.3f ms/page   %9.1f MB/s\n",
        label,
        seconds * 1000.0,
        (double)RB_CHUNK_BYTES / seconds / 1048576.0);
}

static void
rb_fill(IDirect3DVertexBuffer9* vb)
{
    UINT offset;

    assert(vb);
    for( offset = 0u; offset < RB_VB_BYTES; offset += RB_CHUNK_BYTES )
    {
        void* mapped = NULL;
        if( FAILED(IDirect3DVertexBuffer9_Lock(
                vb, offset, RB_CHUNK_BYTES, &mapped, 0u)) )
            continue;
        memset(mapped, 0x5a, RB_CHUNK_BYTES);
        IDirect3DVertexBuffer9_Unlock(vb);
    }
}

static void
rb_case(
    IDirect3DDevice9* device,
    const char* label,
    DWORD usage,
    D3DPOOL pool,
    DWORD lock_flags,
    void* dst)
{
    IDirect3DVertexBuffer9* vb = NULL;
    void* mapped = NULL;
    HRESULT hr;

    assert(device);
    assert(label);
    assert(dst);
    hr = IDirect3DDevice9_CreateVertexBuffer(
        device, RB_VB_BYTES, usage, 0u, pool, &vb, NULL);
    if( FAILED(hr) )
    {
        printf("  %-46s CreateVertexBuffer 0x%08lx\n", label, (unsigned long)hr);
        return;
    }
    rb_fill(vb);
    hr = IDirect3DVertexBuffer9_Lock(vb, 0u, RB_CHUNK_BYTES, &mapped, lock_flags);
    if( FAILED(hr) )
    {
        printf("  %-46s Lock 0x%08lx\n", label, (unsigned long)hr);
        IDirect3DVertexBuffer9_Release(vb);
        return;
    }
    /* One untimed pass so the comparison is steady-state, not first-touch. */
    memcpy(dst, mapped, RB_CHUNK_BYTES);
    rb_row(label, rb_time_copy(mapped, dst, RB_CHUNK_BYTES, RB_PASSES));
    IDirect3DVertexBuffer9_Unlock(vb);
    IDirect3DVertexBuffer9_Release(vb);
}

int
main(void)
{
    WNDCLASSA wc;
    HWND hwnd;
    IDirect3D9* d3d = NULL;
    IDirect3DDevice9* device = NULL;
    D3DPRESENT_PARAMETERS pp;
    D3DCAPS9 caps;
    DWORD behavior;
    HRESULT hr;
    void* dst;
    void* control_src;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "d3d9_readback_probe";
    RegisterClassA(&wc);
    hwnd = CreateWindowExA(
        0,
        "d3d9_readback_probe",
        "d3d9_readback_probe",
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
    d3d = Direct3DCreate9(D3D_SDK_VERSION);
    assert(d3d);
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
    behavior = D3DCREATE_FPU_PRESERVE;
    if( caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT )
        behavior |= D3DCREATE_HARDWARE_VERTEXPROCESSING;
    else
        behavior |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;
    hr = IDirect3D9_CreateDevice(
        d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, behavior, &pp, &device);
    if( FAILED(hr) )
    {
        printf("CreateDevice failed 0x%08lx\n", (unsigned long)hr);
        return 2;
    }

    dst = malloc(RB_CHUNK_BYTES);
    assert(dst);
    control_src = malloc(RB_CHUNK_BYTES);
    assert(control_src);
    memset(control_src, 0x5a, RB_CHUNK_BYTES);

    printf("=== d3d9_readback_probe: reading %.2f MB per page out of a locked VB ===\n",
           (double)RB_CHUNK_BYTES / 1048576.0);
    memcpy(dst, control_src, RB_CHUNK_BYTES);
    rb_row(
        "malloc -> malloc (cached DRAM control)",
        rb_time_copy(control_src, dst, RB_CHUNK_BYTES, RB_PASSES));
    rb_case(device, "DEFAULT WRITEONLY, Lock(0)   <-- ours today",
            D3DUSAGE_WRITEONLY, D3DPOOL_DEFAULT, 0u, dst);
    rb_case(device, "DEFAULT WRITEONLY, Lock(READONLY)",
            D3DUSAGE_WRITEONLY, D3DPOOL_DEFAULT, D3DLOCK_READONLY, dst);
    rb_case(device, "DEFAULT no-WRITEONLY, Lock(0)",
            0u, D3DPOOL_DEFAULT, 0u, dst);
    rb_case(device, "DEFAULT no-WRITEONLY, Lock(READONLY)",
            0u, D3DPOOL_DEFAULT, D3DLOCK_READONLY, dst);
    rb_case(device, "DEFAULT WRITEONLY|DYNAMIC, Lock(0)",
            D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, D3DPOOL_DEFAULT, 0u, dst);
    rb_case(device, "MANAGED WRITEONLY, Lock(0)",
            D3DUSAGE_WRITEONLY, D3DPOOL_MANAGED, 0u, dst);
    rb_case(device, "SYSTEMMEM WRITEONLY, Lock(0)",
            D3DUSAGE_WRITEONLY, D3DPOOL_SYSTEMMEM, 0u, dst);

    free(control_src);
    free(dst);
    IDirect3DDevice9_Release(device);
    IDirect3D9_Release(d3d);
    DestroyWindow(hwnd);
    return 0;
}
