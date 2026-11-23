#define MA_NO_THREADING
#define MA_NO_DEVICE_IO
#include "../common/common.c"

#include "datasource_segment_playback.c"

int main(int argc, char** argv)
{
    ma_register_test("Data Source: Segment Junction Handover",   test_entry__datasource_segment_junction_handover);
    ma_register_test("Data Source: Segment Boundary Continuation", test_entry__datasource_segment_boundary_continuation);
    ma_register_test("Data Source: Segment Playthrough",         test_entry__datasource_segment_playthrough);
    ma_register_test("Data Source: Segment Loop",                test_entry__datasource_segment_loop);

    return ma_run_tests(argc, argv);
}
