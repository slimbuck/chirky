#include "../src/trace.h"
#include <assert.h>

int main(void)
{
    struct trace_capture t={0};
    assert(!trace_arm(&t,0,16667));assert(!trace_arm(&t,1801,16667));
    assert(trace_arm(&t,2,16667));assert(!trace_arm(&t,2,16667));
    trace_scope_at(&t,"disabled",true,0,0,0);assert(!t.count);
    for(unsigned frame=0;frame<2;frame++) {
        uint64_t start=1000000+frame*16667;
        t.recording=true;
        trace_scope_at(&t,"frame",true,0,start,0);
        trace_scope_at(&t,"robot",true,0,start+1,1);
        trace_scope_at(&t,"robot.rasterise",true,0,start+2,2);
        trace_scope_at(&t,"robot.rasterise",false,0,start+502,502);
        trace_scope_at(&t,"robot",false,100,start+1001,1001);
        trace_scope_at(&t,"present.wait",true,100,start+1002,1002);
        trace_scope_at(&t,"present.wait",false,100,start+16666,1012);
        trace_scope_at(&t,"frame",false,100,start+16667,1013);
        t.frames++;
        assert(!t.depth && !t.invalid);
    }
    assert(t.spans[1].parent==0 && t.spans[2].parent==1 && t.spans[5].parent==4);
    assert(t.spans[3].end-t.spans[3].start==15664);
    assert(t.spans[3].cpu_end-t.spans[3].cpu_start==10);
    assert(trace_write(&t,"build/test-profile.json"));
    free(t.spans);t=(struct trace_capture){0};
    assert(trace_arm(&t,1,16667));t.recording=true;
    trace_scope_at(&t,"open",true,0,0,0);
    trace_scope_at(&t,"wrong",false,0,1,1);assert(t.invalid);
    free(t.spans);t=(struct trace_capture){0};
    assert(trace_arm(&t,1,16667));t.recording=true;
    for(unsigned i=0;i<TRACE_DEPTH+1;i++)trace_scope_at(&t,"deep",true,0,i,i);
    assert(t.invalid);
    free(t.spans);t=(struct trace_capture){0};
    assert(trace_arm(&t,1,16667));t.recording=true;
    for(unsigned i=0;i<65;i++) {
        trace_scope_at(&t,"full",true,0,i,i);trace_scope_at(&t,"full",false,0,i,i);
    }
    assert(t.invalid && t.count==64);free(t.spans);
    puts("Live trace: bounded capture, nesting, CPU/wall separation and overflow checks passed.");
}
