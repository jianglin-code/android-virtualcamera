#define LOG_TAG "VIRTUALCAMERA"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <fcntl.h>
#include <errno.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <android/native_window.h>

#include <JRTPLIB/src/rtpipv4address.h>
#include <JRTPLIB/src/rtptimeutilities.h>
#include <JRTPLIB/src/rtpudpv4transmitter.h>
#include <JRTPLIB/src/rtpsessionparams.h>
#include <JRTPLIB/src/rtpsession.h>
#include <JRTPLIB/src/rtppacket.h>
#include <Common/thread/thread.h>
#include <AnsyncDecoder/AnsyncDecoder.h>

#include "VirtualCameraService.h"

#include <binder/IServiceManager.h>
#include <gui/ISurfaceComposer.h>
#include <utils/String16.h>

#include <ui/GraphicBuffer.h>
#include <gui/IGraphicBufferProducer.h>
#include <gui/IProducerListener.h>
#include <gui/Surface.h>
#include <utils/Trace.h>
#include <utils/Mutex.h>
#include <system/window.h>

using namespace jrtplib;
using namespace android;

static RTPThread* msRecvThread = NULL;
static int msRecvQuit = 1;
static AnsyncDecoder* msDecoder = NULL;
static android::sp<ANativeWindow> msWindow;
static android::sp<ANativeWindow> msCallBackWindow;
static RTPSession msVideoSession;
static Mutex mInputMutex;

static void sCopyFrame(const uint8_t *src, uint8_t *dest, 
                const int width, const int height, const int stride_src, const int stride_dest) {
    const int h8 = height % 8;

    for (int i = 0; i < h8; i++) {
        memcpy(dest, src, width);
        dest += stride_dest; src += stride_src;
    }

    for (int i = 0; i < height; i += 8) {
        memcpy(dest, src, width);
        dest += stride_dest; src += stride_src;
        memcpy(dest, src, width);
        dest += stride_dest; src += stride_src;
        memcpy(dest, src, width);
        dest += stride_dest; src += stride_src;
        memcpy(dest, src, width);
        dest += stride_dest; src += stride_src;
        memcpy(dest, src, width);
        dest += stride_dest; src += stride_src;
        memcpy(dest, src, width);
        dest += stride_dest; src += stride_src;
        memcpy(dest, src, width);
        dest += stride_dest; src += stride_src;
        memcpy(dest, src, width);
        dest += stride_dest; src += stride_src;
    }
}

static int sDirectCopyToSurface(uint8_t *rgb, int w, int h, 
                ANativeWindow *window) { 
    int result = 0;
    if (window != NULL) {
        ANativeWindow_Buffer buffer;
        if (ANativeWindow_lock(window, &buffer, NULL) == 0) {
            //ALOGD("sDirectCopyToSurface width = %d , height = %d, stride = %d, w = %d , h = %d", buffer.width, buffer.height, buffer.stride, w, h);
            if (buffer.stride == w) {
                memcpy(buffer.bits, rgb, (size_t) (buffer.stride * (h<buffer.height?h:buffer.height) * 4));
            } else {
                const uint8_t *src = rgb;
                const int src_w = w * 4;
                const int src_step = w * 4;
                uint8_t *dest = (uint8_t *)buffer.bits;
                const int dest_w = buffer.width * 4;
                const int dest_step = buffer.stride * 4;
                const int width = src_w < dest_w ? src_w : dest_w;
                const int height = h < buffer.height ? h : buffer.height;
                sCopyFrame(src, dest, width, height-10, src_step, dest_step);
            }
            //ALOGD("buffer width = %d , height = %d , stride = %d , format = %d ", buffer.width, buffer.height, buffer.stride, buffer.format);
            /*if(msCallBackWindow.get() != 0){
                ANativeWindow_Buffer callbuffer;
                if (ANativeWindow_lock(msCallBackWindow.get(), &callbuffer, NULL) == 0) {
                    memcpy(callbuffer.bits, buffer.bits, buffer.stride * 4 * buffer.height);
                    //ALOGD(" callbuffer width = %d , height = %d , stride = %d , format = %d ", callbuffer.width, callbuffer.height, callbuffer.stride, callbuffer.format);
                    ANativeWindow_unlockAndPost(msCallBackWindow.get());
                } 
            }*/
            //ALOGD("%s: data1 = %d, data2 = %d, data3 = %d,", __FUNCTION__, (uint8_t)rgb[0], (uint8_t)rgb[1], (uint8_t)rgb[2]);
            ANativeWindow_unlockAndPost(window);
        } else {
            result = -1;
        }

    } else {
        result = -1;
    }
    return result;
}

