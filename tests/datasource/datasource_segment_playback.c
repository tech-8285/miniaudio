/*
Integration tests for a two-segment playback chain built from two PCM recordings.

Two PCM recordings are synthesised on disk and then opened through the public
decoder API. Each recording is exposed through exactly one segment of the
original recording:

    segment A: recording A, frames [400, 1400)
    segment B: recording B, frames [600, 2100), with loop point [200, 600)

The segments are chained A -> B with ma_data_source_set_next() and every read in
these tests is issued against the head of the chain. Both recordings hold a
monotonic ramp (recording A: value == frame index, recording B: value == 10000 +
frame index), so a single delivered sample identifies both the recording and the
absolute frame it was taken from.

Contract under test (see the "Data Source" section of miniaudio.h):

  1. Cursors, lengths, ranges and loop points are all relative to the start of
     the range, so the caller's readings describe positions inside a segment.
  2. A read that straddles the junction delivers the tail of the current segment
     followed, without a gap, by the head of the next one. After such a read,
     ma_data_source_get_current() names the segment that can continue playing
     and the cursor readings show where it continues from.
  3. Once inside segment B the caller can either play through the whole segment
     to its end, or stay inside the segment's declared loop point. The two modes
     are distinguishable by sample order (the frame delivered after the loop end
     is the loop begin when looping, and the next frame of the segment when
     playing through) and by the reported in-segment position.
*/

#define DS_TEST_SAMPLE_RATE         48000
#define DS_TEST_FORMAT              ma_format_s16
#define DS_TEST_CHANNELS            1

#define DS_TEST_REC_A_FRAME_COUNT   3000
#define DS_TEST_REC_B_FRAME_COUNT   2500
#define DS_TEST_REC_A_VALUE_BIAS    0
#define DS_TEST_REC_B_VALUE_BIAS    10000

#define DS_TEST_SEG_A_BEG           400
#define DS_TEST_SEG_A_END           1400
#define DS_TEST_SEG_B_BEG           600
#define DS_TEST_SEG_B_END           2100
#define DS_TEST_SEG_B_LOOP_BEG      200
#define DS_TEST_SEG_B_LOOP_END      600

#define DS_TEST_SEG_A_LEN           (DS_TEST_SEG_A_END - DS_TEST_SEG_A_BEG)                             /* 1000 */
#define DS_TEST_SEG_B_LEN           (DS_TEST_SEG_B_END - DS_TEST_SEG_B_BEG)                             /* 1500 */
#define DS_TEST_SEG_B_LOOP_LEN      (DS_TEST_SEG_B_LOOP_END - DS_TEST_SEG_B_LOOP_BEG)                   /* 400 */

#define DS_TEST_SEG_A_PATH          TEST_OUTPUT_DIR "/datasource_segment_a.wav"
#define DS_TEST_SEG_B_PATH          TEST_OUTPUT_DIR "/datasource_segment_b.wav"

