/*
Integration tests for the node graph timing model during offline mixing.

These tests exercise the contract documented in section "7.1. Timing" of miniaudio.h:

  - There are two clocks: a local clock per node and a global clock per graph.
  - The global clock (ma_node_graph_get_time()) advances whenever
    ma_node_graph_read_pcm_frames() is called.
  - A node's local clock (ma_node_get_time()) only advances while the node is
    started, and only by the number of output frames it actually processed.
  - ma_node_set_state_time() schedules frame-exact starts/stops against the
    global clock, independent of the block size used by the caller.

The scenarios covered:

  1. test_entry__node_graph_timing_scheduled_ranges
     Two sources with different scheduled play ranges are mixed into one graph.
     After the mix, each source's local clock must report exactly the number of
     frames it actually performed, and the mixed output must show which source
     was audible at every frame.

  2. test_entry__node_graph_timing_stop_and_rejoin
     A source is scheduled to rest (stop) and later rejoin. While resting its
     local clock must not advance, and when it rejoins the audible content must
     continue from the source position indicated by its local clock.

  3. test_entry__node_graph_timing_batched_reads
     The caller drains the mix in irregular batches. After every batch the
     global clock must equal the total number of frames handed out, and the
     per-node clocks and mixed content must be identical to a single-shot read
     (scheduling is frame-exact regardless of read chunking).
*/

#define TEST_TIMING_EPSILON 0.0001f

static ma_bool32 test_timing__check_frame_range(const char* pLabel, const float* pFrames, ma_uint64 beg, ma_uint64 end, float expected)
{
    ma_uint64 i;
    for (i = beg; i < end; i += 1) {
        if (pFrames[i] < expected - TEST_TIMING_EPSILON || pFrames[i] > expected + TEST_TIMING_EPSILON) {
            printf("    FAILED: %s: frame %llu: expected %f, got %f\n", pLabel, (unsigned long long)i, (double)expected, (double)pFrames[i]);
            return MA_FALSE;
        }
    }
    return MA_TRUE;
}