#define ALIGN(x, mask) ( ((x) + (mask) - 1) & ~((mask) - 1) )

#define red(x)   (((x) >> 11) & 0x1f)
#define green(x) (((x) >>  5) & 0x3f)
#define blue(x)  ( (x)        & 0x1f)

#define cc(x) \
            if(x > 200){ \
                x -= 10;  \
            } \

static void rgbToYuv420(uint8_t* rgbBuf, size_t width, size_t height, uint8_t* yPlane,
        uint8_t* crPlane, uint8_t* cbPlane, size_t chromaStep, size_t yStride, size_t chromaStride) {
    uint8_t R, G, B;
    //uint16_t color0;
    double A = 0;
    size_t index = 0;
    for (size_t j = 0; j < height; j++) {
        uint8_t* cr = crPlane;
        uint8_t* cb = cbPlane;
        uint8_t* y = yPlane;
        bool jEven = (j & 1) == 0;
        for (size_t i = 0; i < width; i++) {
            R =  rgbBuf[index++];
            G =  rgbBuf[index++];
            B =  rgbBuf[index++];
            A =  rgbBuf[index++] / 255.0;

            //R = R * A  + (1.0-A)*255;
            //G = G * A  + (1.0-A)*255;
            //B = B * A  + (1.0-A)*255;

            //cc(R)
            //cc(G)
            //cc(B)
            
            /*color0 = G << 8 | R;

            R = red(color0);
            G = green(color0);
            B = blue(color0);

            R = (R<< 3) | (R >> 2);
            G = (G<< 2) | (G >> 4);
            B = (B<< 3) | (B >> 2);*/

            *y++ = (77 * R + 150 * G +  29 * B) >> 8;
            if (jEven && (i & 1) == 0) {
                *cb = (( -43 * R - 85 * G + 128 * B) >> 8) + 128;
                *cr = (( 128 * R - 107 * G - 21 * B) >> 8) + 128;
                cr += chromaStep;
                cb += chromaStep;
            }
            // Skip alpha
            //index++;
        }
        yPlane += yStride;
        if (jEven) {
            crPlane += chromaStride;
            cbPlane += chromaStride;
        }
    }
}

static void rgbToYuv420(uint8_t* rgbBuf, size_t width, size_t height, android_ycbcr* ycbcr) {
    size_t cStep = ycbcr->chroma_step;
    size_t cStride = ycbcr->cstride;
    size_t yStride = ycbcr->ystride;
    ALOGV("%s: yStride is: %zu, cStride is: %zu, cStep is: %zu", __FUNCTION__, yStride, cStride,
            cStep);
    rgbToYuv420(rgbBuf, width, height, reinterpret_cast<uint8_t*>(ycbcr->y),
            reinterpret_cast<uint8_t*>(ycbcr->cr), reinterpret_cast<uint8_t*>(ycbcr->cb),
            cStep, yStride, cStride);
}