#define DS_TEST_CHECK(condition)                                                        \
    do {                                                                                \
        if (!(condition)) {                                                             \
            printf("    FAILED: %s (line %d)\n", #condition, __LINE__);                 \
            hasFailed = MA_TRUE;                                                        \
        }                                                                               \
    } while (0)


/* Writes a mono s16 WAV whose samples are `valueBias + frameIndex`. */
static ma_result ds_test__write_ramp_recording(const char* pFilePath, ma_int16 valueBias, ma_uint64 frameCount)
{
    ma_result result;
    ma_encoder_config config;
    ma_encoder encoder;
    ma_int16 buffer[256];
    ma_uint64 absoluteFrame = 0;
    ma_uint64 framesRemaining = frameCount;

    config = ma_encoder_config_init(ma_encoding_format_wav, DS_TEST_FORMAT, DS_TEST_CHANNELS, DS_TEST_SAMPLE_RATE);
    result = ma_encoder_init_file(pFilePath, &config, &encoder);
    if (result != MA_SUCCESS) {
        printf("    Failed to open \"%s\" for encoding: %s\n", pFilePath, ma_result_description(result));
        return result;
    }

    while (framesRemaining > 0) {
        ma_uint64 framesToWrite = framesRemaining;
        ma_uint64 iFrame;

        if (framesToWrite > ma_countof(buffer)) {
            framesToWrite = ma_countof(buffer);
        }

        for (iFrame = 0; iFrame < framesToWrite; iFrame += 1) {
            buffer[iFrame] = (ma_int16)(valueBias + absoluteFrame + iFrame);
        }

        result = ma_encoder_write_pcm_frames(&encoder, buffer, framesToWrite, NULL);
        if (result != MA_SUCCESS) {
            printf("    Failed to write \"%s\": %s\n", pFilePath, ma_result_description(result));
            break;
        }

        absoluteFrame  += framesToWrite;
        framesRemaining -= framesToWrite;
    }

    ma_encoder_uninit(&encoder);

    return result;
}

static ma_result ds_test__prepare_recordings(void)
{
    ma_result result;

    result = ds_test__write_ramp_recording(DS_TEST_SEG_A_PATH, DS_TEST_REC_A_VALUE_BIAS, DS_TEST_REC_A_FRAME_COUNT);
    if (result != MA_SUCCESS) {
        return result;
    }

    return ds_test__write_ramp_recording(DS_TEST_SEG_B_PATH, DS_TEST_REC_B_VALUE_BIAS, DS_TEST_REC_B_FRAME_COUNT);
}

/*
Initializes the playback chain: two decoders, each exposing exactly one segment
of its recording, with the loop point declared for segment B, chained A -> B.
The caller must always read through pDecoderA.
*/
static ma_result ds_test__init_chain(ma_decoder* pDecoderA, ma_decoder* pDecoderB)
{
    ma_result result;
    ma_decoder_config config;

    config = ma_decoder_config_init(DS_TEST_FORMAT, DS_TEST_CHANNELS, DS_TEST_SAMPLE_RATE);

    result = ma_decoder_init_file(DS_TEST_SEG_A_PATH, &config, pDecoderA);
    if (result != MA_SUCCESS) {
        printf("    Failed to open \"%s\" for decoding: %s\n", DS_TEST_SEG_A_PATH, ma_result_description(result));
        return result;
    }

    result = ma_decoder_init_file(DS_TEST_SEG_B_PATH, &config, pDecoderB);
    if (result != MA_SUCCESS) {
        printf("    Failed to open \"%s\" for decoding: %s\n", DS_TEST_SEG_B_PATH, ma_result_description(result));
        ma_decoder_uninit(pDecoderA);
        return result;
    }

    /* Each recording is exposed through exactly one segment. Ranges are absolute, in the recording's own frames. */
    result = ma_data_source_set_range_in_pcm_frames(pDecoderA, DS_TEST_SEG_A_BEG, DS_TEST_SEG_A_END);
    if (result != MA_SUCCESS) {
        ma_decoder_uninit(pDecoderB);
        ma_decoder_uninit(pDecoderA);
        return result;
    }

    result = ma_data_source_set_range_in_pcm_frames(pDecoderB, DS_TEST_SEG_B_BEG, DS_TEST_SEG_B_END);
    if (result != MA_SUCCESS) {
        ma_decoder_uninit(pDecoderB);
        ma_decoder_uninit(pDecoderA);
        return result;
    }

    /*
    The loop point is relative to the start of segment B. It must be declared
    after the range because setting a range resets any previously declared loop
    point.
    */
    result = ma_data_source_set_loop_point_in_pcm_frames(pDecoderB, DS_TEST_SEG_B_LOOP_BEG, DS_TEST_SEG_B_LOOP_END);
    if (result != MA_SUCCESS) {
        ma_decoder_uninit(pDecoderB);
        ma_decoder_uninit(pDecoderA);
        return result;
    }

    result = ma_data_source_set_next(pDecoderA, pDecoderB);
    if (result != MA_SUCCESS) {
        ma_decoder_uninit(pDecoderB);
        ma_decoder_uninit(pDecoderA);
        return result;
    }

    return MA_SUCCESS;
}

static void ds_test__uninit_chain(ma_decoder* pDecoderA, ma_decoder* pDecoderB)
{
    ma_decoder_uninit(pDecoderB);
    ma_decoder_uninit(pDecoderA);
}

/*
Checks that every delivered sample equals `valueBias + firstAbsoluteFrame + i`,
which pins the sample stream to one recording and to a contiguous absolute frame
range inside it.
*/
static ma_bool32 ds_test__check_samples(const char* pLabel, const ma_int16* pFrames, ma_uint64 frameCount, ma_int16 valueBias, ma_uint64 firstAbsoluteFrame)
{
    ma_uint64 iFrame;

    for (iFrame = 0; iFrame < frameCount; iFrame += 1) {
        ma_int16 expected = (ma_int16)(valueBias + firstAbsoluteFrame + iFrame);

        if (pFrames[iFrame] != expected) {
            printf("    FAILED: %s: frame %d: expected %d (recording frame %d), got %d\n",
                pLabel,
                (int)iFrame,
                (int)expected,
                (int)(firstAbsoluteFrame + iFrame),
                (int)pFrames[iFrame]);
            return MA_FALSE;
        }
    }

    return MA_TRUE;
}

/* Checks the caller-visible in-segment position of a data source. */
static ma_bool32 ds_test__check_cursor(const char* pLabel, ma_data_source* pDataSource, ma_uint64 expectedCursor)
{
    ma_result result;
    ma_uint64 cursor = 0;

    result = ma_data_source_get_cursor_in_pcm_frames(pDataSource, &cursor);
    if (result != MA_SUCCESS) {
        printf("    FAILED: %s: failed to read the cursor: %s\n", pLabel, ma_result_description(result));
        return MA_FALSE;
    }

    if (cursor != expectedCursor) {
        printf("    FAILED: %s: expected cursor %d, got %d\n", pLabel, (int)expectedCursor, (int)cursor);
        return MA_FALSE;
    }

    return MA_TRUE;
}

/* Checks which segment of the chain the caller would read from next. */
static ma_bool32 ds_test__check_current(const char* pLabel, ma_data_source* pChainHead, ma_data_source* pExpected)
{
    ma_data_source* pCurrent = ma_data_source_get_current(pChainHead);

    if (pCurrent != pExpected) {
        printf("    FAILED: %s: wrong segment is current\n", pLabel);
        return MA_FALSE;
    }

    return MA_TRUE;
}


/*
Test 1: A single read that straddles the junction.

Segment A is positioned 960 frames in, 40 frames before the end of its segment.
A read of 120 frames therefore delivers 40 frames from the tail of segment A
(recording A, absolute frames 1360..1399) followed by 80 frames from the head of
segment B (recording B, absolute frames 600..679).

After the read the caller's readings must show that segment A is spent (its
in-segment cursor sits on the segment length) and that segment B is the segment
that can continue, 80 frames in. The following read must then continue inside
segment B with no gap.
*/
int test_entry__datasource_segment_junction_handover(int argc, char** argv)
{
    ma_result result;
    ma_bool32 hasFailed = MA_FALSE;
    ma_decoder decoderA;
    ma_decoder decoderB;
    ma_int16 frames[256];
    ma_uint64 framesRead = 0;
    ma_uint64 length = 0;
    ma_uint64 rangeBeg = 0;
    ma_uint64 rangeEnd = 0;
    ma_uint64 loopBeg = 0;
    ma_uint64 loopEnd = 0;

    (void)argc;
    (void)argv;

    printf("  Preparing recordings and chaining the two segments...\n");

    result = ds_test__prepare_recordings();
    if (result != MA_SUCCESS) {
        return -1;
    }

    result = ds_test__init_chain(&decoderA, &decoderB);
    if (result != MA_SUCCESS) {
        return -1;
    }

    /* The public readings describe the declared segments, not the recordings. */
    ma_data_source_get_range_in_pcm_frames(&decoderA, &rangeBeg, &rangeEnd);
    DS_TEST_CHECK(rangeBeg == DS_TEST_SEG_A_BEG);
    DS_TEST_CHECK(rangeEnd == DS_TEST_SEG_A_END);
    ma_data_source_get_range_in_pcm_frames(&decoderB, &rangeBeg, &rangeEnd);
    DS_TEST_CHECK(rangeBeg == DS_TEST_SEG_B_BEG);
    DS_TEST_CHECK(rangeEnd == DS_TEST_SEG_B_END);

    /* Lengths are also expressed relative to the segment start. */
    result = ma_data_source_get_length_in_pcm_frames(&decoderA, &length);
    DS_TEST_CHECK(result == MA_SUCCESS);
    DS_TEST_CHECK(length == DS_TEST_SEG_A_LEN);
    result = ma_data_source_get_length_in_pcm_frames(&decoderB, &length);
    DS_TEST_CHECK(result == MA_SUCCESS);
    DS_TEST_CHECK(length == DS_TEST_SEG_B_LEN);

    ma_data_source_get_loop_point_in_pcm_frames(&decoderB, &loopBeg, &loopEnd);
    DS_TEST_CHECK(loopBeg == DS_TEST_SEG_B_LOOP_BEG);
    DS_TEST_CHECK(loopEnd == DS_TEST_SEG_B_LOOP_END);

    /* Position 960 frames inside segment A, then straddle the junction. */
    result = ma_data_source_seek_to_pcm_frame(&decoderA, DS_TEST_SEG_A_LEN - 40);
    DS_TEST_CHECK(result == MA_SUCCESS);
    DS_TEST_CHECK(ds_test__check_cursor("segment A after seek", &decoderA, DS_TEST_SEG_A_LEN - 40));

    printf("  Reading 120 frames across the junction...\n");

    framesRead = 0;
    result = ma_data_source_read_pcm_frames(&decoderA, frames, 120, &framesRead);
    DS_TEST_CHECK(result == MA_SUCCESS);
    DS_TEST_CHECK(framesRead == 120);

    /* Delivered audio: the tail of segment A, then the head of segment B. */
    DS_TEST_CHECK(ds_test__check_samples("tail of segment A", frames, 40, DS_TEST_REC_A_VALUE_BIAS, DS_TEST_SEG_A_END - 40));
    DS_TEST_CHECK(ds_test__check_samples("head of segment B", frames + 40, 80, DS_TEST_REC_B_VALUE_BIAS, DS_TEST_SEG_B_BEG));

    /*
    The readings the caller receives: segment A's cursor sits on its segment
    length (nothing left to play there), segment B's cursor says how far into
    the segment the handover went, and the chain names B as the current segment.
    */
    DS_TEST_CHECK(ds_test__check_cursor("segment A after handover", &decoderA, DS_TEST_SEG_A_LEN));
    DS_TEST_CHECK(ds_test__check_cursor("segment B after handover", &decoderB, 80));
    DS_TEST_CHECK(ds_test__check_current("after handover", &decoderA, (ma_data_source*)&decoderB));

    /* Length minus in-segment cursor is what tells the caller how much more can be played from the current segment. */
    result = ma_data_source_get_length_in_pcm_frames(&decoderB, &length);
    DS_TEST_CHECK(result == MA_SUCCESS);
    DS_TEST_CHECK(ds_test__check_cursor("segment B before measuring the remainder", &decoderB, 80));
    DS_TEST_CHECK(length - 80 == DS_TEST_SEG_B_LEN - 80);

    /* A follow-up read continues inside segment B from where the handover stopped. */
    framesRead = 0;
    result = ma_data_source_read_pcm_frames(&decoderA, frames, 200, &framesRead);
    DS_TEST_CHECK(result == MA_SUCCESS);
    DS_TEST_CHECK(framesRead == 200);
    DS_TEST_CHECK(ds_test__check_samples("continuation inside segment B", frames, 200, DS_TEST_REC_B_VALUE_BIAS, DS_TEST_SEG_B_BEG + 80));
    DS_TEST_CHECK(ds_test__check_cursor("segment B after continuation", &decoderB, 280));
    DS_TEST_CHECK(ds_test__check_current("after continuation", &decoderA, (ma_data_source*)&decoderB));

    ds_test__uninit_chain(&decoderA, &decoderB);

    if (hasFailed) {
        return -1;
    } else {
        return 0;
    }
}


/*
Test 2: Consecutive reads that land exactly on the segment boundary.

The first read drains segment A exactly (1000 frames). The delivered samples
must stop at the last frame of segment A and the readings must still name A
with its cursor on the segment length. The next read starts at frame 0 of
segment B and the readings must switch to B.
*/
int test_entry__datasource_segment_boundary_continuation(int argc, char** argv)
{
    ma_result result;
    ma_bool32 hasFailed = MA_FALSE;
    ma_decoder decoderA;
    ma_decoder decoderB;
    ma_int16 frames[2048];
    ma_uint64 framesRead = 0;

    (void)argc;
    (void)argv;

    result = ds_test__prepare_recordings();
    if (result != MA_SUCCESS) {
        return -1;
    }

    result = ds_test__init_chain(&decoderA, &decoderB);
    if (result != MA_SUCCESS) {
        return -1;
    }

    printf("  Draining segment A exactly...\n");

    framesRead = 0;
    result = ma_data_source_read_pcm_frames(&decoderA, frames, DS_TEST_SEG_A_LEN, &framesRead);
    DS_TEST_CHECK(result == MA_SUCCESS);
    DS_TEST_CHECK(framesRead == DS_TEST_SEG_A_LEN);
    DS_TEST_CHECK(ds_test__check_samples("whole segment A", frames, DS_TEST_SEG_A_LEN, DS_TEST_REC_A_VALUE_BIAS, DS_TEST_SEG_A_BEG));
    DS_TEST_CHECK(ds_test__check_cursor("segment A drained", &decoderA, DS_TEST_SEG_A_LEN));
    DS_TEST_CHECK(ds_test__check_current("still on segment A", &decoderA, (ma_data_source*)&decoderA));

    printf("  Continuing into segment B...\n");

    framesRead = 0;
    result = ma_data_source_read_pcm_frames(&decoderA, frames, 16, &framesRead);
    DS_TEST_CHECK(result == MA_SUCCESS);
    DS_TEST_CHECK(framesRead == 16);
    DS_TEST_CHECK(ds_test__check_samples("start of segment B", frames, 16, DS_TEST_REC_B_VALUE_BIAS, DS_TEST_SEG_B_BEG));
    DS_TEST_CHECK(ds_test__check_cursor("segment B after first frames", &decoderB, 16));
    DS_TEST_CHECK(ds_test__check_current("segment B is current", &decoderA, (ma_data_source*)&decoderB));

    ds_test__uninit_chain(&decoderA, &decoderB);

    if (hasFailed) {
        return -1;
    } else {
        return 0;
    }
}


/*
Test 3: Play through segment B to its end.

After the handover into segment B, looping is disabled (the default). Reads must
walk segment B in order from the first frame to the last frame of the segment:
the declared loop point [200, 600) must not be honoured. The in-segment cursor
must advance monotonically to the segment length, after which the chain reports
MA_AT_END. In particular, the frame delivered after in-segment position 600 is
absolute frame 1200 (the next frame of the segment), not the loop begin.
*/
int test_entry__datasource_segment_playthrough(int argc, char** argv)
{
    ma_result result;
    ma_bool32 hasFailed = MA_FALSE;
    ma_decoder decoderA;
    ma_decoder decoderB;
    ma_int16 frames[2048];
    ma_uint64 framesRead = 0;
    ma_uint64 iFrame;

    (void)argc;
    (void)argv;

    result = ds_test__prepare_recordings();
    if (result != MA_SUCCESS) {
        return -1;
    }

    result = ds_test__init_chain(&decoderA, &decoderB);
    if (result != MA_SUCCESS) {
        return -1;
    }

    DS_TEST_CHECK(ma_data_source_is_looping(&decoderA) == MA_FALSE);

    /* Enter segment B: 60 frames from the tail of A, then 40 frames from the head of B. */
    result = ma_data_source_seek_to_pcm_frame(&decoderA, DS_TEST_SEG_A_LEN - 60);
    DS_TEST_CHECK(result == MA_SUCCESS);

    framesRead = 0;
    result = ma_data_source_read_pcm_frames(&decoderA, frames, 100, &framesRead);
    DS_TEST_CHECK(result == MA_SUCCESS);
    DS_TEST_CHECK(framesRead == 100);
    DS_TEST_CHECK(ds_test__check_samples("tail of segment A", frames, 60, DS_TEST_REC_A_VALUE_BIAS, DS_TEST_SEG_A_END - 60));
    DS_TEST_CHECK(ds_test__check_samples("head of segment B", frames + 60, 40, DS_TEST_REC_B_VALUE_BIAS, DS_TEST_SEG_B_BEG));
    DS_TEST_CHECK(ds_test__check_cursor("segment B after handover", &decoderB, 40));

    printf("  Playing through the loop point and on to the end of segment B...\n");

    /*
    600 frames from position 40 deliver in-segment positions 40..639 without
    interruption. In particular the sample delivered at in-segment position 600
    is recording frame 1200 - the next frame of the segment - not the loop begin
    (recording frame 800), because the loop point is only honoured while looping.
    */
    framesRead = 0;
    result = ma_data_source_read_pcm_frames(&decoderA, frames, 600, &framesRead);
    DS_TEST_CHECK(result == MA_SUCCESS);
    DS_TEST_CHECK(framesRead == 600);
    DS_TEST_CHECK(ds_test__check_samples("segment B across the loop point", frames, 600, DS_TEST_REC_B_VALUE_BIAS, DS_TEST_SEG_B_BEG + 40));
    DS_TEST_CHECK(ds_test__check_cursor("segment B past the loop point", &decoderB, 640));

    /* Note the contrast with the loop test: the sample at in-segment position 600 is 11200 here. */
    DS_TEST_CHECK(frames[DS_TEST_SEG_B_LOOP_END - 40] == (ma_int16)(DS_TEST_REC_B_VALUE_BIAS + DS_TEST_SEG_B_BEG + DS_TEST_SEG_B_LOOP_END));

    /* Drain the rest of the segment in one read. */
    framesRead = 0;
    result = ma_data_source_read_pcm_frames(&decoderA, frames, DS_TEST_SEG_B_LEN, &framesRead);
    DS_TEST_CHECK(result == MA_SUCCESS);
    DS_TEST_CHECK(framesRead == DS_TEST_SEG_B_LEN - 640);
    DS_TEST_CHECK(ds_test__check_samples("rest of segment B", frames, framesRead, DS_TEST_REC_B_VALUE_BIAS, DS_TEST_SEG_B_BEG + 640));

    /* Every frame of the segment was delivered exactly once, in order. */
    for (iFrame = 1; iFrame < framesRead; iFrame += 1) {
        if (frames[iFrame] != (ma_int16)(frames[iFrame - 1] + 1)) {
            printf("    FAILED: rest of segment B is not contiguous at frame %d\n", (int)iFrame);
            hasFailed = MA_TRUE;
            break;
        }
    }

    DS_TEST_CHECK(ds_test__check_cursor("segment B drained", &decoderB, DS_TEST_SEG_B_LEN));
    DS_TEST_CHECK(ds_test__check_current("end of chain is segment B", &decoderA, (ma_data_source*)&decoderB));

    printf("  Reading past the end of the chain...\n");

    framesRead = 0;
    result = ma_data_source_read_pcm_frames(&decoderA, frames, 16, &framesRead);
    DS_TEST_CHECK(result == MA_AT_END);
    DS_TEST_CHECK(framesRead == 0);
    DS_TEST_CHECK(ds_test__check_cursor("segment B still on its length", &decoderB, DS_TEST_SEG_B_LEN));

    ds_test__uninit_chain(&decoderA, &decoderB);

    if (hasFailed) {
        return -1;
    } else {
        return 0;
    }
}


/*
Test 4: Stay inside segment B's declared loop point.

Two arrangements are exercised from the same starting position (40 frames into
segment B):

  a. Looping enabled on segment B and on the head of the chain: reads are
     clamped to the declared loop end and rewind to the declared loop begin, so
     the caller never leaves the segment. Delivered samples follow the order
     segment B [40, 600) then [200, 600) then [200, 300) again, and the
     in-segment cursor always stays within [200, 600). Further reads keep
     walking that same window rather than ending the chain.

  b. Looping enabled on segment B alone: reads are still clamped to the
     declared loop end (the loop point is honoured) but the chain reports its
     end instead of re-entering the loop, because the chain head is not looping.

The key distinction from the play-through test: after in-segment position 600
this mode delivers the loop begin (absolute frame 800), not the next frame of
the segment (absolute frame 1200).
*/
int test_entry__datasource_segment_loop(int argc, char** argv)
{
    ma_result result;
    ma_bool32 hasFailed = MA_FALSE;
    ma_decoder decoderA;
    ma_decoder decoderB;
    ma_int16 frames[2048];
    ma_uint64 framesRead = 0;
    ma_uint32 iStep = 0;

    (void)argc;
    (void)argv;

    result = ds_test__prepare_recordings();
    if (result != MA_SUCCESS) {
        return -1;
    }

    result = ds_test__init_chain(&decoderA, &decoderB);
    if (result != MA_SUCCESS) {
        return -1;
    }

    /* Enter segment B so that the loop is evaluated from inside the second segment. */
    result = ma_data_source_seek_to_pcm_frame(&decoderA, DS_TEST_SEG_A_LEN - 60);
    DS_TEST_CHECK(result == MA_SUCCESS);

    framesRead = 0;
    result = ma_data_source_read_pcm_frames(&decoderA, frames, 100, &framesRead);
    DS_TEST_CHECK(result == MA_SUCCESS);
    DS_TEST_CHECK(framesRead == 100);
    DS_TEST_CHECK(ds_test__check_samples("tail of segment A", frames, 60, DS_TEST_REC_A_VALUE_BIAS, DS_TEST_SEG_A_END - 60));
    DS_TEST_CHECK(ds_test__check_samples("head of segment B", frames + 60, 40, DS_TEST_REC_B_VALUE_BIAS, DS_TEST_SEG_B_BEG));
    DS_TEST_CHECK(ds_test__check_cursor("segment B after handover", &decoderB, 40));

    /*
    Looping mode: segment B's flag makes each read stop at the declared loop
    end, and the chain head's flag keeps the chain from ending when the current
    segment reports its end, rewinding it to that segment's loop begin.
    */
    result = ma_data_source_set_looping(&decoderB, MA_TRUE);
    DS_TEST_CHECK(result == MA_SUCCESS);
    result = ma_data_source_set_looping(&decoderA, MA_TRUE);
    DS_TEST_CHECK(result == MA_SUCCESS);

    printf("  Looping: reading up to the loop end...\n");

    framesRead = 0;
    result = ma_data_source_read_pcm_frames(&decoderA, frames, 560, &framesRead);
    DS_TEST_CHECK(result == MA_SUCCESS);
    DS_TEST_CHECK(framesRead == 560);
    DS_TEST_CHECK(ds_test__check_samples("segment B up to the loop end", frames, 560, DS_TEST_REC_B_VALUE_BIAS, DS_TEST_SEG_B_BEG + 40));
    DS_TEST_CHECK(ds_test__check_cursor("segment B on the loop end", &decoderB, DS_TEST_SEG_B_LOOP_END));
    DS_TEST_CHECK(ds_test__check_current("loop stays on segment B", &decoderA, (ma_data_source*)&decoderB));

    printf("  Looping: crossing the loop end rewinds to the loop begin...\n");

    /* This read starts exactly on the loop end; it must wrap to the loop begin. */
    framesRead = 0;
    result = ma_data_source_read_pcm_frames(&decoderA, frames, 200, &framesRead);
    DS_TEST_CHECK(result == MA_SUCCESS);
    DS_TEST_CHECK(framesRead == 200);
    DS_TEST_CHECK(ds_test__check_samples("rewound to the loop begin", frames, 200, DS_TEST_REC_B_VALUE_BIAS, DS_TEST_SEG_B_BEG + DS_TEST_SEG_B_LOOP_BEG));
    DS_TEST_CHECK(ds_test__check_cursor("segment B inside the loop", &decoderB, DS_TEST_SEG_B_LOOP_BEG + 200));

    /* Contrast with play-through: this sample would be 11200 there. */
    DS_TEST_CHECK(frames[0] == (ma_int16)(DS_TEST_REC_B_VALUE_BIAS + DS_TEST_SEG_B_BEG + DS_TEST_SEG_B_LOOP_BEG));

    /* Cross the loop end again in the middle of a read. */
    printf("  Looping: wrapping again mid-read...\n");

    framesRead = 0;
    result = ma_data_source_read_pcm_frames(&decoderA, frames, 300, &framesRead);
    DS_TEST_CHECK(result == MA_SUCCESS);
    DS_TEST_CHECK(framesRead == 300);
    DS_TEST_CHECK(ds_test__check_samples("rest of the loop pass", frames, 200, DS_TEST_REC_B_VALUE_BIAS, DS_TEST_SEG_B_BEG + DS_TEST_SEG_B_LOOP_BEG + 200));
    DS_TEST_CHECK(ds_test__check_samples("second loop pass", frames + 200, 100, DS_TEST_REC_B_VALUE_BIAS, DS_TEST_SEG_B_BEG + DS_TEST_SEG_B_LOOP_BEG));
    DS_TEST_CHECK(ds_test__check_cursor("segment B back inside the loop", &decoderB, DS_TEST_SEG_B_LOOP_BEG + 100));

    /* The loop never ends: further reads keep delivering samples and never report MA_AT_END. */
    printf("  Looping: staying inside the loop across further reads...\n");

    for (iStep = 0; iStep < 8; iStep += 1) {
        ma_uint64 expectedPosition = (DS_TEST_SEG_B_LOOP_BEG + 100) + (iStep * 50);

        /* Wrap the expected in-segment position around the loop. */
        while (expectedPosition >= DS_TEST_SEG_B_LOOP_END) {
            expectedPosition -= DS_TEST_SEG_B_LOOP_LEN;
        }

        framesRead = 0;
        result = ma_data_source_read_pcm_frames(&decoderA, frames, 50, &framesRead);
        DS_TEST_CHECK(result == MA_SUCCESS);
        DS_TEST_CHECK(framesRead == 50);
        DS_TEST_CHECK(ds_test__check_samples("sliding loop pass", frames, 50, DS_TEST_REC_B_VALUE_BIAS, DS_TEST_SEG_B_BEG + expectedPosition));
        DS_TEST_CHECK(ds_test__check_cursor("segment B sliding in the loop", &decoderB, expectedPosition + 50));
        DS_TEST_CHECK(ds_test__check_current("still looping on segment B", &decoderA, (ma_data_source*)&decoderB));
    }

    ds_test__uninit_chain(&decoderA, &decoderB);

    /* Arrangement b: the segment's loop flag alone clamps delivery at the loop end. */
    printf("  Loop point with the chain head not looping...\n");

    result = ds_test__init_chain(&decoderA, &decoderB);
    if (result != MA_SUCCESS) {
        return -1;
    }

    result = ma_data_source_seek_to_pcm_frame(&decoderA, DS_TEST_SEG_A_LEN - 60);
    DS_TEST_CHECK(result == MA_SUCCESS);

    framesRead = 0;
    result = ma_data_source_read_pcm_frames(&decoderA, frames, 100, &framesRead);
    DS_TEST_CHECK(result == MA_SUCCESS);
    DS_TEST_CHECK(framesRead == 100);
    DS_TEST_CHECK(ds_test__check_cursor("segment B after handover", &decoderB, 40));

    result = ma_data_source_set_looping(&decoderB, MA_TRUE);
    DS_TEST_CHECK(result == MA_SUCCESS);

    /* Only 560 frames are available before the declared loop end, even though the segment has more. */
    framesRead = 0;
    result = ma_data_source_read_pcm_frames(&decoderA, frames, 1000, &framesRead);
    DS_TEST_CHECK(result == MA_SUCCESS);
    DS_TEST_CHECK(framesRead == DS_TEST_SEG_B_LOOP_END - 40);
    DS_TEST_CHECK(ds_test__check_samples("clamped at the loop end", frames, framesRead, DS_TEST_REC_B_VALUE_BIAS, DS_TEST_SEG_B_BEG + 40));
    DS_TEST_CHECK(ds_test__check_cursor("segment B on the loop end", &decoderB, DS_TEST_SEG_B_LOOP_END));

    framesRead = 0;
    result = ma_data_source_read_pcm_frames(&decoderA, frames, 16, &framesRead);
    DS_TEST_CHECK(result == MA_AT_END);
    DS_TEST_CHECK(framesRead == 0);

    ds_test__uninit_chain(&decoderA, &decoderB);

    if (hasFailed) {
        return -1;
    } else {
        return 0;
    }
}
