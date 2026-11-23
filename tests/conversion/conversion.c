#define MA_NO_THREADING
#define MA_NO_DEVICE_IO
#include "../common/common.c"

ma_result init_data_converter(ma_uint32 rateIn, ma_uint32 rateOut, ma_resample_algorithm algorithm, ma_data_converter* pDataConverter)
{
    ma_result result;
    ma_data_converter_config config;

    config = ma_data_converter_config_init(ma_format_s16, ma_format_s16, 1, 1, rateIn, rateOut);
    config.resampling.algorithm = algorithm;

    result = ma_data_converter_init(&config, NULL, pDataConverter);
    if (result != MA_SUCCESS) {
        return result;
    }

    return MA_SUCCESS;
}

#if 0
ma_result test_data_converter__passthrough()
{
    /*
    Notes:
      - The isPassthrough flag should be set to true. Both the positive and negative cases need to be tested.
      - ma_data_converter_set_rate() should fail with MA_INVALID_OPERATION.
      - The output should be identical to the input.
    */
    printf("Passthrough\n");

    

    return MA_SUCCESS;
}
#endif

ma_result test_data_converter__resampling_expected_output_fixed_interval(ma_data_converter* pDataConverter, ma_uint64 frameCountPerIteration)
{
    ma_result result = MA_SUCCESS;
    ma_int16 input[4096];
    ma_int16 i;

    MA_ASSERT(frameCountPerIteration < ma_countof(input));

    /* Fill the input buffer with sequential numbers so we can get an idea on the state of things. Useful for inspecting the linear backend in particular. */
    for (i = 0; i < (ma_int16)ma_countof(input); i += 1) {
        input[i] = i;
    }

    for (i = 0; i < (ma_int16)ma_countof(input); i += (ma_int16)frameCountPerIteration) {
        ma_int16 output[4096];
        ma_uint64 outputFrameCount;
        ma_uint64 inputFrameCount;
        ma_uint64 expectedOutputFrameCount;

        /* We retrieve the required number of input frames for the specified number of output frames, and then compare with what we actually get when reading. */
        ma_data_converter_get_expected_output_frame_count(pDataConverter, frameCountPerIteration, &expectedOutputFrameCount);

        outputFrameCount = ma_countof(output);
        inputFrameCount = frameCountPerIteration;
        result = ma_data_converter_process_pcm_frames(pDataConverter, input, &inputFrameCount, output, &outputFrameCount);
        if (result != MA_SUCCESS) {
            printf("Failed to process frames.");
            break;
        }

        if (outputFrameCount != expectedOutputFrameCount) {
            printf("ERROR: Predicted vs actual output count mismatch: predicted=%d, actual=%d\n", (int)expectedOutputFrameCount, (int)outputFrameCount);
            result = MA_ERROR;
        }
    }

    if (result != MA_SUCCESS) {
        printf("FAILED\n");
    } else {
        printf("PASSED\n");
    }

    return result;
}

ma_result test_data_converter__resampling_expected_output_by_algorithm_and_rate_fixed_interval(ma_resample_algorithm algorithm, ma_uint32 rateIn, ma_uint32 rateOut, ma_uint64 frameCountPerIteration)
{
    ma_result result;
    ma_bool32 hasError = MA_FALSE;
    ma_data_converter converter;

    result = init_data_converter(rateIn, rateOut, algorithm, &converter);
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = test_data_converter__resampling_expected_output_fixed_interval(&converter, frameCountPerIteration);

    ma_data_converter_uninit(&converter, NULL);

    if (hasError) {
        return MA_ERROR;
    } else {
        return MA_SUCCESS;
    }
}

ma_result test_data_converter__resampling_expected_output_by_algorithm_fixed_interval(ma_resample_algorithm algorithm, ma_uint64 frameCountPerIteration)
{
    ma_result result;
    ma_bool32 hasError = MA_FALSE;

    result = test_data_converter__resampling_expected_output_by_algorithm_and_rate_fixed_interval(algorithm, 44100, 48000, frameCountPerIteration);
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = test_data_converter__resampling_expected_output_by_algorithm_and_rate_fixed_interval(algorithm, 48000, 44100, frameCountPerIteration);
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    
    result = test_data_converter__resampling_expected_output_by_algorithm_and_rate_fixed_interval(algorithm, 44100, 192000, frameCountPerIteration);
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = test_data_converter__resampling_expected_output_by_algorithm_and_rate_fixed_interval(algorithm, 192000, 44100, frameCountPerIteration);
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    if (hasError) {
        return MA_ERROR;
    } else {
        return MA_SUCCESS;
    }
}

ma_result test_data_converter__resampling_expected_output_by_algorithm(ma_resample_algorithm algorithm)
{
    ma_result result;
    ma_bool32 hasError = MA_FALSE;

    result = test_data_converter__resampling_expected_output_by_algorithm_fixed_interval(algorithm, 1);
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = test_data_converter__resampling_expected_output_by_algorithm_fixed_interval(algorithm, 16);
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = test_data_converter__resampling_expected_output_by_algorithm_fixed_interval(algorithm, 127);
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }
    
    if (hasError) {
        return MA_ERROR;
    } else {
        return MA_SUCCESS;
    }
}