static status_t produceFrame(const sp<ANativeWindow>& anw,
                             uint8_t* pixelBuffer,
                             int32_t bufWidth, // Width of the pixelBuffer
                             int32_t bufHeight, // Height of the pixelBuffer
                             int32_t pixelFmt, // Format of the pixelBuffer
                             int32_t bufSize) {
    ATRACE_CALL();
    status_t err = NO_ERROR;
    ANativeWindowBuffer* anb;
    ALOGV("%s: Dequeue buffer from %p %dx%d (fmt=%x, size=%x)",
            __FUNCTION__, anw.get(), bufWidth, bufHeight, pixelFmt, bufSize);

    if (anw == 0) {
        ALOGE("%s: anw must not be NULL", __FUNCTION__);
        return BAD_VALUE;
    } else if (pixelBuffer == NULL) {
        ALOGE("%s: pixelBuffer must not be NULL", __FUNCTION__);
        return BAD_VALUE;
    } else if (bufWidth < 0) {
        ALOGE("%s: width must be non-negative", __FUNCTION__);
        return BAD_VALUE;
    } else if (bufHeight < 0) {
        ALOGE("%s: height must be non-negative", __FUNCTION__);
        return BAD_VALUE;
    } else if (bufSize < 0) {
        ALOGE("%s: bufSize must be non-negative", __FUNCTION__);
        return BAD_VALUE;
    }

    size_t width = static_cast<size_t>(bufWidth);
    size_t height = static_cast<size_t>(bufHeight);
    size_t bufferLength = static_cast<size_t>(bufSize);

    // TODO: Switch to using Surface::lock and Surface::unlockAndPost
    err = native_window_dequeue_buffer_and_wait(anw.get(), &anb);
    if (err != NO_ERROR) {
        ALOGE("%s: Failed to dequeue buffer, error %s (%d).", __FUNCTION__,
                strerror(-err), err);
        //OVERRIDE_SURFACE_ERROR(err);
        return err;
    }

    sp<GraphicBuffer> buf(GraphicBuffer::from(anb));
    uint32_t grallocBufWidth = buf->getWidth();
    uint32_t grallocBufHeight = buf->getHeight();
    uint32_t grallocBufStride = buf->getStride();
    //ALOGD("%s: Received gralloc buffer with bad dimensions %" PRIu32 "x%" PRIu32
    //        ", expecting dimensions %zu x %zu",  __FUNCTION__, grallocBufWidth,
    //        grallocBufHeight, width, height);
    width = grallocBufWidth;
    height = grallocBufHeight;
    if (grallocBufWidth != width || grallocBufHeight != height) {
        ALOGE("%s: Received gralloc buffer with bad dimensions %" PRIu32 "x%" PRIu32
                ", expecting dimensions %zu x %zu",  __FUNCTION__, grallocBufWidth,
                grallocBufHeight, width, height);
        return BAD_VALUE;
    }

    int32_t bufFmt = 0;
    err = anw->query(anw.get(), NATIVE_WINDOW_FORMAT, &bufFmt);
    if (err != NO_ERROR) {
        ALOGE("%s: Error while querying surface pixel format %s (%d).", __FUNCTION__,
                strerror(-err), err);
        //OVERRIDE_SURFACE_ERROR(err);
        return err;
    }

    uint64_t tmpSize = (pixelFmt == HAL_PIXEL_FORMAT_BLOB) ? grallocBufWidth :
            4 * grallocBufHeight * grallocBufWidth;
    if (bufFmt != pixelFmt) {
        if (bufFmt == HAL_PIXEL_FORMAT_RGBA_8888 && pixelFmt == HAL_PIXEL_FORMAT_BLOB) {
            ALOGV("%s: Using BLOB to RGBA format override.", __FUNCTION__);
            tmpSize = 4 * (grallocBufWidth + grallocBufStride * (grallocBufHeight - 1));
        } else {
            ALOGW("%s: Format mismatch in produceFrame: expecting format %#" PRIx32
                    ", but received buffer with format %#" PRIx32, __FUNCTION__, pixelFmt, bufFmt);
        }
    }
    //ALOGD("%s: Format mismatch in produceFrame: expecting format %#" PRIx32
    //                ", but received buffer with format %#" PRIx32, __FUNCTION__, pixelFmt, bufFmt);
    if (tmpSize > SIZE_MAX) {
        ALOGE("%s: Overflow calculating size, buffer with dimens %zu x %zu is absurdly large...",
                __FUNCTION__, width, height);
        return BAD_VALUE;
    }

    size_t totalSizeBytes = tmpSize;

    ALOGV("%s: Pixel format chosen: %x", __FUNCTION__, pixelFmt);
    switch(pixelFmt) {
        case HAL_PIXEL_FORMAT_YCrCb_420_SP: {
            if (bufferLength < totalSizeBytes) {
                ALOGE("%s: PixelBuffer size %zu too small for given dimensions",
                        __FUNCTION__, bufferLength);
                return BAD_VALUE;
            }
            uint8_t* img = NULL;
            ALOGV("%s: Lock buffer from %p for write", __FUNCTION__, anw.get());
            err = buf->lock(GRALLOC_USAGE_SW_WRITE_OFTEN, (void**)(&img));
            if (err != NO_ERROR) return err;

            uint8_t* yPlane = img;
            uint8_t* uPlane = img + height * width;
            uint8_t* vPlane = uPlane + 1;
            size_t chromaStep = 2;
            size_t yStride = width;
            size_t chromaStride = width;

            rgbToYuv420(pixelBuffer, width, height, yPlane,
                    uPlane, vPlane, chromaStep, yStride, chromaStride);
            break;
        }
        case HAL_PIXEL_FORMAT_YV12: {
            if (bufferLength < totalSizeBytes) {
                ALOGE("%s: PixelBuffer size %zu too small for given dimensions",
                        __FUNCTION__, bufferLength);
                return BAD_VALUE;
            }

            if ((width & 1) || (height & 1)) {
                ALOGE("%s: Dimens %zu x %zu are not divisible by 2.", __FUNCTION__, width, height);
                return BAD_VALUE;
            }

            uint8_t* img = NULL;
            ALOGV("%s: Lock buffer from %p for write", __FUNCTION__, anw.get());
            err = buf->lock(GRALLOC_USAGE_SW_WRITE_OFTEN, (void**)(&img));
            if (err != NO_ERROR) {
                ALOGE("%s: Error %s (%d) while locking gralloc buffer for write.", __FUNCTION__,
                        strerror(-err), err);
                return err;
            }

            uint32_t stride = buf->getStride();
            ALOGV("%s: stride is: %" PRIu32, __FUNCTION__, stride);
            LOG_ALWAYS_FATAL_IF(stride % 16, "Stride is not 16 pixel aligned %d", stride);

            uint32_t cStride = ALIGN(stride / 2, 16);
            size_t chromaStep = 1;

            uint8_t* yPlane = img;
            uint8_t* crPlane = img + static_cast<uint32_t>(height) * stride;
            uint8_t* cbPlane = crPlane + cStride * static_cast<uint32_t>(height) / 2;

            rgbToYuv420(pixelBuffer, width, height, yPlane,
                    crPlane, cbPlane, chromaStep, stride, cStride);
            break;
        }
        case HAL_PIXEL_FORMAT_YCbCr_420_888: {
            // Software writes with YCbCr_420_888 format are unsupported
            // by the gralloc module for now
            if (bufferLength < totalSizeBytes) {
                ALOGE("%s: PixelBuffer size %zu too small for given dimensions",
                        __FUNCTION__, bufferLength);
                return BAD_VALUE;
            }
            android_ycbcr ycbcr = android_ycbcr();
            ALOGV("%s: Lock buffer from %p for write", __FUNCTION__, anw.get());

            err = buf->lockYCbCr(GRALLOC_USAGE_SW_WRITE_OFTEN, &ycbcr);
            if (err != NO_ERROR) {
                ALOGE("%s: Failed to lock ycbcr buffer, error %s (%d).", __FUNCTION__,
                        strerror(-err), err);
                return err;
            }
            rgbToYuv420(pixelBuffer, width, height, &ycbcr);
            break;
        }
        default: {
            ALOGE("%s: Invalid pixel format in produceFrame: %x", __FUNCTION__, pixelFmt);
            return BAD_VALUE;
        }
    }

    ALOGV("%s: Unlock buffer from %p", __FUNCTION__, anw.get());
    err = buf->unlock();
    if (err != NO_ERROR) {
        ALOGE("%s: Failed to unlock buffer, error %s (%d).", __FUNCTION__, strerror(-err), err);
        return err;
    }

    ALOGV("%s: Queue buffer to %p", __FUNCTION__, anw.get());
    err = anw->queueBuffer(anw.get(), buf->getNativeBuffer(), /*fenceFd*/-1);
    if (err != NO_ERROR) {
        ALOGE("%s: Failed to queue buffer, error %s (%d).", __FUNCTION__, strerror(-err), err);
        //OVERRIDE_SURFACE_ERROR(err);
        return err;
    }
    return NO_ERROR;
}

