
ma_bool32 test_delay__is_near(float actual, float expected)
{
    float tolerance = 0.0001f;

    if (actual > expected) {
        return (actual - expected) <= tolerance;
    } else {
        return (expected - actual) <= tolerance;
    }
}

/*
Basic cross feedback routing. A pulse fed into the left channel should have its first echo split
between the left and right channels according to the cross feedback ratio. The dry input is never
crossed so the original sound keeps its position.
*/
ma_result test_delay__cross_feedback_basic(float crossFeedback, float expectedLeft, float expectedRight)
{
    ma_result result;
    ma_delay_config delayConfig;
    ma_delay delay;
    const ma_uint32 channels = 2;
    const ma_uint32 delayInFrames = 4;
    const float decay = 0.5f;
    float framesIn[2];
    float framesOut[2];
    float echoLeft = 0;
    float echoRight = 0;
    ma_uint32 iFrame;

    printf("    cross feedback %.2f\n", crossFeedback);

    delayConfig = ma_delay_config_init(channels, 48000, delayInFrames, decay);
    if (delayConfig.crossFeedback != 0) {
        return MA_ERROR;
    }

    delayConfig.crossFeedback = crossFeedback;
    result = ma_delay_init(&delayConfig, NULL, &delay);
    if (result != MA_SUCCESS) {
        return result;
    }

    if (ma_delay_get_cross_feedback(&delay) != crossFeedback) {
        ma_delay_uninit(&delay, NULL);
        return MA_ERROR;
    }

    /* Feed a single left-channel pulse, then silence. wet = dry = 1 (the defaults). */
    for (iFrame = 0; iFrame < delayInFrames * 2; iFrame += 1) {
        if (iFrame == 0) {
            framesIn[0] = 1.0f;
            framesIn[1] = 0.0f;
        } else {
            framesIn[0] = 0.0f;
            framesIn[1] = 0.0f;
        }

        result = ma_delay_process_pcm_frames(&delay, framesOut, framesIn, 1);
        if (result != MA_SUCCESS) {
            ma_delay_uninit(&delay, NULL);
            return result;
        }

        if (iFrame == delayInFrames) {
            echoLeft  = framesOut[0];  /* First echo. */
            echoRight = framesOut[1];
        }
    }

    /*
    The first echo has a total level of decay, with (1 - crossFeedback) retained on the left and
    crossFeedback routed to the right.
    */
    if (!test_delay__is_near(echoLeft, expectedLeft) || !test_delay__is_near(echoRight, expectedRight)) {
        printf("        FAIL: expected (%.4f, %.4f), got (%.4f, %.4f)\n", expectedLeft, expectedRight, echoLeft, echoRight);
        ma_delay_uninit(&delay, NULL);
        return MA_ERROR;
    }

    ma_delay_uninit(&delay, NULL);
    return MA_SUCCESS;
}

/*
The total attenuation of each feedback sample must be determined entirely by decay. With full cross
feedback the whole echo moves to the opposite channel, but its level is unchanged.
*/
ma_result test_delay__cross_feedback_full(void)
{
    return test_delay__cross_feedback_basic(1.0f, 0.0f, 0.5f);
}

ma_result test_delay__cross_feedback_partial(void)
{
    /* decay * (1 - 0.25) = 0.375 stays left, decay * 0.25 = 0.125 crosses to the right. */
    return test_delay__cross_feedback_basic(0.25f, 0.375f, 0.125f);
}