ma_result test_data_converter__resampling_expected_output(void)
{
    ma_result result;
    ma_bool32 hasError = MA_FALSE;

    printf("Linear\n");
    result = test_data_converter__resampling_expected_output_by_algorithm(ma_resample_algorithm_linear);
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    if (hasError) {
        return MA_ERROR;
    } else {
        return MA_SUCCESS;
    }
}


ma_result test_data_converter__resampling_required_input_fixed_interval(ma_data_converter* pDataConverter, ma_uint64 frameCountPerIteration)
{
    ma_result result = MA_SUCCESS;
    ma_int16 input[4096];
    ma_int16 i;

    MA_ASSERT(frameCountPerIteration < ma_countof(input));

    /* Fill the input buffer with sequential numbers so we can get an idea on the state of things. Useful for inspecting the linear backend in particular. */
    for (i = 0; i < (ma_int16)ma_countof(input); i += 1) {
        input[i] = i;
    }

    for (i = 0; i < (ma_int16)ma_countof(input); i += (ma_int16)frameCountPerIteration) {
        ma_int16 output[4096];
        ma_uint64 outputFrameCount;
        ma_uint64 inputFrameCount;
        ma_uint64 requiredInputFrameCount;

        /* We retrieve the required number of input frames for the specified number of output frames, and then compare with what we actually get when reading. */
        ma_data_converter_get_required_input_frame_count(pDataConverter, frameCountPerIteration, &requiredInputFrameCount);

        outputFrameCount = frameCountPerIteration;
        inputFrameCount = ma_countof(input);
        result = ma_data_converter_process_pcm_frames(pDataConverter, input, &inputFrameCount, output, &outputFrameCount);
        if (result != MA_SUCCESS) {
            printf("Failed to process frames.");
            break;
        }

        if (inputFrameCount != requiredInputFrameCount) {
            printf("ERROR: Predicted vs actual input count mismatch: predicted=%d, actual=%d\n", (int)requiredInputFrameCount, (int)inputFrameCount);
            result = MA_ERROR;
        }
    }

    if (result != MA_SUCCESS) {
        printf("FAILED\n");
    } else {
        printf("PASSED\n");
    }

    return result;
}

ma_result test_data_converter__resampling_required_input_by_algorithm_and_rate_fixed_interval(ma_resample_algorithm algorithm, ma_uint32 rateIn, ma_uint32 rateOut, ma_uint64 frameCountPerIteration)
{
    ma_result result;
    ma_bool32 hasError = MA_FALSE;
    ma_data_converter converter;

    result = init_data_converter(rateIn, rateOut, algorithm, &converter);
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = test_data_converter__resampling_required_input_fixed_interval(&converter, frameCountPerIteration);

    ma_data_converter_uninit(&converter, NULL);

    if (hasError) {
        return MA_ERROR;
    } else {
        return MA_SUCCESS;
    }
}

ma_result test_data_converter__resampling_required_input_by_algorithm_fixed_interval(ma_resample_algorithm algorithm, ma_uint64 frameCountPerIteration)
{
    ma_result result;
    ma_bool32 hasError = MA_FALSE;

    result = test_data_converter__resampling_required_input_by_algorithm_and_rate_fixed_interval(algorithm, 44100, 48000, frameCountPerIteration);
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = test_data_converter__resampling_required_input_by_algorithm_and_rate_fixed_interval(algorithm, 48000, 44100, frameCountPerIteration);
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    
    result = test_data_converter__resampling_required_input_by_algorithm_and_rate_fixed_interval(algorithm, 44100, 192000, frameCountPerIteration);
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = test_data_converter__resampling_required_input_by_algorithm_and_rate_fixed_interval(algorithm, 192000, 44100, frameCountPerIteration);
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    if (hasError) {
        return MA_ERROR;
    } else {
        return MA_SUCCESS;
    }
}

ma_result test_data_converter__resampling_required_input_by_algorithm(ma_resample_algorithm algorithm)
{
    ma_result result;
    ma_bool32 hasError = MA_FALSE;

    result = test_data_converter__resampling_required_input_by_algorithm_fixed_interval(algorithm, 1);
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = test_data_converter__resampling_required_input_by_algorithm_fixed_interval(algorithm, 16);
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = test_data_converter__resampling_required_input_by_algorithm_fixed_interval(algorithm, 127);
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }
    
    if (hasError) {
        return MA_ERROR;
    } else {
        return MA_SUCCESS;
    }
}

ma_result test_data_converter__resampling_required_input(void)
{
    ma_result result;
    ma_bool32 hasError = MA_FALSE;

    printf("Linear\n");
    result = test_data_converter__resampling_required_input_by_algorithm(ma_resample_algorithm_linear);
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    if (hasError) {
        return MA_ERROR;
    } else {
        return MA_SUCCESS;
    }
}




ma_result test_data_converter__resampling(void)
{
    ma_result result;
    ma_bool32 hasError = MA_FALSE;

    result = test_data_converter__resampling_expected_output();
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = test_data_converter__resampling_required_input();
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    if (hasError) {
        return MA_ERROR;
    } else {
        return MA_SUCCESS;
    }
}