static int sDirectCopyToCallBackSurface(uint8_t *rgb, int w, int h, 
                ANativeWindow *window) {
    int result = 0;
    if (window != NULL) {
        produceFrame(window, rgb, w, h, HAL_PIXEL_FORMAT_YCbCr_420_888, w*h*4);
    } else {
        result = -1;
    }
    return result;
}

static void sDecoder_cb(void *userdata, void *data, int dataLen, 
                int w, int h, u32 timestamp, int mediaType) {
    if (mediaType == 1) {
        //Mutex::Autolock l(mInputMutex);
        sDirectCopyToCallBackSurface((uint8_t *)data, w, h, msCallBackWindow.get());
        sDirectCopyToSurface((uint8_t *)data, w, h, msWindow.get());
    }
}

static int sReceiveVideoPacket(void *data, size_t *dataLen) {
    RTPTime delay(0.020);
    msVideoSession.BeginDataAccess();
    if (msVideoSession.GotoFirstSource()) {
        do {
            RTPPacket *packet;
            while ((packet = msVideoSession.GetNextPacket()) != 0) {
                uint8_t fu_indicator_type = packet->GetPayloadData()[0] & (uint8_t)0x1f;
                if (fu_indicator_type == 28) {
                    uint8_t flag = packet->GetPayloadData()[1] & (uint8_t)0xC0;
                    if (flag == 0x80) {
                        memcpy((uint8_t *)data, packet->GetPayloadData() + 2, packet->GetPayloadLength() - 2 );
                        *dataLen = packet->GetPayloadLength() - 2;

                    } else if (flag == 0x40) {
                        memcpy((uint8_t *)data + *dataLen, packet->GetPayloadData() + 2, packet->GetPayloadLength() - 2 );
                        *dataLen += packet->GetPayloadLength() - 2;
                        AnsyncDecoder_ReceiveData(msDecoder, data, (int)*dataLen, 0, 1);
                        *dataLen = 0;

                    } else {
                        memcpy((uint8_t *)data + *dataLen, packet->GetPayloadData() + 2, packet->GetPayloadLength() - 2 );
                        *dataLen += packet->GetPayloadLength() - 2;
                        
                    }

                } else {
                    memcpy(data, packet->GetPayloadData(), packet->GetPayloadLength());
                    *dataLen = packet->GetPayloadLength();
                    AnsyncDecoder_ReceiveData(msDecoder, data, (int)*dataLen, 0, 1);
                    *dataLen = 0;

                }
                msVideoSession.DeletePacket(packet);
            }
        } while (msVideoSession.GotoNextSource());
    }
    msVideoSession.EndDataAccess();
    RTPTime::Wait(delay);
    return 0;
}

