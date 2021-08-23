#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/mem.h>

#include <unistd.h>
#include <pthread.h>
#include <ffmpeg/include/libswresample/swresample.h>
#include "Common/circular_list.h"
#include "Common/thread/thread.h"
#include "sps_pps.h"
#include "AnsyncDecoder.h"
#include "check_frame_type.h"
#include "fflog.h"

#define MAX_FRAME_HEAD_LENGTH 256


#define MAX_AUDIO_FRAME_SIZE  192000

typedef struct stBufferData {
    u8 *head;
    u8 *data;
    int len;
    u32 timestamp;
    
    int max_len;
    int need_read;
    int media_type;
}BufferData;

struct stAnsyncDecoder {
    AVCodecContext *ctx;
    AVCodecParameters *param;
    struct SwsContext *swsContext;
    AVFrame *frame;
    AVPacket pkt;

    u8 frame_head[MAX_FRAME_HEAD_LENGTH]; // SPS + PPS
    u8 *sps;
    u8 *pps;
    int sps_length;
    int pps_length;

    AVCodecContext *a_ctx;
    AVCodecParameters *a_param;
    struct SwrContext *a_swrContext;
    AVFrame *a_frame;
    AVFrame *a_frame_out;
    AVPacket a_pkt;

    unsigned long long cnt_rcv;
    unsigned long long cnt_dec;
    
    CircularList *buffer_list;
    CircularListNode *node_write;
    
    RTPThread *thread;
    int quit;
    int running;

	uint8_t *rgb_data;

    void *userdata;
    decoder_callback callback;

	int frame_width;
	int frame_height;
};

static void decode_video_node(AnsyncDecoder *ad, BufferData *buffer) {
    AVPacket pkt;
    u8 nalu_type = buffer->data[4] & 0x1f;
    u8 *pkt_data = NULL;
    int pkt_len = 0;
//    switch (nalu_type) {
//        case 0x05:
//            pkt_data = buffer->head;
//            pkt_len = ad->sps_length + ad->pps_length + buffer->len;
//            memcpy(buffer->head, ad->frame_head, ad->sps_length + ad->pps_length);
//            break;
//
//        case 0x07:
//            memcpy(ad->sps, buffer->data, buffer->len);
//            ad->sps_length = buffer->len;
//            ad->pps = ad->sps + buffer->len;
//            break;
//
//        case 0x08:
//            memcpy(ad->pps, buffer->data, buffer->len);
//            ad->pps_length = buffer->len;
//            break;
//
//        default:
//            pkt_data = buffer->data;
//            pkt_len = buffer->len;
//            break;
//    }

    pkt_data = buffer->data;
    pkt_len = buffer->len;

    if (pkt_data == NULL || pkt_len == 0) {
        return;
    }

    av_init_packet(&pkt);
    pkt.data = pkt_data;
    pkt.size = pkt_len;

    do {
		if (ad->ctx->has_b_frames > 1) {
			ad->ctx->has_b_frames = 1;
		}

        int result = avcodec_send_packet(ad->ctx, &pkt);
        if (result < 0 && result != AVERROR_EOF) {
            break;
        }
        
        result = avcodec_receive_frame(ad->ctx, ad->frame);
        if (result < 0 && result != AVERROR_EOF) {
            printf("[ffmpeg error] %d : %s\n",result, av_err2str(result));
            break;
        }

        if (ad->swsContext == NULL) {
            ad->swsContext = sws_getContext(ad->ctx->width, ad->ctx->height, ad->ctx->pix_fmt,
                                            ad->ctx->width, ad->ctx->height, AV_PIX_FMT_RGBA,
                                            SWS_BICUBIC, NULL, NULL, NULL);
            if (!ad->swsContext) {
                printf("failure to get sws context\n");
                break;
            }

			ad->rgb_data = (uint8_t *)malloc((size_t)ad->ctx->width * ad->ctx->height * 4);
		}

        if(result == 0) {
			int line_size[2] = {ad->frame->width * 4, 0};
            sws_scale(ad->swsContext,
                      (const uint8_t* const *)ad->frame->data,
                      ad->frame->linesize,
                      0,
                      ad->ctx->height,
                      &ad->rgb_data,
                      line_size);

            if (ad->callback) {
                ad->callback(ad->userdata, ad->rgb_data, ad->ctx->width * ad->ctx->height * 4, ad->ctx->width, ad->ctx->height, buffer->timestamp, 1);
            }
        }
    } while (0);
    
    av_packet_unref(&pkt);
}

static FILE *fp0 = NULL;
static FILE *fp1 = NULL;

