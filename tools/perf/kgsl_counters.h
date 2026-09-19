#ifndef TORIRS_KGSL_COUNTERS_H
#define TORIRS_KGSL_COUNTERS_H
/* Motorola ghost 5.1.1 source, commit 9b58d85b1133aca8d33e7b94c8487484720bf4fd:
 * include/linux/msm_kgsl.h; drivers/gpu/msm/a3xx_reg.h.
 * Read already-reserved kernel counters. Never reserve/reprogram a counter. */
#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
struct KraitKgslQuery { uint32_t group;uint32_t* countables;uint32_t count,max,pad[2]; };
struct KraitKgslValue { uint32_t group,countable;uint64_t value; };
struct KraitKgslRead { struct KraitKgslValue* values;uint32_t count,pad[2]; };
#define KRAIT_KGSL_QUERY _IOWR(0x09,0x3a,struct KraitKgslQuery)
#define KRAIT_KGSL_READ _IOWR(0x09,0x3b,struct KraitKgslRead)
static int krait_kgsl_open(void)
{
    int fd=open("/dev/kgsl-3d0",O_RDWR);if(fd<0)return -1;
    uint32_t slots[32];struct KraitKgslQuery q={.group=10,.countables=slots,.count=32};
    if(ioctl(fd,KRAIT_KGSL_QUERY,&q)){close(fd);return -1;}
    unsigned have=0;
    for(unsigned i=0;i<q.max&&i<32;i++){if(slots[i]==0x1d)have|=1;if(slots[i]==0x0e)have|=2;}
    if(have!=3){close(fd);return -1;}return fd;
}
static int krait_kgsl_read(int fd,uint64_t* alu_cycles,uint64_t* fragment_alu)
{
    struct KraitKgslValue values[]={{10,0x1d,0},{10,0x0e,0}};
    struct KraitKgslRead read={.values=values,.count=2};
    if(ioctl(fd,KRAIT_KGSL_READ,&read))return -1;
    *alu_cycles=values[0].value;*fragment_alu=values[1].value;return 0;
}
#endif