static void thread_recv_virtualcamera(void *d) 
{
    size_t srecvLen = 0;
    size_t bufferlen = sizeof(uint8_t)*1080*1920*4;
    uint8_t* srecvData = (uint8_t*)malloc(bufferlen);
    if(srecvData == NULL)
        return ;
    ALOGD("thread_recv_virtualcamera BEGIN");
    msRecvQuit = 0;
    msDecoder = AnsyncDecoder_Create(NULL, 0, NULL, 0, NULL, sDecoder_cb);
    ALOGD("thread_recv_virtualcamera BEGIN BEGIN");
    while (!msRecvQuit) {
        //memset(srecvData, 0, bufferlen);
        sReceiveVideoPacket(srecvData, &srecvLen);
    }
    ALOGD("thread_recv_virtualcamera END");
    if(srecvData){
        free(srecvData);
        srecvData = NULL;
    }
    ALOGD("thread_recv_virtualcamera END END");
    AnsyncDecoder_Destroy(msDecoder);
    msDecoder = NULL;
    ALOGD("thread_recv_virtualcamera END END END");
}

static int sDestroyMediaSession() {
    if(msRecvThread == NULL)
        return 0;

    RTPTime delay = RTPTime(1.0);

    ALOGD("sDestroyMediaSession BEGIN");

    msVideoSession.BYEDestroy(delay, 
                "stop rtp msVideoSession", strlen("stop rtp msVideoSession"));

    msRecvQuit = 1;

    ALOGD("sDestroyMediaSession END");
    Thread_Destroy(msRecvThread);
    msRecvThread = NULL;
    ALOGD("sDestroyMediaSession END END");
    return 0;
}