/*
Changing the ratio while a tail is already circulating in the delay line must not move the buffered
samples itself. In echo (immediate start) mode the next feedback write already uses the new ratio,
so the echo bounces to the opposite channel from the first echo onward.
*/
ma_result test_delay__cross_feedback_runtime_change_echo(void)
{
    ma_result result;
    ma_delay_config delayConfig;
    ma_delay delay;
    const ma_uint32 delayInFrames = 4;
    float framesIn[2];
    float framesOut[2];
    float firstEchoL = 0;
    float firstEchoR = 0;
    float secondEchoL = 0;
    float secondEchoR = 0;
    ma_uint32 iFrame;

    printf("    runtime change (echo mode)\n");

    delayConfig = ma_delay_config_init(2, 48000, delayInFrames, 0.5f);
    delayConfig.crossFeedback = 0;  /* Record the pulse with no cross feedback. */
    result = ma_delay_init(&delayConfig, NULL, &delay);
    if (result != MA_SUCCESS) {
        return result;
    }

    for (iFrame = 0; iFrame < delayInFrames; iFrame += 1) {
        framesIn[0] = (iFrame == 0) ? 1.0f : 0.0f;
        framesIn[1] = 0.0f;

        result = ma_delay_process_pcm_frames(&delay, framesOut, framesIn, 1);
        if (result != MA_SUCCESS) {
            ma_delay_uninit(&delay, NULL);
            return result;
        }
    }

    /* Switch to full cross feedback before the first echo is generated. */
    ma_delay_set_cross_feedback(&delay, 1.0f);
    if (ma_delay_get_cross_feedback(&delay) != 1.0f) {
        ma_delay_uninit(&delay, NULL);
        return MA_ERROR;
    }

    for (iFrame = delayInFrames; iFrame < delayInFrames * 3; iFrame += 1) {
        framesIn[0] = 0.0f;
        framesIn[1] = 0.0f;

        result = ma_delay_process_pcm_frames(&delay, framesOut, framesIn, 1);
        if (result != MA_SUCCESS) {
            ma_delay_uninit(&delay, NULL);
            return result;
        }

        if (iFrame == delayInFrames) {
            firstEchoL  = framesOut[0];
            firstEchoR  = framesOut[1];
        } else if (iFrame == delayInFrames * 2) {
            secondEchoL = framesOut[0];
            secondEchoR = framesOut[1];
        }
    }

    /* First feedback after the change crosses the whole echo to the right. */
    if (!test_delay__is_near(firstEchoL, 0.0f) || !test_delay__is_near(firstEchoR, 0.5f)) {
        printf("        FAIL: first echo expected (0.0000, 0.5000), got (%.4f, %.4f)\n", firstEchoL, firstEchoR);
        ma_delay_uninit(&delay, NULL);
        return MA_ERROR;
    }

    /* The next feedback bounces the tail back to the left, losing another decay factor. */
    if (!test_delay__is_near(secondEchoL, 0.25f) || !test_delay__is_near(secondEchoR, 0.0f)) {
        printf("        FAIL: second echo expected (0.2500, 0.0000), got (%.4f, %.4f)\n", secondEchoL, secondEchoR);
        ma_delay_uninit(&delay, NULL);
        return MA_ERROR;
    }

    ma_delay_uninit(&delay, NULL);
    return MA_SUCCESS;
}

