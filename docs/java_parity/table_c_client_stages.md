### C client (torirs, --soft3d) frame stages

Frame mean 34.80 ms under TORIRS_PERF=1. The profiler is ~69% of the frame
on this box, so read the shares, not the milliseconds.

| stage | mean/frame | share of frame |
|---|---|---|
| render | 30.02 ms | 86.3 % |
| present | 1.16 ms | 3.3 % |
| app_run | 2.86 ms | 8.2 % |
| interact | 0.57 ms | 1.6 % |
| emit | 0.39 ms | 1.1 % |
| paint | 0.47 ms | 1.4 % |
| build | 0.48 ms | 1.4 % |
| logic | 0.99 ms | 2.8 % |
| cs2 | 0.65 ms | 1.9 % |
| async | 0.55 ms | 1.6 % |
| layout | 0.00 ms | 0.0 % |
| platform_poll | 0.03 ms | 0.1 % |
| input_prep | 0.05 ms | 0.1 % |

### Inside `render` (30.02 ms, 86.3 % of frame)

| sub-stage | mean/frame | share of render | share of frame |
|---|---|---|---|
| r_model | 22.82 ms | 76.0 % | 65.6 % |
| r_raster | 7.32 ms | 24.4 % | 21.0 % |
| r_project | 3.49 ms | 11.6 % | 10.0 % |
| r_sort | 3.09 ms | 10.3 % | 8.9 % |
| r_sprite | 2.31 ms | 7.7 % | 6.6 % |
| r_font | 0.39 ms | 1.3 % | 1.1 % |
| r_clear | 0.57 ms | 1.9 % | 1.6 % |
| r_rect | 0.01 ms | 0.0 % | 0.0 % |
| r_other | 0.12 ms | 0.4 % | 0.3 % |
| **chrome (sprite+font+rect)** | **2.71 ms** | **9.0 %** | **7.8 %** |