/*
####################################################################################################

Integration tests for the two public conversion entry points.

The same multi-track venue recording (an in-memory WAV file written with ma_encoder) is
converted through both public entries:

  1. Whole-segment conversion ("preview file"): ma_decode_memory() converts the entire
     recording in a single call.
  2. Streamed conversion ("convert while playing"): ma_data_converter_process_pcm_frames()
     converts the recording piece by piece, each piece sized to the number of frames the
     playback device can accept in one period.

Both entries output to the venue's physical speaker wiring (explicit output channel maps).

Covered scenarios:
  - Direct routing by speaker position; speakers without a matching track stay silent.
    A mixing matrix from the console takes precedence over the preset routing.
  - Recording and playback sample rates differ while channels are added/removed; every
    output channel sample is computable independently on the playback timeline (library
    linear resampling + its start delay).
  - When the real-time output buffer fills up, the remaining recording continues from the
    actually consumed position; the stitched waveform has the same sample phase as the
    whole-segment preview and the delivered frame counts reconstruct the full length.
####################################################################################################
*/

#define VENUE_RECORDING_RATE    44100   /* Recording device sample rate. */
#define VENUE_PLAYBACK_RATE     48000   /* Playback device sample rate. */
#define VENUE_TRACK_COUNT       4       /* Tracks in the recording. */
#define VENUE_FRAME_COUNT       1024    /* Recording length in frames. */
#define VENUE_MAX_SPEAKERS      8
#define VENUE_MAX_OUT_FRAMES    8192    /* Capacity for converted output buffers. */
#define VENUE_WAV_CAP           (64*1024)
#define VENUE_MAX_CALLBACKS     256

/* The recording's track layout. Matches the WAV file's default 4-channel map (FL/FR/FC/BC). */
static const ma_channel g_venueRecordingMap[VENUE_TRACK_COUNT] = {
    MA_CHANNEL_FRONT_LEFT, MA_CHANNEL_FRONT_RIGHT, MA_CHANNEL_FRONT_CENTER, MA_CHANNEL_BACK_CENTER
};

/* Deterministic recording signal: a distinct offset per track plus a gentle ramp so that both
the track of origin and the position on the timeline can be identified from any sample. */
static float venue_track_sample(ma_uint32 track, ma_uint64 frameIndex)
{
    return 0.1f*(float)(track + 1) + (float)frameIndex*(0.25f/(float)VENUE_FRAME_COUNT);
}

static ma_int32 venue_track_index_for_speaker(ma_channel speaker)
{
    ma_uint32 i;
    for (i = 0; i < VENUE_TRACK_COUNT; i += 1) {
        if (g_venueRecordingMap[i] == speaker) {
            return (ma_int32)i;
        }
    }
    return -1;  /* No track is wired to this speaker. */
}

/*
Reference model of the library's linear resampler (LPF disabled). Output frame k samples the
input timeline at k*rateIn/rateOut - startDelay, where startDelay is the converter's reported
input latency (ma_data_converter_get_input_latency()). Input frames before the first one are 0.
*/
static double venue_expected_resampled_sample(ma_uint32 track, ma_uint64 k, ma_uint32 rateIn, ma_uint32 rateOut, ma_uint64 startDelayInFrames)
{
    ma_int64 posNum = (ma_int64)(k*(ma_uint64)rateIn) - (ma_int64)(startDelayInFrames*(ma_uint64)rateOut);  /* In 1/rateOut input-frame units. */
    ma_int64 i   = posNum / (ma_int64)rateOut;
    ma_int64 rem = posNum % (ma_int64)rateOut;
    double frac;
    double v0;
    double v1;

    if (rem < 0) { i -= 1; rem += (ma_int64)rateOut; }  /* Floor division for negative positions. */
    frac = (double)rem / (double)rateOut;

    v0 = (i   < 0 || i   >= VENUE_FRAME_COUNT) ? 0.0 : (double)venue_track_sample(track, (ma_uint64)i);
    v1 = (i+1 < 0 || i+1 >= VENUE_FRAME_COUNT) ? 0.0 : (double)venue_track_sample(track, (ma_uint64)(i+1));

    return v0 + (v1 - v0)*frac;
}

static ma_bool32 venue_f32_close(float actual, double expected, double tolerance)
{
    double d = (double)actual - expected;
    if (d < 0) {
        d = -d;
    }
    return d <= tolerance;
}

/* In-memory file used to hold the recording as a WAV file. */
typedef struct
{
    ma_uint8 data[VENUE_WAV_CAP];
    size_t size;
    size_t cursor;
} venue_mem_file;

static ma_result venue_mem_write(ma_encoder* pEncoder, const void* pBufferIn, size_t bytesToWrite, size_t* pBytesWritten)
{
    venue_mem_file* pFile = (venue_mem_file*)pEncoder->pUserData;

    if (pFile->cursor + bytesToWrite > VENUE_WAV_CAP) {
        return MA_TOO_BIG;
    }

    MA_COPY_MEMORY(pFile->data + pFile->cursor, pBufferIn, bytesToWrite);
    pFile->cursor += bytesToWrite;
    if (pFile->size < pFile->cursor) {
        pFile->size = pFile->cursor;
    }

    *pBytesWritten = bytesToWrite;
    return MA_SUCCESS;
}