static void decode_audio_node(AnsyncDecoder *ad, BufferData *buffer) {
    AVPacket pkt;

    if (buffer->data == NULL || buffer->len == 0) {
        return;
    }

    av_init_packet(&pkt);
    pkt.data = buffer->data;
    pkt.size = buffer->len;

    do {
        int result = avcodec_send_packet(ad->a_ctx, &pkt);
        if (result < 0 && result != AVERROR_EOF) {
            printf("[ffmpeg error] %d : %s\n",result, av_err2str(result));
            break;
        }

        result = avcodec_receive_frame(ad->a_ctx, ad->a_frame);
        if (result < 0 && result != AVERROR_EOF) {
            printf("[ffmpeg error] %d : %s\n",result, av_err2str(result));
            break;
        }

        if (fp0 == NULL) {
            fp0 = fopen("/sdcard/DCIM/Test/test0.pcm", "wb");
        }
        result = (int) fwrite(ad->a_frame->data[0], 1, ad->a_frame->linesize[0], fp0);
        printf("fp0 fwrite result(%d)", result);

        if (ad->a_swrContext == NULL) {
            ad->a_swrContext = swr_alloc_set_opts(NULL, AV_CH_LAYOUT_MONO, AV_SAMPLE_FMT_S16, 44100,
                               ad->a_frame->channel_layout, (enum AVSampleFormat) ad->a_frame->format, ad->a_frame->sample_rate,
                               0, NULL);
            swr_init(ad->a_swrContext);
        }

        ad->a_frame_out->format = AV_SAMPLE_FMT_S16;
        ad->a_frame_out->channel_layout = AV_CH_LAYOUT_MONO;
        ad->a_frame_out->sample_rate = 44100;
        result = swr_convert_frame(ad->a_swrContext, ad->a_frame_out, ad->a_frame);
        if (result < 0 && result != AVERROR_EOF) {
            printf("[ffmpeg error] %d : %s\n",result, av_err2str(result));
            break;
        }

        if (ad->callback) {
            if (fp1 == NULL) {
                fp1 = fopen("/sdcard/DCIM/Test/test1.pcm", "wb");
            }
            result = (int) fwrite(ad->a_frame_out->data[0], 1, (size_t) ad->a_frame_out->linesize[0], fp1);
            printf("fp1 fwrite result(%d)", result);
            ad->callback(ad->userdata, ad->a_frame_out->data[0], ad->a_frame_out->linesize[0], 0, 0, 0, 2);
        }
    } while (0);
    av_packet_unref(&pkt);
}

static void decode_thread_func(void *userdata) {
    AnsyncDecoder *ad = (AnsyncDecoder*)userdata;
    CircularListNode *node = ad->buffer_list->nodes;
    BufferData *buffer;
    ad->running = 1;
    while (!ad->quit) {
        buffer = (BufferData*)node->data;
        if(buffer->need_read){
            if (buffer->media_type == 1) {
                decode_video_node(ad, buffer);
            } else if (buffer->media_type == 2) {
                decode_audio_node(ad, buffer);
            }
            ad->cnt_dec++;
            buffer->need_read = 0;
            node = node->next;
        }else{
            usleep(1000);
        }
    }
    ad->running = 0;
}

CAPI AnsyncDecoder* AnsyncDecoder_Create(const void *sps, int sps_length, const void *pps, int pps_length, void *userdata, decoder_callback callback) {
    AnsyncDecoder *ad = NULL;
    AVCodec *codec = NULL;
    AVCodecParameters* param = NULL;
    int ok = 0;
    int result;
    h264_sps_t h264_sps;
    h264_sps_read((u8*)sps, sps_length, &h264_sps);
    av_register_all();

    do {
        ad = (AnsyncDecoder *)malloc(sizeof(AnsyncDecoder));
        if (!ad) break;
        memset(ad, 0, sizeof(AnsyncDecoder));

        // 视频数据解码
        codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        if(!codec) break;
        
        ad->ctx = avcodec_alloc_context3(codec);
        if (!ad->ctx) break;
        
        param = avcodec_parameters_alloc();
        param->width = 1080; //(h264_sps.i_mb_width*16 + 7)*8/8;
        param->height = 1920;// (h264_sps.i_mb_height*16 + 7)*8/8;

		ad->frame_width = param->width;
		ad->frame_height = param->height;
        
        result = avcodec_parameters_to_context(ad->ctx, param);
        if(result < 0) {
            printf("[ffmpeg error] %d : %s\n",result, av_err2str(result));
            break;
        }
        avcodec_parameters_free(&param);

        result = avcodec_open2(ad->ctx, codec, NULL);
        if (result < 0) {
            printf("[ffmpeg error] %d : %s\n",result,av_err2str(result));
            break;
        }
        
        ad->frame = av_frame_alloc();
        if(!ad->frame) {
            printf("av_frame_alloc error.\n");
            break;
        }

//      ad->sps = ad->frame_head;
//      ad->sps_length = sps_length + 4;
//
//      ad->pps = ad->sps + sps_length + 4;
//      ad->pps_length = pps_length + 4;
//
//		ad->sps[0] = 0x00;
//		ad->sps[1] = 0x00;
//		ad->sps[2] = 0x00;
//		ad->sps[3] = 0x01;
//		ad->pps[0] = 0x00;
//		ad->pps[1] = 0x00;
//		ad->pps[2] = 0x00;
//		ad->pps[3] = 0x01;
//		memcpy(ad->sps+4, sps, sps_length);
//		memcpy(ad->pps+4, pps, pps_length);

        // 音频数据解码
        codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
        if(!codec) break;

        ad->a_ctx = avcodec_alloc_context3(codec);
        if (!ad->a_ctx) break;

        param = avcodec_parameters_alloc();
        param->codec_type = AVMEDIA_TYPE_AUDIO;
        param->sample_rate = 44100;
        param->format = AV_SAMPLE_FMT_FLTP;
        param->channels = 1;
        param->channel_layout = AV_CH_LAYOUT_MONO;
        param->bit_rate = 64000;

        result = avcodec_parameters_to_context(ad->a_ctx, param);
        if(result < 0) {
            printf("[ffmpeg error] %d : %s\n",result, av_err2str(result));
            break;
        }
        avcodec_parameters_free(&param);

        result = avcodec_open2(ad->a_ctx, codec, NULL);
        if (result < 0) {
            printf("[ffmpeg error] %d : %s\n",result, av_err2str(result));
            break;
        }

        ad->a_frame = av_frame_alloc();
        if(!ad->a_frame) {
            printf("av_frame_alloc error.\n");
            break;
        }

        ad->a_frame_out = av_frame_alloc();
        if (!ad->a_frame_out) {
            printf("av_frame_alloc error.\n");
            break;
        }

        ad->thread = Thread_Create(decode_thread_func, ad);
        if (!ad->thread)
            break;
        
        ad->buffer_list = CircularList_Create(240, sizeof(BufferData));
        ad->node_write = ad->buffer_list->nodes;
        
        ad->callback = callback;
        ad->userdata = userdata;
        Thread_Run(ad->thread);
        ok = 1;
    } while (0);
    
    if (!ok) {
        AnsyncDecoder_Destroy(ad);
        ad = NULL;
    }
    
    return ad;
}