#define CHECK_ERROR_JRTPLIB(status) \
    if (status < 0) { \
        ALOGE("ERROR: %s", jrtplib::RTPGetErrorString(status).c_str()); \
        exit(-1);\
    }

static int sCreateMediaSession(const uint8_t *ip) {
    if(msRecvThread != NULL)
        return 0;

    ALOGD("sCreateMediaSession 1");
    RTPSessionParams sessionparams;
    sessionparams.SetOwnTimestampUnit(1.0 / 60.0);
    sessionparams.SetAcceptOwnPackets(true);
    ALOGD("sCreateMediaSession 2");
    RTPUDPv4TransmissionParams transparams;
    transparams.SetPortbase(5000);
    transparams.SetRTPReceiveBuffer(1080*1920*4*60);
	transparams.SetRTCPReceiveBuffer(1080*1920*4*60);
    ALOGD("sCreateMediaSession 3");
    int status = msVideoSession.Create(sessionparams, &transparams);
    CHECK_ERROR_JRTPLIB(status);
    ALOGD("sCreateMediaSession 4");
    uint8_t localip[] = {ip[0], ip[1], ip[2], ip[3]};
    RTPIPv4Address addr(localip, 5000);
    ALOGD("sCreateMediaSession 5");
    status = msVideoSession.AddDestination(addr);
    CHECK_ERROR_JRTPLIB(status);
    ALOGD("sCreateMediaSession 6");
    msVideoSession.SetDefaultPayloadType(96);
    msVideoSession.SetDefaultMark(false);
    msVideoSession.SetDefaultTimestampIncrement(160);
    ALOGD("sCreateMediaSession 7");
    msRecvThread = Thread_Create(thread_recv_virtualcamera, NULL);
    Thread_Run(msRecvThread);
    ALOGD("sCreateMediaSession END");
    return 0;
}