static ma_result venue_mem_seek(ma_encoder* pEncoder, ma_int64 offset, ma_seek_origin origin)
{
    venue_mem_file* pFile = (venue_mem_file*)pEncoder->pUserData;
    ma_int64 newCursor;

    if (origin == ma_seek_origin_start) {
        newCursor = offset;
    } else if (origin == ma_seek_origin_current) {
        newCursor = (ma_int64)pFile->cursor + offset;
    } else {
        newCursor = (ma_int64)pFile->size + offset;
    }

    if (newCursor < 0 || (ma_uint64)newCursor > (ma_uint64)VENUE_WAV_CAP) {
        return MA_INVALID_ARGS;
    }

    pFile->cursor = (size_t)newCursor;
    return MA_SUCCESS;
}

static venue_mem_file g_venueWav;                                           /* The recording, as a WAV file. */
static float g_venueSourcePCM[VENUE_FRAME_COUNT * VENUE_TRACK_COUNT];       /* The synthesized recording. */
static float g_venueDecodedPCM[VENUE_FRAME_COUNT * VENUE_TRACK_COUNT];      /* The recording decoded back from the WAV file (streaming entry input). */
static float g_venueStreamedPCM[VENUE_MAX_OUT_FRAMES * VENUE_MAX_SPEAKERS]; /* Stitched output of the streaming entry. */

/* Builds the multi-track recording: synthesize -> encode to in-memory WAV -> decode back
without conversion to obtain the exact PCM the streaming entry will be fed. */
static ma_result venue_build_recording(void)
{
    ma_result result;
    ma_encoder_config encoderConfig;
    ma_encoder encoder;
    ma_decoder_config decoderConfig;
    ma_uint64 decodedFrameCount = 0;
    void* pDecoded = NULL;
    ma_uint64 i;
    ma_uint32 t;

    for (i = 0; i < VENUE_FRAME_COUNT; i += 1) {
        for (t = 0; t < VENUE_TRACK_COUNT; t += 1) {
            g_venueSourcePCM[i*VENUE_TRACK_COUNT + t] = venue_track_sample(t, i);
        }
    }

    MA_ZERO_OBJECT(&g_venueWav);
    encoderConfig = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, VENUE_TRACK_COUNT, VENUE_RECORDING_RATE);
    result = ma_encoder_init(venue_mem_write, venue_mem_seek, &g_venueWav, &encoderConfig, &encoder);
    if (result != MA_SUCCESS) {
        printf("ERROR: Failed to initialize encoder for recording: %d\n", result);
        return result;
    }

    result = ma_encoder_write_pcm_frames(&encoder, g_venueSourcePCM, VENUE_FRAME_COUNT, NULL);
    ma_encoder_uninit(&encoder);
    if (result != MA_SUCCESS) {
        printf("ERROR: Failed to encode recording: %d\n", result);
        return result;
    }

    /* Decode the recording back without any conversion (passthrough) to feed the streaming entry. */
    decoderConfig = ma_decoder_config_init(ma_format_f32, VENUE_TRACK_COUNT, VENUE_RECORDING_RATE);
    result = ma_decode_memory(g_venueWav.data, g_venueWav.size, &decoderConfig, &decodedFrameCount, &pDecoded);
    if (result != MA_SUCCESS) {
        printf("ERROR: Failed to decode recording back: %d\n", result);
        return result;
    }

    if (decodedFrameCount != VENUE_FRAME_COUNT) {
        printf("ERROR: Recording round-trip frame count mismatch: expected=%d, actual=%d\n", (int)VENUE_FRAME_COUNT, (int)decodedFrameCount);
        ma_free(pDecoded, NULL);
        return MA_ERROR;
    }

    MA_COPY_MEMORY(g_venueDecodedPCM, pDecoded, sizeof(g_venueDecodedPCM));
    ma_free(pDecoded, NULL);

    if (memcmp(g_venueDecodedPCM, g_venueSourcePCM, sizeof(g_venueSourcePCM)) != 0) {
        printf("ERROR: Recording round-trip is not sample-exact.\n");
        return MA_ERROR;
    }

    return MA_SUCCESS;
}

/* Entry 1: whole-segment conversion of the recording file in a single call. */
static ma_result venue_convert_whole(ma_uint32 speakerCount, const ma_channel* pWiring, ma_channel_mix_mode mixMode, ma_uint32 rateOut, float** ppFramesOut, ma_uint64* pFrameCountOut)
{
    ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, speakerCount, rateOut);
    decoderConfig.pChannelMap                 = (ma_channel*)pWiring;
    decoderConfig.channelMixMode              = mixMode;
    decoderConfig.resampling.algorithm        = ma_resample_algorithm_linear;
    decoderConfig.resampling.linear.lpfOrder  = 0;  /* No filtering: output samples must be exactly computable. */

    return ma_decode_memory(g_venueWav.data, g_venueWav.size, &decoderConfig, pFrameCountOut, (void**)ppFramesOut);
}