#define TEST_TIMING_CHECK(condition)                                            \
    do {                                                                        \
        if (!(condition)) {                                                     \
            printf("    FAILED: %s (line %d)\n", #condition, __LINE__);         \
            hasFailed = MA_TRUE;                                                \
        }                                                                       \
    } while (0)

#define TEST_TIMING_CHECK_RANGE(pFrames, beg, end, expected)                    \
    do {                                                                        \
        if (!test_timing__check_frame_range(#expected, pFrames, beg, end, expected)) { \
            hasFailed = MA_TRUE;                                                \
        }                                                                       \
    } while (0)


/* Initializes a mono f32 data source node from a constant frame block and attaches it to the graph endpoint. */
static ma_result test_timing__init_source(ma_node_graph* pGraph, const float* pData, ma_uint64 frameCount, ma_audio_buffer* pBuffer, ma_data_source_node* pNode)
{
    ma_result result;
    ma_audio_buffer_config bufferConfig;
    ma_data_source_node_config nodeConfig;

    /* ma_audio_buffer_init() references the data without copying, so pData must outlive the node. */
    bufferConfig = ma_audio_buffer_config_init(ma_format_f32, 1, frameCount, pData, NULL);
    result = ma_audio_buffer_init(&bufferConfig, pBuffer);
    if (result != MA_SUCCESS) {
        return result;
    }

    nodeConfig = ma_data_source_node_config_init(pBuffer);
    result = ma_data_source_node_init(pGraph, &nodeConfig, NULL, pNode);
    if (result != MA_SUCCESS) {
        ma_audio_buffer_uninit(pBuffer);
        return result;
    }

    result = ma_node_attach_output_bus(pNode, 0, ma_node_graph_get_endpoint(pGraph), 0);
    if (result != MA_SUCCESS) {
        ma_data_source_node_uninit(pNode, NULL);
        ma_audio_buffer_uninit(pBuffer);
        return result;
    }

    return MA_SUCCESS;
}

static void test_timing__uninit_source(ma_audio_buffer* pBuffer, ma_data_source_node* pNode)
{
    ma_data_source_node_uninit(pNode, NULL);
    ma_audio_buffer_uninit(pBuffer);
}


/*
Test 1: Two sources performing over different scheduled ranges in the same mix.

    source A: constant 1.0, scheduled to play global frames [0, 300)
    source B: constant 0.5, scheduled to play global frames [100, 400)

The mix is read out to global frame 480. Expected mixed output:

    [0,   100) -> 1.0  (A only)
    [100, 300) -> 1.5  (A + B)
    [300, 400) -> 0.5  (B only)
    [400, 480) -> 0.0  (both finished)

Each source's local clock must read exactly the number of frames it performed
(300 each), while the global clock reads the full 480 frames handed out.
*/
#define T1_SOURCE_FRAMES  300
#define T1_TOTAL_FRAMES   480

static float g_t1_dataA[T1_SOURCE_FRAMES];
static float g_t1_dataB[T1_SOURCE_FRAMES];
static float g_t1_output[T1_TOTAL_FRAMES];

int test_entry__node_graph_timing_scheduled_ranges(int argc, char** argv)
{
    ma_bool32 hasFailed = MA_FALSE;
    ma_result result;
    ma_node_graph_config graphConfig;
    ma_node_graph graph;
    ma_audio_buffer bufferA;
    ma_audio_buffer bufferB;
    ma_data_source_node nodeA;
    ma_data_source_node nodeB;
    ma_uint64 framesRead = 0;
    ma_uint32 i;

    (void)argc;
    (void)argv;

    for (i = 0; i < T1_SOURCE_FRAMES; i += 1) {
        g_t1_dataA[i] = 1.0f;
        g_t1_dataB[i] = 0.5f;
    }
    for (i = 0; i < T1_TOTAL_FRAMES; i += 1) {
        g_t1_output[i] = 999.0f;    /* Poison so unwritten frames are detected. */
    }

    graphConfig = ma_node_graph_config_init(1);
    result = ma_node_graph_init(&graphConfig, NULL, &graph);
    TEST_TIMING_CHECK(result == MA_SUCCESS);
    if (result != MA_SUCCESS) {
        return 1;
    }

    result = test_timing__init_source(&graph, g_t1_dataA, T1_SOURCE_FRAMES, &bufferA, &nodeA);
    TEST_TIMING_CHECK(result == MA_SUCCESS);
    result = test_timing__init_source(&graph, g_t1_dataB, T1_SOURCE_FRAMES, &bufferB, &nodeB);
    TEST_TIMING_CHECK(result == MA_SUCCESS);

    /* Schedule the performance ranges against the global clock. */
    ma_node_set_state_time(&nodeA, ma_node_state_started, 0);
    ma_node_set_state_time(&nodeA, ma_node_state_stopped, 300);
    ma_node_set_state_time(&nodeB, ma_node_state_started, 100);
    ma_node_set_state_time(&nodeB, ma_node_state_stopped, 400);

    /* The schedule must be readable back exactly as written. */
    TEST_TIMING_CHECK(ma_node_get_state_time(&nodeA, ma_node_state_started) == 0);
    TEST_TIMING_CHECK(ma_node_get_state_time(&nodeA, ma_node_state_stopped) == 300);
    TEST_TIMING_CHECK(ma_node_get_state_time(&nodeB, ma_node_state_started) == 100);
    TEST_TIMING_CHECK(ma_node_get_state_time(&nodeB, ma_node_state_stopped) == 400);

    /* No frames have been performed yet: every clock must read zero. */
    TEST_TIMING_CHECK(ma_node_graph_get_time(&graph) == 0);
    TEST_TIMING_CHECK(ma_node_get_time(&nodeA) == 0);
    TEST_TIMING_CHECK(ma_node_get_time(&nodeB) == 0);

    /* Perform the whole mix offline in one read. */
    result = ma_node_graph_read_pcm_frames(&graph, g_t1_output, T1_TOTAL_FRAMES, &framesRead);
    TEST_TIMING_CHECK(result == MA_SUCCESS);
    TEST_TIMING_CHECK(framesRead == T1_TOTAL_FRAMES);

    /* The global clock advances by the number of frames handed out. */
    TEST_TIMING_CHECK(ma_node_graph_get_time(&graph) == T1_TOTAL_FRAMES);

    /*
    Each local clock reports how many frames that source actually performed.
    A played [0,300) and B played [100,400): 300 frames each, even though the
    graph as a whole advanced by 480.
    */
    TEST_TIMING_CHECK(ma_node_get_time(&nodeA) == 300);
    TEST_TIMING_CHECK(ma_node_get_time(&nodeB) == 300);

    /* The mixed output must show which source was audible at every frame. */
    TEST_TIMING_CHECK_RANGE(g_t1_output,   0, 100, 1.0f);   /* A only. */
    TEST_TIMING_CHECK_RANGE(g_t1_output, 100, 300, 1.5f);   /* A + B. */
    TEST_TIMING_CHECK_RANGE(g_t1_output, 300, 400, 0.5f);   /* B only. */
    TEST_TIMING_CHECK_RANGE(g_t1_output, 400, T1_TOTAL_FRAMES, 0.0f);   /* Both finished. */

    test_timing__uninit_source(&bufferA, &nodeA);
    test_timing__uninit_source(&bufferB, &nodeB);
    ma_node_graph_uninit(&graph, NULL);

    return (hasFailed) ? 1 : 0;
}


/*
Test 2: A source rests and then rejoins the performance.

    source C: data[i] = i, scheduled to play [0, 200), rest [200, 400), rejoin [400, 600)

While resting, the source's local clock must stay frozen at 200 (the frames it
has performed so far). When it rejoins at global frame 400, the audible content
must continue from source frame 200 - the position its local clock was left at -
so the value heard at any global frame matches the source's own counter.
*/
#define T2_SOURCE_FRAMES  512
#define T2_BATCH_FRAMES   200

static float g_t2_data[T2_SOURCE_FRAMES];
static float g_t2_output[T2_BATCH_FRAMES];

int test_entry__node_graph_timing_stop_and_rejoin(int argc, char** argv)
{
    ma_bool32 hasFailed = MA_FALSE;
    ma_result result;
    ma_node_graph_config graphConfig;
    ma_node_graph graph;
    ma_audio_buffer bufferC;
    ma_data_source_node nodeC;
    ma_uint64 framesRead = 0;
    ma_uint64 localTimeAtRejoin;
    ma_uint32 i;

    (void)argc;
    (void)argv;

    /* The source encodes its own frame index so the heard value identifies the performed frame. */
    for (i = 0; i < T2_SOURCE_FRAMES; i += 1) {
        g_t2_data[i] = (float)i;
    }

    graphConfig = ma_node_graph_config_init(1);
    result = ma_node_graph_init(&graphConfig, NULL, &graph);
    TEST_TIMING_CHECK(result == MA_SUCCESS);
    if (result != MA_SUCCESS) {
        return 1;
    }

    result = test_timing__init_source(&graph, g_t2_data, T2_SOURCE_FRAMES, &bufferC, &nodeC);
    TEST_TIMING_CHECK(result == MA_SUCCESS);

    /* First stint: play global frames [0, 200). */
    ma_node_set_state_time(&nodeC, ma_node_state_started, 0);
    ma_node_set_state_time(&nodeC, ma_node_state_stopped, 200);

    result = ma_node_graph_read_pcm_frames(&graph, g_t2_output, T2_BATCH_FRAMES, &framesRead);
    TEST_TIMING_CHECK(result == MA_SUCCESS);
    TEST_TIMING_CHECK(framesRead == T2_BATCH_FRAMES);

    /* The source performed its first 200 frames: the heard values are the source frames 0..199. */
    for (i = 0; i < T2_BATCH_FRAMES; i += 1) {
        TEST_TIMING_CHECK(g_t2_output[i] == (float)i);
    }

    TEST_TIMING_CHECK(ma_node_get_time(&nodeC) == 200);
    TEST_TIMING_CHECK(ma_node_graph_get_time(&graph) == 200);

    /* Schedule the rest and the rejoin: silent over [200, 400), playing again over [400, 600). */
    ma_node_set_state_time(&nodeC, ma_node_state_started, 400);
    ma_node_set_state_time(&nodeC, ma_node_state_stopped, 600);

    /* Rest period: read global frames [200, 400). */
    result = ma_node_graph_read_pcm_frames(&graph, g_t2_output, T2_BATCH_FRAMES, &framesRead);
    TEST_TIMING_CHECK(result == MA_SUCCESS);
    TEST_TIMING_CHECK(framesRead == T2_BATCH_FRAMES);

    /* Nothing but silence is heard while the source rests... */
    TEST_TIMING_CHECK_RANGE(g_t2_output, 0, T2_BATCH_FRAMES, 0.0f);

    /* ...and its local clock must not have advanced, while the global clock kept running. */
    TEST_TIMING_CHECK(ma_node_get_time(&nodeC) == 200);
    TEST_TIMING_CHECK(ma_node_graph_get_time(&graph) == 400);

    /* Rejoin: read global frames [400, 600). */
    localTimeAtRejoin = ma_node_get_time(&nodeC);
    TEST_TIMING_CHECK(localTimeAtRejoin == 200);

    result = ma_node_graph_read_pcm_frames(&graph, g_t2_output, T2_BATCH_FRAMES, &framesRead);
    TEST_TIMING_CHECK(result == MA_SUCCESS);
    TEST_TIMING_CHECK(framesRead == T2_BATCH_FRAMES);

    /*
    The heard content must continue from the source position the local clock was
    frozen at: output frame i is source frame (localTimeAtRejoin + i). The beat
    you hear and the position the source's counter continues from are the same.
    */
    for (i = 0; i < T2_BATCH_FRAMES; i += 1) {
        TEST_TIMING_CHECK(g_t2_output[i] == (float)(localTimeAtRejoin + i));
    }

    /* The local clock only counted performed frames: 200 + 200, not the 600 the graph has played. */
    TEST_TIMING_CHECK(ma_node_get_time(&nodeC) == 400);
    TEST_TIMING_CHECK(ma_node_graph_get_time(&graph) == 600);

    test_timing__uninit_source(&bufferC, &nodeC);
    ma_node_graph_uninit(&graph, NULL);

    return (hasFailed) ? 1 : 0;
}


/*
Test 3: The caller drains the mix in irregular batches.

    source A: constant 1.0,  always playing
    source B: constant 0.25, scheduled to play global frames [150, 450)

The 500-frame mix is read in batches of 37, 211, 96, 113 and 43 frames, chosen
so that batch boundaries do not line up with the scheduled start/stop times.
After every batch the global clock must tell the caller exactly how far the
graph has performed, each source's local clock must reflect only the frames it
actually played, and the final mix must be identical to a frame-exact schedule
independent of the read chunking.
*/
#define T3_TOTAL_FRAMES   500
#define T3_SOURCE_A_FRAMES 500
#define T3_SOURCE_B_FRAMES 300

static float g_t3_dataA[T3_SOURCE_A_FRAMES];
static float g_t3_dataB[T3_SOURCE_B_FRAMES];
static float g_t3_output[T3_TOTAL_FRAMES];

int test_entry__node_graph_timing_batched_reads(int argc, char** argv)
{
    ma_bool32 hasFailed = MA_FALSE;
    ma_result result;
    ma_node_graph_config graphConfig;
    ma_node_graph graph;
    ma_audio_buffer bufferA;
    ma_audio_buffer bufferB;
    ma_data_source_node nodeA;
    ma_data_source_node nodeB;
    static const ma_uint32 batches[] = { 37, 211, 96, 113, 43 };
    ma_uint64 cumulative;
    ma_uint64 expectedTimeB;
    ma_uint32 iBatch;
    ma_uint32 i;

    (void)argc;
    (void)argv;

    for (i = 0; i < T3_SOURCE_A_FRAMES; i += 1) {
        g_t3_dataA[i] = 1.0f;
    }
    for (i = 0; i < T3_SOURCE_B_FRAMES; i += 1) {
        g_t3_dataB[i] = 0.25f;
    }

    graphConfig = ma_node_graph_config_init(1);
    result = ma_node_graph_init(&graphConfig, NULL, &graph);
    TEST_TIMING_CHECK(result == MA_SUCCESS);
    if (result != MA_SUCCESS) {
        return 1;
    }

    result = test_timing__init_source(&graph, g_t3_dataA, T3_SOURCE_A_FRAMES, &bufferA, &nodeA);
    TEST_TIMING_CHECK(result == MA_SUCCESS);
    result = test_timing__init_source(&graph, g_t3_dataB, T3_SOURCE_B_FRAMES, &bufferB, &nodeB);
    TEST_TIMING_CHECK(result == MA_SUCCESS);

    ma_node_set_state_time(&nodeB, ma_node_state_started, 150);
    ma_node_set_state_time(&nodeB, ma_node_state_stopped, 450);

    cumulative = 0;
    for (iBatch = 0; iBatch < sizeof(batches)/sizeof(batches[0]); iBatch += 1) {
        ma_uint64 framesRead = 0;

        result = ma_node_graph_read_pcm_frames(&graph, g_t3_output + cumulative, batches[iBatch], &framesRead);
        TEST_TIMING_CHECK(result == MA_SUCCESS);
        TEST_TIMING_CHECK(framesRead == batches[iBatch]);

        cumulative += batches[iBatch];

        /* After every batch, the global clock tells the caller exactly where the graph has played to. */
        TEST_TIMING_CHECK(ma_node_graph_get_time(&graph) == cumulative);

        /* Source A plays continuously: its local clock tracks the global clock. */
        TEST_TIMING_CHECK(ma_node_get_time(&nodeA) == cumulative);

        /* Source B only counts the frames inside its scheduled [150, 450) window. */
        expectedTimeB = cumulative;
        if (expectedTimeB > 450) {
            expectedTimeB = 450;
        }
        if (expectedTimeB > 150) {
            expectedTimeB -= 150;
        } else {
            expectedTimeB = 0;
        }
        TEST_TIMING_CHECK(ma_node_get_time(&nodeB) == expectedTimeB);
    }

    TEST_TIMING_CHECK(cumulative == T3_TOTAL_FRAMES);
    TEST_TIMING_CHECK(ma_node_get_time(&nodeA) == T3_TOTAL_FRAMES);
    TEST_TIMING_CHECK(ma_node_get_time(&nodeB) == 300);

    /*
    The mix content must be frame-exact regardless of how the caller chunked
    the reads: B's 0.25 is audible exactly over global frames [150, 450).
    */
    TEST_TIMING_CHECK_RANGE(g_t3_output,   0, 150, 1.0f);
    TEST_TIMING_CHECK_RANGE(g_t3_output, 150, 450, 1.25f);
    TEST_TIMING_CHECK_RANGE(g_t3_output, 450, T3_TOTAL_FRAMES, 1.0f);

    test_timing__uninit_source(&bufferA, &nodeA);
    test_timing__uninit_source(&bufferB, &nodeB);
    ma_node_graph_uninit(&graph, NULL);

    return (hasFailed) ? 1 : 0;
}
