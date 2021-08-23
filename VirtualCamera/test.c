//
//  main.c
//  TestFFmpeg
//
//  Created by yuzh on 2019/5/11.
//  Copyright © 2019 yuzh. All rights reserved.
//

#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libswresample/swresample.h>

#include "fflog.h"

#define MAX_AUDIO_FRAME_SIZE  192000

#define SAMPLE_PRT(fmt...)   \
do {\
printf("[%s]-%d: ", __FUNCTION__, __LINE__);\
printf(fmt);\
}while(0)

const char *in_file = "/sdcard/DCIM/testx.aac";
const char *out_file = "/sdcard/DCIM/xxx.pcm";
int testaac()
{
    //注册所有的工具
    av_register_all();

    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext  *cod_ctx = NULL;
    AVCodec         *cod   = NULL;

    //分配一个avformat
    fmt_ctx = avformat_alloc_context();
    if (fmt_ctx == NULL)
        printf("alloc fail");

    //打开文件，解封装
    if (avformat_open_input(&fmt_ctx, in_file, NULL, NULL) != 0)
        printf("open fail");

    //查找文件的相关流信息
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0)
        printf("find stream fail");

    //输出格式信息
    av_dump_format(fmt_ctx, 0, in_file, 0);

    //查找解码信息
    int stream_index = -1;
    for (int i = 0; i < fmt_ctx->nb_streams; i++)
        if (fmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            stream_index = i;
            break;
        }

    if (stream_index == -1)
        printf("find stream fail");

    //保存解码器
    cod_ctx = fmt_ctx->streams[stream_index]->codec;
    cod = avcodec_find_decoder(cod_ctx->codec_id);

    if (cod == NULL)
        printf("find codec fail");

    if (avcodec_open2(cod_ctx, cod, NULL) < 0)
        printf("can't open codec");

    FILE *out_fb = NULL;
    out_fb = fopen(out_file, "wb");

    //创建packet,用于存储解码前的数据
    AVPacket *packet = (AVPacket *)malloc(sizeof(AVPacket));
    av_init_packet(packet);

    //设置转码后输出相关参数
    //采样的布局方式
    uint64_t out_channel_layout = AV_CH_LAYOUT_MONO;
    //采样个数
    int out_nb_samples = 1024;
    //采样格式
    enum AVSampleFormat  sample_fmt = AV_SAMPLE_FMT_S16;
    //采样率
    int out_sample_rate = 44100;
    //通道数
    int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);
    printf("%d\n",out_channels);

    //创建buffer
    int buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, sample_fmt, 1);

    //注意要用av_malloc
    uint8_t *buffer = (uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE * 2);


    //创建Frame，用于存储解码后的数据
    AVFrame *frame = av_frame_alloc();

    int got_picture;

    int64_t in_channel_layout = av_get_default_channel_layout(cod_ctx->channels);
    printf("cod_ctx->sample_fmt(%d) cod_ctx->sample_rate(%d) in_channel_layout(%ld)",
            cod_ctx->sample_fmt, cod_ctx->sample_rate, in_channel_layout);

    //打开转码器
    struct SwrContext *convert_ctx = swr_alloc();
    //设置转码参数
    convert_ctx = swr_alloc_set_opts(convert_ctx, out_channel_layout, sample_fmt, out_sample_rate, \
                                     in_channel_layout, cod_ctx->sample_fmt, cod_ctx->sample_rate, 0, NULL);
    //初始化转码器
    swr_init(convert_ctx);

    printf("input sample_fmt(%d) sample_rate(%d) in_channel_layout(%ld)",
           cod_ctx->sample_fmt, cod_ctx->sample_rate, in_channel_layout);

    printf("output sample_fmt(%d) sample_rate(%d) in_channel_layout(%ld)",
           sample_fmt, out_sample_rate, out_channel_layout);



    //while循环，每次读取一帧，并转码
    int result = 0;
    while (av_read_frame(fmt_ctx, packet) >= 0) {

        if (packet->stream_index == stream_index) {

            //解码声音
            if ((result = avcodec_decode_audio4(cod_ctx, frame, &got_picture, packet) )< 0) {
                printf("decode error %s", av_err2str(result));
                continue;
            }

            if (got_picture > 0) {
                //转码
                swr_convert(convert_ctx, &buffer, MAX_AUDIO_FRAME_SIZE, (const uint8_t **)frame->data, frame->nb_samples);

//                printf("pts:%10lld\t packet size:%d\n", packet->pts, packet->size);

                fwrite(buffer, 1, buffer_size, out_fb);
            }
            got_picture=0;
        }

        av_free_packet(packet);
    }

    swr_free(&convert_ctx);

    fclose(out_fb);

    return 0;
}
