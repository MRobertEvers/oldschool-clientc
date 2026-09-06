/* Included by the pipeline probe. Buffer all samples; file IO follows the
 * last measured frame. No PMU ioctls, per-stage timers or forced GPU drains. */
#include "gles2_frame_times.h"
struct FrameTimeRow {
    unsigned sample, arm, models;
    uint64_t work_us, swap_us, cadence_us, presented_us;
};
static struct {
    bool record, presented;
    uint64_t start_us, previous_present_us;
    unsigned count, capacity;
    struct FrameTimeRow pending, *rows;
} frame_times;

void ToriRS_FrameTimes_Begin(uint64_t start_us)
{
    frame_times.start_us=start_us;
    frame_times.record=false;
    frame_times.presented=false;
}
void ToriRS_FrameTimes_Present(uint64_t before_us, uint64_t after_us)
{
    if( !pipeline_pmu.timings ) return;
    frame_times.pending.swap_us=after_us-before_us;
    frame_times.pending.cadence_us=frame_times.previous_present_us ? after_us-frame_times.previous_present_us : 0;
    frame_times.pending.presented_us=after_us;
    frame_times.previous_present_us=after_us;
    frame_times.presented=true;
}
void ToriRS_FrameTimes_End(uint64_t end_us)
{
    if( !frame_times.record ) return;
    if( !frame_times.presented || !frame_times.pending.cadence_us
        || frame_times.count>=frame_times.capacity )
        pipeline_pmu_error("frame-time missing presentation/capacity");
    frame_times.pending.work_us=end_us-frame_times.start_us;
    frame_times.rows[frame_times.count++]=frame_times.pending;
    frame_times.record=false;
    if( frame_times.count!=frame_times.capacity ) return;
    const char* path=getenv("TORIRS_FRAME_TIME_FILE");
    FILE* output=path ? fopen(path,"w") : NULL;
    if( !output ) pipeline_pmu_error("frame-time output");
    fprintf(output,"sample,arm,models,work_us,swap_us,cadence_us,presented_us\n");
    for( unsigned i=0;i<frame_times.count;i++ ) {
        const struct FrameTimeRow* r=&frame_times.rows[i];
        fprintf(output,"%u,%u,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
            r->sample,r->arm,r->models,r->work_us,r->swap_us,r->cadence_us,r->presented_us);
    }
    if( fclose(output) ) pipeline_pmu_error("frame-time close output");
    free(frame_times.rows);frame_times.rows=NULL;
    TORIRS_ERR("frame-times-complete,%u\n",frame_times.count);
}