/*
In delayed start mode the buffered sample is read out before it is fed back, so a recording made
under the old configuration is played back on its original channel. Only the feedback written after
the ratio change uses the new ratio.
*/
ma_result test_delay__cross_feedback_runtime_change_delayed_start(void)
{
    ma_result result;
    ma_delay_config delayConfig;
    ma_delay delay;
    const ma_uint32 delayInFrames = 4;
    float framesIn[2];
    float framesOut[2];
    float firstReadL = 0;
    float firstReadR = 0;
    float secondReadL = 0;
    float secondReadR = 0;
    ma_uint32 iFrame;

    printf("    runtime change (delayed start)\n");

    delayConfig = ma_delay_config_init(2, 48000, delayInFrames, 0.5f);
    delayConfig.delayStart    = MA_TRUE;
    delayConfig.crossFeedback = 0;  /* Record the pulse with no cross feedback. */
    result = ma_delay_init(&delayConfig, NULL, &delay);
    if (result != MA_SUCCESS) {
        return result;
    }

    for (iFrame = 0; iFrame < delayInFrames; iFrame += 1) {
        framesIn[0] = (iFrame == 0) ? 1.0f : 0.0f;
        framesIn[1] = 0.0f;

        result = ma_delay_process_pcm_frames(&delay, framesOut, framesIn, 1);
        if (result != MA_SUCCESS) {
            ma_delay_uninit(&delay, NULL);
            return result;
        }
    }

    /* Switch to full cross feedback before the buffered recording is read back. */
    ma_delay_set_cross_feedback(&delay, 1.0f);

    for (iFrame = delayInFrames; iFrame < delayInFrames * 4; iFrame += 1) {
        framesIn[0] = 0.0f;
        framesIn[1] = 0.0f;

        result = ma_delay_process_pcm_frames(&delay, framesOut, framesIn, 1);
        if (result != MA_SUCCESS) {
            ma_delay_uninit(&delay, NULL);
            return result;
        }

        if (iFrame == delayInFrames) {
            firstReadL  = framesOut[0];
            firstReadR  = framesOut[1];
        } else if (iFrame == delayInFrames * 2) {
            secondReadL = framesOut[0];
            secondReadR = framesOut[1];
        }
    }

    /* The old recording still plays back on its original (left) channel. */
    if (!test_delay__is_near(firstReadL, 1.0f) || !test_delay__is_near(firstReadR, 0.0f)) {
        printf("        FAIL: old recording expected (1.0000, 0.0000), got (%.4f, %.4f)\n", firstReadL, firstReadR);
        ma_delay_uninit(&delay, NULL);
        return MA_ERROR;
    }

    /* Its feedback was written with the new ratio, so one delay line later it reads back on the right. */
    if (!test_delay__is_near(secondReadL, 0.0f) || !test_delay__is_near(secondReadR, 0.5f)) {
        printf("        FAIL: crossed feedback expected (0.0000, 0.5000), got (%.4f, %.4f)\n", secondReadL, secondReadR);
        ma_delay_uninit(&delay, NULL);
        return MA_ERROR;
    }

    ma_delay_uninit(&delay, NULL);
    return MA_SUCCESS;
}

ma_result test_delay__cross_feedback_validation(void)
{
    ma_result result;
    ma_delay_config delayConfig;
    ma_delay delay;

    printf("    validation\n");

    delayConfig = ma_delay_config_init(2, 48000, 4, 0.5f);
    delayConfig.crossFeedback = 1.5f;
    result = ma_delay_init(&delayConfig, NULL, &delay);
    if (result == MA_SUCCESS) {
        ma_delay_uninit(&delay, NULL);
        return MA_ERROR;  /* Should have been rejected. */
    }

    delayConfig.crossFeedback = -0.1f;
    result = ma_delay_init(&delayConfig, NULL, &delay);
    if (result == MA_SUCCESS) {
        ma_delay_uninit(&delay, NULL);
        return MA_ERROR;  /* Should have been rejected. */
    }

    /* Out of range values are ignored by the setter. */
    delayConfig.crossFeedback = 0;
    result = ma_delay_init(&delayConfig, NULL, &delay);
    if (result != MA_SUCCESS) {
        return result;
    }

    ma_delay_set_cross_feedback(&delay, -1.0f);
    if (ma_delay_get_cross_feedback(&delay) != 0.0f) {
        ma_delay_uninit(&delay, NULL);
        return MA_ERROR;
    }

    ma_delay_set_cross_feedback(&delay, 2.0f);
    if (ma_delay_get_cross_feedback(&delay) != 0.0f) {
        ma_delay_uninit(&delay, NULL);
        return MA_ERROR;
    }

    ma_delay_set_cross_feedback(&delay, 0.5f);
    if (ma_delay_get_cross_feedback(&delay) != 0.5f) {
        ma_delay_uninit(&delay, NULL);
        return MA_ERROR;
    }

    ma_delay_uninit(&delay, NULL);
    return MA_SUCCESS;
}