static ma_data_converter_config venue_converter_config_init(ma_uint32 speakerCount, const ma_channel* pWiring, ma_channel_mix_mode mixMode, float** ppWeights, ma_uint32 rateOut)
{
    ma_data_converter_config config = ma_data_converter_config_init(ma_format_f32, ma_format_f32, VENUE_TRACK_COUNT, speakerCount, VENUE_RECORDING_RATE, rateOut);
    config.pChannelMapIn                 = (ma_channel*)g_venueRecordingMap;
    config.pChannelMapOut                = (ma_channel*)pWiring;
    config.channelMixMode                = mixMode;
    config.ppChannelWeights              = ppWeights;
    config.resampling.algorithm          = ma_resample_algorithm_linear;
    config.resampling.linear.lpfOrder    = 0;  /* No filtering: output samples must be exactly computable. */

    return config;
}

/* Entry 2: streamed conversion, delivering at most periodFrames frames per call, continuing
from the actually consumed input position whenever the output buffer fills up. */
static ma_result venue_convert_streaming(ma_uint32 speakerCount, const ma_channel* pWiring, ma_channel_mix_mode mixMode, float** ppWeights, ma_uint32 rateOut, ma_uint64 periodFrames, float* pOut, ma_uint64 outCapFrames, ma_uint64* pFramesProducedOut, ma_uint64* pFramesConsumedOut, ma_uint64* pInputLatencyOut, ma_uint64* pExpectedTotalOut)
{
    ma_result result;
    ma_data_converter_config config;
    ma_data_converter converter;
    ma_uint64 srcOffset = 0;
    ma_uint64 outOffset = 0;

    config = venue_converter_config_init(speakerCount, pWiring, mixMode, ppWeights, rateOut);
    result = ma_data_converter_init(&config, NULL, &converter);
    if (result != MA_SUCCESS) {
        return result;
    }

    if (pInputLatencyOut != NULL) {
        *pInputLatencyOut = ma_data_converter_get_input_latency(&converter);
    }
    if (pExpectedTotalOut != NULL) {
        ma_data_converter_get_expected_output_frame_count(&converter, VENUE_FRAME_COUNT, pExpectedTotalOut);
    }

    while (srcOffset < VENUE_FRAME_COUNT) {
        ma_uint64 framesIn  = VENUE_FRAME_COUNT - srcOffset;
        ma_uint64 framesOut = outCapFrames - outOffset;
        if (framesOut > periodFrames) {
            framesOut = periodFrames;   /* The device accepts at most this many frames per period. */
        }
        if (framesOut == 0) {
            ma_data_converter_uninit(&converter, NULL);
            return MA_ERROR;    /* Output buffer too small for the whole conversion. */
        }

        result = ma_data_converter_process_pcm_frames(&converter, g_venueDecodedPCM + srcOffset*VENUE_TRACK_COUNT, &framesIn, pOut + outOffset*speakerCount, &framesOut);
        if (result != MA_SUCCESS) {
            ma_data_converter_uninit(&converter, NULL);
            return result;
        }

        srcOffset += framesIn;  /* Continue from the actually consumed position. */
        outOffset += framesOut;

        if (framesIn == 0 && framesOut == 0) {
            break;  /* Safety. */
        }
    }

    ma_data_converter_uninit(&converter, NULL);

    *pFramesProducedOut = outOffset;
    *pFramesConsumedOut = srcOffset;

    return MA_SUCCESS;
}

#define VENUE_CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            printf("ERROR: %s (line %d)\n", message, (int)__LINE__); \
            result = MA_ERROR; \
            goto done; \
        } \
    } while (0)

/*
Scenario 1a: direct routing by speaker position. Each speaker receives the track wired to its
position regardless of cabling order; speakers without a matching track stay silent.
*/
static ma_result venue_test_speaker_routing_for_wiring(const ma_channel* pWiring, ma_uint32 speakerCount, const char* pWiringName)
{
    ma_result result = MA_SUCCESS;
    float* pWhole = NULL;
    ma_uint64 wholeFrames = 0;
    ma_uint64 produced = 0;
    ma_uint64 consumed = 0;
    ma_uint64 k;
    ma_uint32 o;

    printf("    %s\n", pWiringName);

    /* Entry 1: whole-segment preview. */
    result = venue_convert_whole(speakerCount, pWiring, ma_channel_mix_mode_simple, VENUE_RECORDING_RATE, &pWhole, &wholeFrames);
    VENUE_CHECK(result == MA_SUCCESS, "Whole-segment conversion failed.");
    VENUE_CHECK(wholeFrames == VENUE_FRAME_COUNT, "Whole-segment frame count mismatch (same rate must be 1:1).");

    /* Entry 2: streamed conversion in device-period sized chunks. */
    result = venue_convert_streaming(speakerCount, pWiring, ma_channel_mix_mode_simple, NULL, VENUE_RECORDING_RATE, 100, g_venueStreamedPCM, VENUE_FRAME_COUNT, &produced, &consumed, NULL, NULL);
    VENUE_CHECK(result == MA_SUCCESS, "Streamed conversion failed.");
    VENUE_CHECK(produced == wholeFrames, "Streamed vs whole-segment frame count mismatch.");
    VENUE_CHECK(consumed == VENUE_FRAME_COUNT, "Streamed conversion did not consume the whole recording.");
    VENUE_CHECK(memcmp(pWhole, g_venueStreamedPCM, (size_t)(wholeFrames*speakerCount*sizeof(float))) == 0, "Streamed and whole-segment waveforms differ.");

    /* Every speaker must receive exactly the track wired to its position, or silence. */
    for (k = 0; k < wholeFrames; k += 1) {
        for (o = 0; o < speakerCount; o += 1) {
            ma_int32 track = venue_track_index_for_speaker(pWiring[o]);
            float expected = (track < 0) ? 0.0f : venue_track_sample((ma_uint32)track, k);
            float actual = pWhole[k*speakerCount + o];
            VENUE_CHECK(actual == expected, "Speaker output is not the wired track (or silence).");
        }
    }

done:
    ma_free(pWhole, NULL);
    return result;
}

