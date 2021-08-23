//
// Created by yuzh on 2017/7/29.
//

#ifndef __FF_MP4_H__
#define __FF_MP4_H__

#include <libavformat/avformat.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEDIA_MUXER_UNKNOWN  -1
#define MEDIA_MUXER_START    0x21
#define MEDIA_MUXER_STOP     0x22

typedef struct FFMp4 {
    AVFormatContext *ofmt_ctx;
    pthread_mutex_t lock;
    char sps_pps[256];
    int sps_pps_len;
    int status;
    int isRunning;
    int frameRate;
    int first_key_frame_for_mp4;
    int count;
} FFMp4;

FFMp4* ff_mp4_init(const char *file, int width, int height, void *sps_pps, int sps_pps_len, int frameRate);
int ff_mp4_uninit(FFMp4* ffMp4);
int ff_mp4_write(FFMp4* ffMp4, unsigned char *data, int data_len, int media_type);
int ff_mp4_isRunning(FFMp4* ffMp4);
#ifdef __cplusplus
}
#endif

#endif //UP_ZX_FF_MP4_H