namespace android {

VirtualCameraService::VirtualCameraService()
{

}

VirtualCameraService::~VirtualCameraService()
{

}

static int mysplit(const char *buf, uint8_t* ip)
{
    char *ctx = NULL;
    int count = 0;
    char *tok = NULL;
    char pbuf[1024] = {0};

    strcpy(pbuf, buf);

    tok = strtok_r(pbuf, ".", &ctx);
    while(tok!= NULL)
    {
        ip[count++] = atoi(tok);
        if(count >= 4) break;

        tok = strtok_r(NULL, ".", &ctx);
    }

    return count;
}

static int get_virtualcamera_ip(uint8_t* ip)
{
    int result = 0;
	int ipfd = open("/data/.virtualcameraip",O_RDONLY);
	if (ipfd >= 0) {
		char buf[256] = {0};
		int len = read(ipfd, buf, 256);
		if (len > 0) {
            result = mysplit(buf, ip);
		}
		close(ipfd);
	}else{
		ALOGD("open /data/.virtualcameraip failed!");
		ip[0]  = 192;
		ip[1]  = 168;
		ip[2]  = 1;
		ip[3]  = 8;
		result = 4;
    }

    return result;
}

status_t VirtualCameraService::createSession(const String16& sip)
{
    uint8_t ip[4] = {0};
    int count = mysplit(String8(sip).c_str(), (uint8_t*)ip);
    ALOGD("ip = %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    Mutex::Autolock l(mInputMutex);

    count = get_virtualcamera_ip(ip);
    if(count == 4){
        ALOGD("ip = %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        sCreateMediaSession(ip);
    }
    return NO_ERROR;
}

status_t VirtualCameraService::destroySession()
{
    Mutex::Autolock l(mInputMutex);

    sDestroyMediaSession();
    return NO_ERROR;
}

status_t VirtualCameraService::setSurface(const sp<IGraphicBufferProducer>& bufferProducer, int32_t width, int32_t height, int32_t format, int32_t transform)
{
    sp<Surface> window;
    status_t res;
    Mutex::Autolock l(mInputMutex);

    /*if(width == 640 && height == 480){
        msWindow = nullptr;
        return NO_ERROR;
    }*/

    if (bufferProducer != 0) {
        window = new Surface(bufferProducer, true);

        ALOGD("width = %d , height = %d , format = %d , transform = %d ", width, height, format, transform);

        ANativeWindow_Buffer buffer;
        if (ANativeWindow_lock(window.get(), &buffer, NULL) == 0) {
            ALOGD("buffer width = %d , height = %d , stride = %d , format = %d ", buffer.width, buffer.height, buffer.stride, buffer.format);

            if(buffer.width > 1) width = buffer.width;
            if(buffer.height > 1) height = buffer.height;

            ANativeWindow_setBuffersGeometry(window.get(), width, height, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM);
            ANativeWindow_unlockAndPost(window.get());
        } 

        res = native_window_set_scaling_mode(window.get(), NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW);
        if (res != OK) {
            ALOGW("%s: Unable to configure stream scaling: %s (%d)", __FUNCTION__, strerror(-res), res);
        }

        if (ANativeWindow_lock(window.get(), &buffer, NULL) == 0) {
            ALOGD("buffer width = %d , height = %d , stride = %d , format = %d ", buffer.width, buffer.height, buffer.stride, buffer.format);
            ANativeWindow_unlockAndPost(window.get());
        }
    }

    msWindow = window;
    return NO_ERROR;
}

status_t VirtualCameraService::releaseSurface()
{
    Mutex::Autolock l(mInputMutex);
    ALOGD("%s", __FUNCTION__);
    msWindow = nullptr;
    return NO_ERROR;
}

status_t VirtualCameraService::setCallBackSurface(const sp<IGraphicBufferProducer>& bufferProducer, int32_t width, int32_t height, int32_t format, int32_t transform)
{
    sp<Surface> window;
    status_t res;
    Mutex::Autolock l(mInputMutex);

    if (bufferProducer != 0) {
        window = new Surface(bufferProducer, true);

        /*if(msWindow.get() != 0){
            if (ANativeWindow_lock(msWindow.get(), &buffer, NULL) == 0) {
                ALOGD("width = %d , height = %d , stride = %d ", buffer.width, buffer.height, buffer.stride);
                width = buffer.width;
                height = buffer.height;
                ANativeWindow_unlockAndPost(msWindow.get());
            }   
        }*/

        ANativeWindow_Buffer buffer;
        if (ANativeWindow_lock(window.get(), &buffer, NULL) == 0) {
            ALOGD("CallBack width = %d , height = %d , stride = %d ", buffer.width, buffer.height, buffer.stride);
            ANativeWindow_setBuffersGeometry(window.get(), width, height, HAL_PIXEL_FORMAT_YCbCr_420_888); 
            //ANativeWindow_setBuffersTransform(window.get(), transform); 
            ANativeWindow_unlockAndPost(window.get());
        }

        /*res = native_window_api_connect(window.get(), NATIVE_WINDOW_API_CAMERA);
        if (res != NO_ERROR) {
            ALOGE("native_window_api_connect failed: %s (%d)", strerror(-res),
                    res);
            return res;
        }*/

        res = native_window_set_usage(window.get(), 0);
        if (res != OK) {
            ALOGW("%s: Unable to configure usage", __FUNCTION__);
        }

        res = native_window_set_scaling_mode(window.get(), NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW);
        if (res != OK) {
            ALOGW("%s: Unable to configure stream scaling: %s (%d)", __FUNCTION__, strerror(-res), res);
        }

        /*res = native_window_set_buffers_dimensions(window.get(), width, height);
        if (res != OK) {
            ALOGW("%s: Unable to configure stream buffer dimensions %d x %d", __FUNCTION__, width, height);
        }*/

        /*res = native_window_set_buffers_format(window.get(), HAL_PIXEL_FORMAT_YCbCr_420_888);
        if (res != OK) {
            ALOGW("%s: Unable to configure stream buffer format %#x", __FUNCTION__, format);
        }*/

        res = native_window_set_buffers_data_space(window.get(), HAL_DATASPACE_V0_JFIF);
        if (res != OK) {
            ALOGW("%s: Unable to configure stream dataspace %#x", __FUNCTION__, HAL_DATASPACE_V0_JFIF);
        }

        /*res = native_window_set_buffers_transform(window.get(), transform);
        if (res != OK) {
            ALOGW("%s: Unable to configure stream transform to %x: %s (%d)", __FUNCTION__, transform, strerror(-res), res);
        }*/

        int maxConsumerBuffers;
        res = static_cast<ANativeWindow*>(window.get())->query(
                window.get(), NATIVE_WINDOW_MIN_UNDEQUEUED_BUFFERS, &maxConsumerBuffers);
        if (res != OK) {
            ALOGW("%s: Unable to query consumer undequeued", __FUNCTION__);
        }

        ALOGD("%s: Consumer wants %d buffers, HAL wants %d", __FUNCTION__, maxConsumerBuffers, 0);
        res = native_window_set_buffer_count(window.get(), maxConsumerBuffers + 6);
        if (res != OK) {
            ALOGW("%s: Unable to set buffer count", __FUNCTION__);
        }

        ALOGD("CallBack width = %d , height = %d , format = %d , transform = %d ", width, height, format, transform);

        /*if (ANativeWindow_lock(window.get(), &buffer, NULL) == 0) {
            ALOGD("CallBack width = %d , height = %d , stride = %d ", buffer.width, buffer.height, buffer.stride);
            ANativeWindow_unlockAndPost(window.get());
        }*/
    }

    msCallBackWindow = window;
    return NO_ERROR;
}

status_t VirtualCameraService::releaseCallBackSurface()
{
    Mutex::Autolock l(mInputMutex);
    /*status_t res;
    if(msCallBackWindow != nullptr){
        res = native_window_api_disconnect(msCallBackWindow.get(),
                NATIVE_WINDOW_API_CAMERA);
        if (res) {
            ALOGW("%s: native_window_api_disconnect failed: %s (%d)",
                    __FUNCTION__, strerror(-res), res);
        }
    }*/

    ALOGD("%s", __FUNCTION__);
    msCallBackWindow = nullptr;
    return NO_ERROR;
}

};