static ma_result test_public_conversion__speaker_routing(void)
{
    ma_result result;
    ma_bool32 hasError = MA_FALSE;

    /* Same speakers as the recording, but the venue wired them in reverse order. */
    static const ma_channel wiringReversed[4] = {
        MA_CHANNEL_BACK_CENTER, MA_CHANNEL_FRONT_CENTER, MA_CHANNEL_FRONT_RIGHT, MA_CHANNEL_FRONT_LEFT
    };

    /* The venue has two extra speakers (back left/right) for which the recording has no tracks. */
    static const ma_channel wiringExtended[6] = {
        MA_CHANNEL_FRONT_LEFT, MA_CHANNEL_FRONT_RIGHT, MA_CHANNEL_FRONT_CENTER, MA_CHANNEL_BACK_CENTER, MA_CHANNEL_BACK_LEFT, MA_CHANNEL_BACK_RIGHT
    };

    printf("  Direct routing by speaker position\n");

    result = venue_test_speaker_routing_for_wiring(wiringReversed, 4, "Reversed cabling (4 speakers)");
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = venue_test_speaker_routing_for_wiring(wiringExtended, 6, "Extra speakers without tracks (6 speakers)");
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    if (hasError) {
        printf("  FAILED\n");
        return MA_ERROR;
    } else {
        printf("  PASSED\n");
        return MA_SUCCESS;
    }
}

/*
Scenario 1b: the console hands over a mixing matrix. Every speaker must receive the weighted
sum of the input tracks exactly as specified, even though the preset routing (1:1 by position)
would produce a different sound. The matrix is only expressible through the streaming entry
(ma_data_converter), so the whole-segment entry is used to produce the preset routing for
contrast.
*/
static ma_result test_public_conversion__mixing_matrix(void)
{
    ma_result result = MA_SUCCESS;
    float* pPreset = NULL;
    ma_uint64 presetFrames = 0;
    ma_uint64 produced = 0;
    ma_uint64 consumed = 0;
    ma_uint64 k;
    ma_uint32 o;
    ma_uint32 i;
    ma_bool32 differsFromPreset = MA_FALSE;

    /* The venue's speakers are wired in reverse order, so the preset routing would shuffle the
    tracks back onto the right speakers. The matrix below deliberately contradicts that routing:
    e.g. the first output cable receives nothing from the front-left track. (Note: with an
    output map identical to the recording's, the library short-circuits to a passthrough, so a
    genuinely different wiring is needed for the matrix to be exercised at all.) */
    static const ma_channel wiring[4] = {
        MA_CHANNEL_BACK_CENTER, MA_CHANNEL_FRONT_CENTER, MA_CHANNEL_FRONT_RIGHT, MA_CHANNEL_FRONT_LEFT
    };

    /* Mixing matrix [input track][output cable]. */
    static float weightsFL[4] = { 0.00f, 0.50f, 0.25f, 0.00f };
    static float weightsFR[4] = { 0.75f, 0.00f, 0.00f, 0.50f };
    static float weightsFC[4] = { 0.25f, 0.25f, 0.00f, 0.00f };
    static float weightsBC[4] = { 0.00f, 0.00f, 0.75f, 0.00f };
    static float* weights[4]  = { weightsFL, weightsFR, weightsFC, weightsBC };

    printf("  Mixing matrix overrides preset routing\n");

    /* Preset routing via the whole-segment entry (1:1 by position). */
    result = venue_convert_whole(4, wiring, ma_channel_mix_mode_simple, VENUE_RECORDING_RATE, &pPreset, &presetFrames);
    VENUE_CHECK(result == MA_SUCCESS, "Whole-segment conversion failed.");

    /* Matrix routing via the streaming entry. */
    result = venue_convert_streaming(4, wiring, ma_channel_mix_mode_custom_weights, weights, VENUE_RECORDING_RATE, 64, g_venueStreamedPCM, VENUE_FRAME_COUNT, &produced, &consumed, NULL, NULL);
    VENUE_CHECK(result == MA_SUCCESS, "Streamed conversion with mixing matrix failed.");
    VENUE_CHECK(produced == presetFrames, "Matrix vs preset frame count mismatch.");
    VENUE_CHECK(consumed == VENUE_FRAME_COUNT, "Matrix conversion did not consume the whole recording.");

    for (k = 0; k < produced; k += 1) {
        for (o = 0; o < 4; o += 1) {
            double expected = 0;
            float actual = g_venueStreamedPCM[k*4 + o];
            for (i = 0; i < 4; i += 1) {
                expected += (double)weights[i][o] * (double)venue_track_sample(i, k);
            }
            VENUE_CHECK(venue_f32_close(actual, expected, 1e-4), "Speaker output does not match the mixing matrix.");
            if (actual != pPreset[k*4 + o]) {
                differsFromPreset = MA_TRUE;
            }
        }
    }

    VENUE_CHECK(differsFromPreset, "Matrix output unexpectedly matches the preset routing.");

done:
    ma_free(pPreset, NULL);
    if (result == MA_SUCCESS) {
        printf("  PASSED\n");
    } else {
        printf("  FAILED\n");
    }
    return result;
}

