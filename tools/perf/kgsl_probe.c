/* ARM32 KGSL ABI from Motorola ghost 5.1.1 msm_kgsl.h. Capability probe,
 * not a renderer performance result. Every successful reservation is put. */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
struct counter_get {uint32_t group,countable,offset,pad[2];};
struct counter_put {uint32_t group,countable,pad[2];};
struct counter_query {uint32_t group;uint32_t* countables;uint32_t count,max,pad[2];};
struct counter_value {uint32_t group,countable;uint64_t value;};
struct counter_read {struct counter_value* values;uint32_t count,pad[2];};
#define GET _IOWR(0x09,0x38,struct counter_get)
#define PUT _IOW(0x09,0x39,struct counter_put)
#define QUERY _IOWR(0x09,0x3a,struct counter_query)
#define READ _IOWR(0x09,0x3b,struct counter_read)
int main(void)
{
    int fd=open("/dev/kgsl-3d0",O_RDWR);if(fd<0){perror("open kgsl");return 1;}
    for(unsigned group=0;group<=14;group++) {
        uint32_t slots[32];struct counter_query q={.group=group,.countables=slots,.count=32};
        int rc=ioctl(fd,QUERY,&q);printf("query group=%u rc=%d errno=%d max=%u",group,rc,rc?errno:0,q.max);
        if(!rc)for(unsigned i=0;i<q.max&&i<32;i++)printf(" %u",slots[i]);
        putchar('\n');
    }
    unsigned candidates[][2]={{12,1},{10,0x1d},{10,0x0e}};
    for(unsigned i=0;i<3;i++) {
        struct counter_get g={.group=candidates[i][0],.countable=candidates[i][1]};
        if(ioctl(fd,GET,&g)){printf("get group=%u countable=%u failed: %s\n",g.group,g.countable,strerror(errno));continue;}
        struct counter_value v={.group=g.group,.countable=g.countable};struct counter_read r={.values=&v,.count=1};
        int rc=ioctl(fd,READ,&r);printf("read group=%u countable=%u rc=%d errno=%d value=%" PRIu64 "\n",g.group,g.countable,rc,rc?errno:0,v.value);
        struct counter_put p={.group=g.group,.countable=g.countable};
        if(ioctl(fd,PUT,&p)){perror("counter put");return 1;}
    }
    close(fd);return 0;
}
