//
// Created by yuzh on 2017/7/29.
//

#include <pthread.h>

#include "ff_mp4.h"
#include "ff_common.h"
#include "check_frame_type.h"

FFMp4* ff_mp4_init(const char *file, int width, int height, void *sps_pps, int sps_pps_len, int frameRate) {
    if (sps_pps_len <= 0) {
        return NULL;
    }
    int ret;
    FFMp4 *ffMp4 = (FFMp4 *)malloc(sizeof(FFMp4));
    memset(ffMp4, 0, sizeof(FFMp4));

    pthread_mutex_init(&ffMp4->lock, NULL);
    pthread_mutex_lock(&ffMp4->lock);
    av_register_all();
//    avformat_network_init();
    ret = avformat_alloc_output_context2(&ffMp4->ofmt_ctx, NULL, NULL, file);
    CHECK_FF_ERROR_ASSERT(ret)

    AVStream *ostream_v = avformat_new_stream(ffMp4->ofmt_ctx, NULL);
    CHECK_NULL_ASSERT(ostream_v)

    ostream_v->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    ostream_v->codecpar->codec_id = AV_CODEC_ID_H264;
    ostream_v->codecpar->width = width;
    ostream_v->codecpar->height = height;
    ostream_v->time_base.num = 1;
    ostream_v->time_base.den = 90000;
    ostream_v->codecpar->codec_tag = 0;
    ostream_v->codecpar->extradata = malloc((size_t) (sps_pps_len + AV_INPUT_BUFFER_PADDING_SIZE));
    memset(ostream_v->codecpar->extradata, 0, (size_t) (sps_pps_len + AV_INPUT_BUFFER_PADDING_SIZE));
    memcpy(ostream_v->codecpar->extradata, sps_pps, (size_t) sps_pps_len);
    ostream_v->codecpar->extradata_size = sps_pps_len;

//    AVStream *ost2 = avformat_new_stream(ffMp4->ofmt_ctx, NULL);
//    CHECK_NULL_ASSERT(ost2)
//    ost2->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
//    ost2->codecpar->codec_id = AV_CODEC_ID_AAC;
//    ost2->codecpar->channel_layout = AV_CH_LAYOUT_STEREO;
//    ost2->codecpar->channels = 2;
//    ost2->codecpar->sample_rate = 44100;
//    ost2->codecpar->profile = FF_PROFILE_AAC_LOW;
//    ost2->codecpar->codec_tag = 0;

    av_dump_format(ffMp4->ofmt_ctx, 0, file, 1);

    if (ffMp4->ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        ffMp4->ofmt_ctx->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }

    if (!(ffMp4->ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open2(&ffMp4->ofmt_ctx->pb, file, AVIO_FLAG_WRITE, &ffMp4->ofmt_ctx->interrupt_callback, NULL);
        CHECK_FF_ERROR_ASSERT(ret)
    }

    ret = avformat_write_header(ffMp4->ofmt_ctx, NULL);
    CHECK_FF_ERROR_ASSERT(ret)
    ffMp4->frameRate = frameRate;
    ffMp4->isRunning = 1;
    pthread_mutex_unlock(&ffMp4->lock);
    return ffMp4;
}

int ff_mp4_uninit(FFMp4* ffMp4) {
    CHECK_NULL_R(ffMp4, 0)
    pthread_mutex_lock(&ffMp4->lock);

    int ret;
    ret = av_write_trailer(ffMp4->ofmt_ctx);
    CHECK_FF_ERROR(ret)
//  avformat_network_deinit();
    avio_close(ffMp4->ofmt_ctx->pb);
    avformat_free_context(ffMp4->ofmt_ctx);

    pthread_mutex_unlock(&ffMp4->lock);
    pthread_mutex_destroy(&ffMp4->lock);
    free(ffMp4);
    return 1;
}

int ff_mp4_isRunning(FFMp4* ffMp4) {
    if (ffMp4 != NULL && ffMp4->isRunning) {
        return 1;
    }
    return 0;
}

/**
 * pts: 单位是毫秒
 * media_type: 0:video 1:audio
 */
int ff_mp4_write(FFMp4* ffMp4, unsigned char *data, int data_len, int media_type) {
    CHECK_NULL_ASSERT(ffMp4);
    int ret;
    int started = ffMp4->count > 0;
    int isIDR = 0;
    if (check_frame_type(data) == I_FRAME) {
        started = 1;
        isIDR = 1;
    }
    if (check_frame_type(data) == SPS_FRAME || check_frame_type(data) == PPS_FRAME) {
        return 0;
    }
    if (started) {
        pthread_mutex_lock(&ffMp4->lock);
        AVPacket *pkt = av_packet_alloc();
        av_init_packet(pkt);
        pkt->data = data;
        pkt->size = data_len;
        pkt->pts =  (long)(90000/ffMp4->frameRate) * ffMp4->count;
//      pkt->dts = pts;
        pkt->flags = isIDR;
        pkt->duration = 0;
        pkt->stream_index = media_type;
        pkt->pos = -1;
        ret = av_interleaved_write_frame(ffMp4->ofmt_ctx, pkt);
        CHECK_FF_ERROR(ret)
        ffMp4->count++;
        av_packet_free(&pkt);
        pthread_mutex_unlock(&ffMp4->lock);
    }
    return 1;
}