/*
Scenario 2: recording and playback sample rates differ while output channels are added or
removed. Every output channel sample must be computable independently on the playback
timeline, following the library's linear resampling and its start delay.
*/
static ma_result venue_test_resample_for_wiring(const ma_channel* pWiring, ma_uint32 speakerCount, ma_uint32 rateOut, const char* pName)
{
    ma_result result = MA_SUCCESS;
    float* pWhole = NULL;
    ma_uint64 wholeFrames = 0;
    ma_uint64 produced = 0;
    ma_uint64 consumed = 0;
    ma_uint64 inputLatency = 0;
    ma_uint64 expectedTotal = 0;
    ma_uint64 k;
    ma_uint32 o;

    printf("    %s\n", pName);

    /* Entry 1: whole-segment preview. */
    result = venue_convert_whole(speakerCount, pWiring, ma_channel_mix_mode_simple, rateOut, &pWhole, &wholeFrames);
    VENUE_CHECK(result == MA_SUCCESS, "Whole-segment conversion failed.");

    /* Entry 2: streamed conversion in device-period sized chunks. */
    result = venue_convert_streaming(speakerCount, pWiring, ma_channel_mix_mode_simple, NULL, rateOut, 96, g_venueStreamedPCM, VENUE_MAX_OUT_FRAMES, &produced, &consumed, &inputLatency, &expectedTotal);
    VENUE_CHECK(result == MA_SUCCESS, "Streamed conversion failed.");

    /* The linear resampler's start delay is exactly one input frame (LPF disabled). */
    VENUE_CHECK(inputLatency == 1, "Unexpected resampler start delay.");
    VENUE_CHECK(produced == expectedTotal, "Produced frame count differs from the library's prediction.");
    VENUE_CHECK(consumed == VENUE_FRAME_COUNT, "Streamed conversion did not consume the whole recording.");
    VENUE_CHECK(produced == wholeFrames, "Streamed vs whole-segment frame count mismatch.");
    VENUE_CHECK(memcmp(pWhole, g_venueStreamedPCM, (size_t)(wholeFrames*speakerCount*sizeof(float))) == 0, "Streamed and whole-segment waveforms differ.");

    /* Every output channel sample must match the independently computed value on the
    playback timeline. */
    for (k = 0; k < wholeFrames; k += 1) {
        for (o = 0; o < speakerCount; o += 1) {
            ma_int32 track = venue_track_index_for_speaker(pWiring[o]);
            float actual = pWhole[k*speakerCount + o];
            if (track < 0) {
                VENUE_CHECK(actual == 0.0f, "Speaker without a wired track is not silent.");
            } else {
                double expected = venue_expected_resampled_sample((ma_uint32)track, k, VENUE_RECORDING_RATE, rateOut, inputLatency);
                VENUE_CHECK(venue_f32_close(actual, expected, 1e-4), "Output sample does not match linear resampling on the playback timeline.");
            }
        }
    }

done:
    ma_free(pWhole, NULL);
    return result;
}

static ma_result test_public_conversion__resample_and_channel_count(void)
{
    ma_result result;
    ma_bool32 hasError = MA_FALSE;

    /* Adding speakers: 4 tracks -> 6 speakers, 44100 -> 48000. */
    static const ma_channel wiringAdded[6] = {
        MA_CHANNEL_FRONT_LEFT, MA_CHANNEL_FRONT_RIGHT, MA_CHANNEL_FRONT_CENTER, MA_CHANNEL_BACK_CENTER, MA_CHANNEL_BACK_LEFT, MA_CHANNEL_BACK_RIGHT
    };

    /* Removing speakers: 4 tracks -> 2 speakers, 44100 -> 32000. */
    static const ma_channel wiringRemoved[2] = {
        MA_CHANNEL_FRONT_LEFT, MA_CHANNEL_FRONT_RIGHT
    };

    printf("  Sample rate conversion with channel count changes\n");

    result = venue_test_resample_for_wiring(wiringAdded, 6, VENUE_PLAYBACK_RATE, "Adding channels (4 -> 6 speakers, 44100 -> 48000)");
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = venue_test_resample_for_wiring(wiringRemoved, 2, 32000, "Removing channels (4 -> 2 speakers, 44100 -> 32000)");
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    if (hasError) {
        printf("  FAILED\n");
        return MA_ERROR;
    } else {
        printf("  PASSED\n");
        return MA_SUCCESS;
    }
}

