#include "../common/common.c"

#include "nodegraph_timing.c"

int main(int argc, char** argv)
{
    ma_register_test("Node Graph Timing: Scheduled Ranges", test_entry__node_graph_timing_scheduled_ranges);
    ma_register_test("Node Graph Timing: Stop And Rejoin",  test_entry__node_graph_timing_stop_and_rejoin);
    ma_register_test("Node Graph Timing: Batched Reads",    test_entry__node_graph_timing_batched_reads);

    return ma_run_tests(argc, argv);
}