CAPI void AnsyncDecoder_Destroy(AnsyncDecoder *ad) {
    if (ad) {
        if (ad->thread) {
            ad->quit = 1;
            Thread_Join(ad->thread);
            Thread_Destroy(ad->thread);
        }
        av_frame_free(&ad->frame);
        avcodec_close(ad->ctx);
        avcodec_free_context(&ad->ctx);
        sws_freeContext(ad->swsContext);


        av_frame_free(&ad->a_frame_out);
        av_frame_free(&ad->a_frame);
        avcodec_close(ad->a_ctx);
        avcodec_free_context(&ad->a_ctx);
        swr_free(&ad->a_swrContext);
        
        if (ad->buffer_list) {
            CircularListNode *node = ad->buffer_list->nodes;
            BufferData *buffer;
            int i, count = ad->buffer_list->count;
            for (i=0; i<count; i++) {
                buffer = (BufferData*)node->data;
                if (buffer->head) {
                    free(buffer->head);
                    buffer->head = NULL;
                }
                node++;
            }
            
            CircularList_Destroy(ad->buffer_list);
            ad->buffer_list = NULL;
        }

		if (ad->rgb_data) {
			free(ad->rgb_data);
		}

        free(ad);
    }
}

CAPI void AnsyncDecoder_ReceiveData(AnsyncDecoder *ad, void *data, int len, u32 timestamp, int mediaType) {
    if (ad && ad->running) {
        BufferData *buffer = (BufferData*)ad->node_write->data;

        int count = 20;
        while(buffer->need_read==1&&count-- >0){
            usleep(1000);
        }

        if(buffer->need_read==1)
            return;

        if (mediaType == 1) {
            // alloc 256 bytes more to prevent avcodec_send_packet crash
            int malloc_len = len + ad->sps_length + ad->pps_length + 256;

            if (buffer->head && buffer->max_len < malloc_len) {
                free(buffer->head);
                buffer->head = NULL;
                buffer->data = NULL;
                buffer->max_len = 0;
            }

            if (!buffer->head) {
                buffer->head = (u8*)malloc(malloc_len);
                buffer->max_len = malloc_len;
            }
            buffer->media_type = 1;
            buffer->len = len;
            buffer->timestamp = timestamp;
            buffer->data = buffer->head + ad->sps_length + ad->pps_length;
            memcpy(buffer->data, data, len);

        } else if (mediaType == 2) {
            if (buffer->head && buffer->max_len < len) {
                free(buffer->head);
                buffer->head = NULL;
                buffer->data = NULL;
                buffer->max_len = 0;
            }

            if (!buffer->head) {
                buffer->head = (u8*)malloc((size_t)len);
                buffer->max_len = len;
            }

            buffer->media_type = 2;
            buffer->len = len;
            buffer->timestamp = timestamp;
            buffer->data = buffer->head;
            memcpy(buffer->data, data, (size_t)len);
        }

        buffer->need_read = 1;
        ad->node_write = ad->node_write->next;
        ad->cnt_rcv++;
    }
}

CAPI int AnsyncDecoder_GetWidth(AnsyncDecoder *ad) {
    int w = 0;
    if (ad && ad->ctx) {
        w = ad->frame_width;
    }
    return w;
}

CAPI int AnsyncDecoder_GetHeight(AnsyncDecoder *ad) {
    int h = 0;
    if (ad && ad->ctx) {
        h = ad->frame_height;
    }
    return h;
}