/*
Scenario 3: the real-time output buffer fills up every device period; the remaining recording
must continue converting from the actually consumed position. The stitched waveform must have
the same sample phase as the whole-segment preview, and the delivered frame counts must
reconstruct the full length of the valid audio.
*/
static ma_result test_public_conversion__streaming_continuation(void)
{
    ma_result result = MA_SUCCESS;
    float* pWhole = NULL;
    ma_uint64 wholeFrames = 0;
    ma_data_converter_config config;
    ma_data_converter converter;
    ma_uint64 expectedTotal = 0;
    ma_uint64 srcOffset = 0;
    ma_uint64 outOffset = 0;
    ma_uint64 delivered[VENUE_MAX_CALLBACKS];
    ma_uint64 callbackCount = 0;
    ma_uint64 deliveredTotal = 0;
    ma_uint64 i;
    const ma_uint64 periodFrames = 96;  /* Frames the device accepts per period. */

    static const ma_channel wiring[4] = {
        MA_CHANNEL_FRONT_LEFT, MA_CHANNEL_FRONT_RIGHT, MA_CHANNEL_FRONT_CENTER, MA_CHANNEL_BACK_CENTER
    };

    printf("  Streaming continuation after the output buffer fills\n");

    /* Entry 1: whole-segment preview (the reference waveform and length). */
    result = venue_convert_whole(4, wiring, ma_channel_mix_mode_simple, VENUE_PLAYBACK_RATE, &pWhole, &wholeFrames);
    VENUE_CHECK(result == MA_SUCCESS, "Whole-segment conversion failed.");

    /* Entry 2: the same recording converted period by period, as if playing out. */
    config = venue_converter_config_init(4, wiring, ma_channel_mix_mode_simple, NULL, VENUE_PLAYBACK_RATE);
    result = ma_data_converter_init(&config, NULL, &converter);
    VENUE_CHECK(result == MA_SUCCESS, "Failed to initialize data converter.");

    ma_data_converter_get_expected_output_frame_count(&converter, VENUE_FRAME_COUNT, &expectedTotal);

    while (srcOffset < VENUE_FRAME_COUNT) {
        ma_uint64 framesIn  = VENUE_FRAME_COUNT - srcOffset;
        ma_uint64 framesOut = periodFrames;     /* The output buffer holds exactly one period. */

        VENUE_CHECK(callbackCount < VENUE_MAX_CALLBACKS, "Too many device periods.");

        result = ma_data_converter_process_pcm_frames(&converter, g_venueDecodedPCM + srcOffset*VENUE_TRACK_COUNT, &framesIn, g_venueStreamedPCM + outOffset*4, &framesOut);
        VENUE_CHECK(result == MA_SUCCESS, "Failed to process frames.");

        /* The buffer filled up: the next call must continue from the consumed position. */
        srcOffset += framesIn;
        outOffset += framesOut;
        delivered[callbackCount] = framesOut;
        callbackCount += 1;

        if (framesIn == 0 && framesOut == 0) {
            break;  /* Safety. */
        }
    }

    ma_data_converter_uninit(&converter, NULL);

    /* The recording must have been consumed exactly once, from consumed positions only. */
    VENUE_CHECK(srcOffset == VENUE_FRAME_COUNT, "Recording was not consumed to the end.");

    /* The delivered frame counts must reconstruct the full length of the valid audio. */
    for (i = 0; i < callbackCount; i += 1) {
        deliveredTotal += delivered[i];
    }
    VENUE_CHECK(deliveredTotal == wholeFrames, "Delivered frame count does not reconstruct the whole-segment length.");
    VENUE_CHECK(deliveredTotal == expectedTotal, "Delivered frame count differs from the library's prediction.");

    /* Every period except the last must have been delivered completely full. */
    for (i = 0; i + 1 < callbackCount; i += 1) {
        VENUE_CHECK(delivered[i] == periodFrames, "A non-final period was not delivered full.");
    }

    /* The stitched waveform must have the same sample phase as the whole-segment preview. */
    VENUE_CHECK(memcmp(g_venueStreamedPCM, pWhole, (size_t)(wholeFrames*4*sizeof(float))) == 0, "Stitched waveform phase differs from the whole-segment preview.");

done:
    ma_free(pWhole, NULL);
    if (result == MA_SUCCESS) {
        printf("  PASSED\n");
    } else {
        printf("  FAILED\n");
    }
    return result;
}

int test_entry__public_conversion_entries(int argc, char** argv)
{
    ma_result result;
    ma_bool32 hasError = MA_FALSE;

    (void)argc;
    (void)argv;

    result = venue_build_recording();
    if (result != MA_SUCCESS) {
        printf("FAILED to build the recording fixture.\n");
        return -1;
    }

    result = test_public_conversion__speaker_routing();
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = test_public_conversion__mixing_matrix();
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = test_public_conversion__resample_and_channel_count();
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    result = test_public_conversion__streaming_continuation();
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }

    if (hasError) {
        return -1;
    } else {
        return 0;
    }
}



int test_entry__data_converter(int argc, char** argv)
{
    ma_result result;
    ma_bool32 hasError = MA_FALSE;

    (void)argc;
    (void)argv;

#if 0
    result = test_data_converter__passthrough();
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }
#endif

    result = test_data_converter__resampling();
    if (result != MA_SUCCESS) {
        hasError = MA_TRUE;
    }


    if (hasError) {
        return -1;
    } else {
        return 0;
    }
}



int main(int argc, char** argv)
{
    ma_register_test("Data Conversion", test_entry__data_converter);
    ma_register_test("Public Conversion Entries", test_entry__public_conversion_entries);

    return ma_run_tests(argc, argv);
}
