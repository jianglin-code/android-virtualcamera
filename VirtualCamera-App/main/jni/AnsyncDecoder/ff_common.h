#ifndef __FF_COMMON_H__
#define __FF_COMMON_H__

#include <stdio.h>
#include <assert.h>

#include "fflog.h"

#define CHECK_FF_ERROR(result) \
do { \
    if(result < 0) { \
        LOGFE("[ffmpeg error] %d : %s\n", result, av_err2str(result)); \
    } \
} while(0);

#define CHECK_FF_ERROR_ASSERT(result) \
do { \
    if(result < 0) { \
        LOGFE("[ffmpeg error] %d : %s\n", result, av_err2str(result)); \
        assert(0); \
    } \
} while(0);


#define UNKNOMN     0
#define SPS_FRAME   1
#define PPS_FRAME   2
#define I_FRAME     3
#define P_FRAME     4

//检测h264数据是I帧 还是P帧
extern "C" static inline int check_frame_type(unsigned char *data) {
    if ( (data[4]&0x1f) == 0x01 ) {
        return P_FRAME;

    } else if ( (data[4]&0x1f) == 0x05 ) {
        return I_FRAME;

    } else if ( (data[4]&0x1f) == 0x07 ) {
        return SPS_FRAME;

    } else if ( (data[4]&0x1f) == 0x08 ) {
        return PPS_FRAME;
    }
    return UNKNOMN;
}

#endif
