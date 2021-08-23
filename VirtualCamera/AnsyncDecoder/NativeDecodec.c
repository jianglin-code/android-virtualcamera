//
// Created by yuzh on 2018/9/10.
//
#include <stdlib.h>
#include <media/NdkMediaCodec.h>
#include <memory.h>

#include "NativeDecodec.h"
#include "fflog.h"

#ifndef U8
#define U8
typedef unsigned char u8;
#endif

#ifndef U32
#define U32
typedef unsigned int u32;
#endif

typedef struct stNativeCodec {
    AMediaCodec *mediaCodec;
    int width;
    int height;
    int color_format;
} NativeCodec;

NativeCodec* NativeCodec_Create(int height, int width) {

    NativeCodec *codec = (NativeCodec *)malloc(sizeof(NativeCodec));
    memset(codec, 0, sizeof(NativeCodec));
    const char *mime = "video/avc";
    AMediaCodec *mediaCodec = NULL;
    AMediaFormat *format;

    mediaCodec = AMediaCodec_createDecoderByType(mime);

    format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, width);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, height);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, 20);

    AMediaCodec_configure(mediaCodec, format, NULL, NULL, 0);
    AMediaCodec_start(mediaCodec);

    LOGFV("AMediaFormat : %s", AMediaFormat_toString(format));
    AMediaFormat_delete(format);
    codec->mediaCodec = mediaCodec;

    return codec;
}

void NativeDecodec_Destroy(NativeCodec* decodec) {

    if (decodec) {
        AMediaCodec_stop(decodec->mediaCodec);
        AMediaCodec_delete(decodec->mediaCodec);
        decodec->mediaCodec = NULL;
        free(decodec);
    }
}

void NaticeDecodec_InputData(NativeCodec *codec, void *data, int length, u32 timestamp) {
    if (codec == NULL || codec->mediaCodec == NULL) {
        LOGFD("pls create codec first...");
        return;
    }
    size_t input_index;
    size_t out_size;
    uint8_t *input_buf;

    AMediaCodecBufferInfo bufferInfo;
    size_t output_index;
    uint8_t *output_buf;

    int cntIn = 0;
    int cntOut = 0;

    input_index = (size_t) AMediaCodec_dequeueInputBuffer(codec->mediaCodec, 10000);
    if (input_index >= 0) {
        input_buf = AMediaCodec_getInputBuffer(codec->mediaCodec, input_index, &out_size);
        memcpy(input_buf, data, (size_t) length);
        AMediaCodec_queueInputBuffer(codec->mediaCodec, input_index, 0, (size_t) length, timestamp, 0);
        cntIn++;
    } else {
        LOGFV("Not have available InputBuffer...");
    }

    output_index = (size_t) AMediaCodec_dequeueOutputBuffer(codec->mediaCodec, &bufferInfo, 0);
    if (output_index >= 0) {
        output_buf = AMediaCodec_getOutputBuffer(codec->mediaCodec, output_index, &out_size);
        cntOut++;
        LOGFV("Output buffer out_index(%zd), out_size(%zd) cntIn(%d) cntOut(%d)", output_index, out_size, cntIn, cntOut);

    } else if (AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED == output_index) {
        LOGFV("output buffer changed");

    } else if (AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED == output_index) {
        AMediaFormat *format = NULL;
        format = AMediaCodec_getOutputFormat(codec->mediaCodec);
        AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &codec->width);
        AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &codec->height);
        AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, &codec->color_format);
        LOGD("format changed to: %s", AMediaFormat_toString(format));
        AMediaFormat_delete(format);

    } else if (AMEDIACODEC_INFO_TRY_AGAIN_LATER == output_index) {
        LOGV("no output buffer right now");

    } else {
        LOGV("unexpected info code: %zd", output_index);
    }

}