/* Cross feedback is stereo only. Mono processing must keep the ordinary per-channel feedback path. */
ma_result test_delay__cross_feedback_ignored_for_mono(void)
{
    ma_result result;
    ma_delay_config delayConfig;
    ma_delay delay;
    const ma_uint32 delayInFrames = 4;
    float framesIn[1];
    float framesOut[1];
    ma_uint32 iFrame;

    printf("    mono ignored\n");

    delayConfig = ma_delay_config_init(1, 48000, delayInFrames, 0.5f);
    delayConfig.crossFeedback = 0.75f;
    result = ma_delay_init(&delayConfig, NULL, &delay);
    if (result != MA_SUCCESS) {
        return result;
    }

    for (iFrame = 0; iFrame <= delayInFrames; iFrame += 1) {
        framesIn[0] = (iFrame == 0) ? 1.0f : 0.0f;

        result = ma_delay_process_pcm_frames(&delay, framesOut, framesIn, 1);
        if (result != MA_SUCCESS) {
            ma_delay_uninit(&delay, NULL);
            return result;
        }
    }

    /* Ordinary mono echo: decay applied, no channel to cross into. */
    if (!test_delay__is_near(framesOut[0], 0.5f)) {
        printf("        FAIL: expected 0.5000, got %.4f\n", framesOut[0]);
        ma_delay_uninit(&delay, NULL);
        return MA_ERROR;
    }

    ma_delay_uninit(&delay, NULL);
    return MA_SUCCESS;
}

/* The node wrapper must expose the same setting and pass it through to the underlying delay. */
ma_result test_delay__node_cross_feedback(void)
{
#if !defined(MA_NO_NODE_GRAPH)
    ma_result result;
    ma_node_graph_config nodeGraphConfig;
    ma_node_graph nodeGraph;
    ma_delay_node_config delayNodeConfig;
    ma_delay_node delayNode;

    printf("    node passthrough\n");

    nodeGraphConfig = ma_node_graph_config_init(2);
    result = ma_node_graph_init(&nodeGraphConfig, NULL, &nodeGraph);
    if (result != MA_SUCCESS) {
        return result;
    }

    delayNodeConfig = ma_delay_node_config_init(2, 48000, 4, 0.5f);
    delayNodeConfig.delay.crossFeedback = 0.25f;
    result = ma_delay_node_init(&nodeGraph, &delayNodeConfig, NULL, &delayNode);
    if (result != MA_SUCCESS) {
        ma_node_graph_uninit(&nodeGraph, NULL);
        return result;
    }

    if (ma_delay_node_get_cross_feedback(&delayNode) != 0.25f) {
        ma_delay_node_uninit(&delayNode, NULL);
        ma_node_graph_uninit(&nodeGraph, NULL);
        return MA_ERROR;
    }

    ma_delay_node_set_cross_feedback(&delayNode, 0.75f);
    if (ma_delay_node_get_cross_feedback(&delayNode) != 0.75f) {
        ma_delay_node_uninit(&delayNode, NULL);
        ma_node_graph_uninit(&nodeGraph, NULL);
        return MA_ERROR;
    }

    if (ma_delay_get_cross_feedback(&delayNode.delay) != 0.75f) {
        ma_delay_node_uninit(&delayNode, NULL);
        ma_node_graph_uninit(&nodeGraph, NULL);
        return MA_ERROR;
    }

    ma_delay_node_uninit(&delayNode, NULL);
    ma_node_graph_uninit(&nodeGraph, NULL);
    return MA_SUCCESS;
#else
    return MA_SUCCESS;
#endif
}

int test_entry__delay(int argc, char** argv)
{
    ma_result result;
    ma_bool32 hasError = MA_FALSE;

    (void)argc;
    (void)argv;

    result = test_delay__cross_feedback_full();
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = test_delay__cross_feedback_partial();
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = test_delay__cross_feedback_runtime_change_echo();
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = test_delay__cross_feedback_runtime_change_delayed_start();
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = test_delay__cross_feedback_validation();
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = test_delay__cross_feedback_ignored_for_mono();
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = test_delay__node_cross_feedback();
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    if (hasError) {
        return -1;
    } else {
        return 0;
    }
}
