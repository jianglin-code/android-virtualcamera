#include <jni.h>
#include <android/native_window_jni.h>

#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include <JRTPLIB/src/rtpipv4address.h>
#include <JRTPLIB/src/rtptimeutilities.h>
#include <JRTPLIB/src/rtpudpv4transmitter.h>
#include <JRTPLIB/src/rtpsessionparams.h>
#include <JRTPLIB/src/rtpsession.h>
#include <JRTPLIB/src/rtppacket.h>
#include <Common/thread/thread.h>
#include <AnsyncDecoder/AnsyncDecoder.h>
#include <display/display.h>

#include "rtpsession.h"
#include "rtpudpv4transmitter.h"
#include "rtpipv4address.h"
#include "rtpsessionparams.h"
#include "rtperrors.h"
#include "rtplibraryversion.h"

#include "fflog.h"

using namespace jrtplib;

typedef struct stJrtplibClassInfo {
    JavaVM *jvm;
    jfieldID context;
    jmethodID postEventId;
} JrtplibClassInfo;

// This function checks if there was a RTP error. If so, it displays an error message and exists.
#define CHECK_ERROR_JRTPLIB(status) \
    if (status < 0) { \
        LOGFE("ERROR: %s", jrtplib::RTPGetErrorString(status).c_str()); \
        exit(-1);\
    }

int test1() {
    RTPSession sess;
    uint16_t portbase, destport;
    uint32_t destip;
    std::string ipstr;
    int status, i, num;

    LOGFD("Using version %s", RTPLibraryVersion::GetVersion().GetVersionString().c_str());

    // First, we'll ask for the necessary information
    portbase = 6000;

    ipstr = "127.0.0.1";
    destip = inet_addr(ipstr.c_str());
    if (destip == INADDR_NONE) {
        LOGFE("Bad IP address specified");
        return -1;
    }

    // The inet_addr function returns a value in network byte order, but
    // we need the IP address in host byte order, so we use a call to
    // ntohl
    destip = ntohl(destip);
    destport = 6000;

    num = 3;

    // Now, we'll create a RTP session, set the destination, send some packets and poll for incoming data.
    RTPUDPv4TransmissionParams transparams;
    RTPSessionParams sessparams;

    // IMPORTANT: The local timestamp unit MUST be set, otherwise RTCP Sender Report info will be calculated wrong
    // In this case, we'll be sending 10 samples each second, so we'll put the timestamp unit to (1.0/10.0)
    sessparams.SetOwnTimestampUnit(1.0 / 10.0);

    sessparams.SetAcceptOwnPackets(true);
    transparams.SetPortbase(portbase);
    status = sess.Create(sessparams, &transparams);
    CHECK_ERROR_JRTPLIB(status);

    RTPIPv4Address addr(destip, destport);

    status = sess.AddDestination(addr);
    CHECK_ERROR_JRTPLIB(status);

    status = sess.SendPacket((void *) "1234567890", 10, 0, false, 10);
    CHECK_ERROR_JRTPLIB(status);

    status = sess.SendPacket((void *) "1234567890", 10, 0, false, 10);
    CHECK_ERROR_JRTPLIB(status);

    for (i = 1; i <= num; i++) {
//        printf("Sending packet %d/%d\n", i, num);

        // send the packet
//        status = sess.SendPacket((void *) "1234567890", 10, 0, false, 10);
//        checkerror(status);

//        status = sess.SendPacket((void *) "1234567890", 10, 0, false, 10);
//        checkerror(status);

        sess.BeginDataAccess();

        // check incoming packets
        if (sess.GotoFirstSourceWithData()) {
            do {
                RTPPacket *pack;
                while ((pack = sess.GetNextPacket()) != NULL) {
                    // You can examine the data here
                    printf("Got packet !\n");
                    // we don't longer need the packet, so we'll delete it
                    sess.DeletePacket(pack);
                }
            } while (sess.GotoNextSourceWithData());
        }

        sess.EndDataAccess();

#ifndef RTP_SUPPORT_THREAD
        status = sess.Poll();
        checkerror(status);
#endif // RTP_SUPPORT_THREAD

        RTPTime::Wait(RTPTime(1, 0));
    }

    sess.BYEDestroy(RTPTime(10, 0), 0, 0);
    return 1;
}

int test2() {
    RTPSession session;
    RTPSessionParams sessionparams;
    sessionparams.SetOwnTimestampUnit(1.0 / 9000.0);
    sessionparams.SetAcceptOwnPackets(true);

    RTPUDPv4TransmissionParams transparams;
    transparams.SetPortbase(8000);

    int status = session.Create(sessionparams, &transparams);
    CHECK_ERROR_JRTPLIB(status);

    uint8_t localip[] = {127, 0, 0, 1};
    RTPIPv4Address addr(localip, 8000);

    status = session.AddDestination(addr);
    CHECK_ERROR_JRTPLIB(status);

    session.SetDefaultPayloadType(96);
    session.SetDefaultMark(false);
    session.SetDefaultTimestampIncrement(160);

    uint8_t silencebuffer[16000];
    for (int i = 0; i < 160; i++) {
        silencebuffer[i] = 128;
    }

    RTPTime delay(0.020);
    RTPTime starttime = RTPTime::CurrentTime();

    status = session.SendPacketAfterSlice(silencebuffer, 2000, 96, true, 300);
    CHECK_ERROR_JRTPLIB(status);

    status = session.SendPacketAfterSlice(silencebuffer, 1000, 96, true, 300);
    CHECK_ERROR_JRTPLIB(status);

    bool done = false;
    while (!done) {
        session.BeginDataAccess();
        if (session.GotoFirstSource()) {
            do {
                RTPPacket *packet;
                while ((packet = session.GetNextPacket()) != 0) {
                    LOGFD("Got packet with extended sequence number %u from SSRC %u", packet->GetExtendedSequenceNumber(), packet->GetSSRC());
                    LOGFD("Got packet len = %zd playload len = %zd timestamp = %u", packet->GetPacketLength(), packet->GetPayloadLength(), packet->GetTimestamp());
                    session.DeletePacket(packet);
                }
            } while (session.GotoNextSource());
        }
        session.EndDataAccess();

        RTPTime::Wait(delay);

        RTPTime t = RTPTime::CurrentTime();
        t -= starttime;
        if (t > RTPTime(10.0))
            done = true;
    }
    delay = RTPTime(10.0);
    session.BYEDestroy(delay, "Time's up", 9);

    return 0;
}

int receiveVideoPacket(void *data, size_t *dataLen);
int receiveAudioPacket(void *data, size_t *dataLen);

RTPSession videoSession;
RTPSession audioSession;
uint8_t recvData[1024*1024];
size_t recvLen;
uint8_t mediaType;

Thread *recvThread;
int recvQuit;

AnsyncDecoder *decoder;
GLDisplay *glDisplay;
GLFrameData glFrameData;
ANativeWindow *window = NULL;

#define CLASS_NAME "com/forrest/jrtplib/JrtplibUtil"
jobject gObj;
JavaVM *jvm;
jmethodID postEventId;

static void java_callback_init(JNIEnv *env) {
    env->GetJavaVM(&jvm);
    jclass jcls = env->FindClass(CLASS_NAME);
    CHECK_NULL_ASSERT(jcls)
//    gClassInfo.context = (*env)->GetFieldID(env, jcls, "mNativeContext", "J");
//    CHECK_NULL_ASSERT(gClassInfo.context)
    postEventId = env->GetMethodID(jcls, "postEventFromNative", "(I[BI)V");
    CHECK_NULL_ASSERT(postEventId)
}

static void copyFrame(const uint8_t *src, uint8_t *dest, const int width, const int height, const int stride_src, const int stride_dest) {
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

// 直接将解码之后的RGB拷贝进surface, Android会自动调用SurfaceTexture.OnFrameAvailableListener
static int directCopyToSurface(uint8_t *rgb, int w, int h, ANativeWindow *window) {
    int result = 0;
    if (window != NULL) {
        ANativeWindow_Buffer buffer;
        if (ANativeWindow_lock(window, &buffer, NULL) == 0) {
            if (buffer.stride == buffer.width) { //步长与宽度相等时,直接拷贝
                memcpy(buffer.bits, rgb, (size_t) (w * h * 3));
            } else {
                const uint8_t *src = rgb;
                const int src_w = w * 3;
                const int src_step = w * 3;
                uint8_t *dest = (uint8_t *)buffer.bits;
                const int dest_w = buffer.width * 3;
                const int dest_step = buffer.stride * 3;
                const int width = src_w < dest_w ? src_w : dest_w;
                const int height = h < buffer.height ? h : buffer.height;
                copyFrame(src, dest, width, height, src_step, dest_step);
            }
            ANativeWindow_unlockAndPost(window);
        } else {
            result = -1;
        }

    } else {
        result = -1;
    }
    return result;
}

static void decoder_cb(void *userdata, void *data, int dataLen, int w, int h, u32 timestamp, int mediaType) {
    if (mediaType == 1) {
        directCopyToSurface((uint8_t *)data, w, h, window);
//        memcpy(glFrameData.data, data, (size_t)w*h*3);
//        glFrameData.width = w;
//        glFrameData.height = h;
//        JNIEnv *env;
//        jvm->AttachCurrentThread(&env, NULL);
//        env->CallVoidMethod(gObj, postEventId, 1, NULL, 0);
//        jvm->DetachCurrentThread();

    } else if (mediaType == 2) {
        JNIEnv *env;
        jvm->AttachCurrentThread(&env, NULL);
        jbyteArray audioArray = env->NewByteArray(dataLen);
        env->SetByteArrayRegion(audioArray, 0, dataLen, (jbyte *)data);
        env->CallVoidMethod(gObj, postEventId, 2, audioArray, dataLen);
        env->DeleteLocalRef(audioArray);
        jvm->DetachCurrentThread();
    }
}

static void thread_recv_data(void *d) {
    recvQuit = 0;
    decoder = AnsyncDecoder_Create(NULL, 0, NULL, 0, NULL, decoder_cb);
    while (!recvQuit) {
        receiveAudioPacket(recvData, &recvLen);
        receiveVideoPacket(recvData, &recvLen);
    }
    AnsyncDecoder_Destroy(decoder);
    decoder = NULL;
}

int createMediaSession(const uint8_t *ip) {
    // 视频发送接收端口
    RTPSessionParams sessionparams;
    sessionparams.SetOwnTimestampUnit(1.0 / 9000.0);
    sessionparams.SetAcceptOwnPackets(true);

    RTPUDPv4TransmissionParams transparams;
    transparams.SetPortbase(5000);

    int status = videoSession.Create(sessionparams, &transparams);
    CHECK_ERROR_JRTPLIB(status);
//
    uint8_t localip[] = {ip[0], ip[1], ip[2], ip[3]};
    RTPIPv4Address addr(localip, 5000);

    status = videoSession.AddDestination(addr);
    CHECK_ERROR_JRTPLIB(status);

    videoSession.SetDefaultPayloadType(96);
    videoSession.SetDefaultMark(false);
    videoSession.SetDefaultTimestampIncrement(0);

    // 音频发送接收端口
    RTPSessionParams sessionparams2;
    sessionparams2.SetOwnTimestampUnit(1.0 / 9000.0);
    sessionparams2.SetAcceptOwnPackets(true);

    RTPUDPv4TransmissionParams transparams2;
    transparams2.SetPortbase(5100);
    status = audioSession.Create(sessionparams2, &transparams2);
    CHECK_ERROR_JRTPLIB(status);

    RTPIPv4Address addr2(localip, 5100);
    status = audioSession.AddDestination(addr2);
    CHECK_ERROR_JRTPLIB(status);

    audioSession.SetDefaultPayloadType(96);
    audioSession.SetDefaultMark(false);
    audioSession.SetDefaultTimestampIncrement(0);

    recvThread = Thread_Create(thread_recv_data, NULL);
    Thread_Run(recvThread);

    return 0;
}

int destroyMediaSession() {
    RTPTime delay = RTPTime(2.0);
    videoSession.BYEDestroy(delay, "stop rtp videoSession", strlen("stop rtp videoSession"));
    audioSession.BYEDestroy(delay, "stop rtp audioSession", strlen("stop rtp audioSession"));
    recvQuit = 1;
    Thread_Destroy(recvThread);
    if (glFrameData.data != NULL) {
        free(glFrameData.data);
        glFrameData.data = NULL;
    }
    return 0;
}

// type = 1 video; type = 2 audio
int sendMediaPacket(const void *data, size_t len, int type) {
    if (type == 1) {
        videoSession.SendPacketAfterSlice(data, len, 96, true, 10);
    } else if (type == 2) {
        audioSession.SendPacket(data, len, 96, true, 10);
    }
    return 0;
}

int receiveVideoPacket(void *data, size_t *dataLen) {
    RTPTime delay(0.020);
    videoSession.BeginDataAccess();
    if (videoSession.GotoFirstSource()) {
        do {
            RTPPacket *packet;
            while ((packet = videoSession.GetNextPacket()) != 0) {
//                LOGFD("Got packet with extended sequence number %u from SSRC %u", packet->GetExtendedSequenceNumber(), packet->GetSSRC());
//                LOGFD("Got packet len = %zd playload len = %zd  type(%u) timestamp = %u",
//                        packet->GetPacketLength(), packet->GetPayloadLength(), packet->GetPayloadType(), packet->GetTimestamp());

                uint8_t fu_indicator_type = packet->GetPayloadData()[0] & (uint8_t)0x1f;
                if (fu_indicator_type == 28) { // 分片包 FU_A
                    uint8_t flag = packet->GetPayloadData()[1] & (uint8_t)0xC0;
                    if (flag == 0x80) {
                        memcpy((uint8_t *)data, packet->GetPayloadData() + 2, packet->GetPayloadLength() - 2 );
                        *dataLen = packet->GetPayloadLength() - 2;
//                        LOGFD("切片RTP包开始 timestamp(%u)", packet->GetTimestamp());

                    } else if (flag == 0x40) {
                        memcpy((uint8_t *)data + *dataLen, packet->GetPayloadData() + 2, packet->GetPayloadLength() - 2 );
                        *dataLen += packet->GetPayloadLength() - 2;
//                        LOGFD("切片RTP包结束 dataLen(%d) timestamp(%u)", *dataLen, packet->GetTimestamp());
                        AnsyncDecoder_ReceiveData(decoder, data, (int)*dataLen, 0, 1);

                    } else {
                        memcpy((uint8_t *)data + *dataLen, packet->GetPayloadData() + 2, packet->GetPayloadLength() - 2 );
                        *dataLen += packet->GetPayloadLength() - 2;
                    }

                } else { // 单个包 SPS:7 PPS:8 I:5 P:1
                    memcpy(data, packet->GetPayloadData(), packet->GetPayloadLength());
                    *dataLen = packet->GetPayloadLength();
                    AnsyncDecoder_ReceiveData(decoder, data, (int)*dataLen, 0, 1);
//                    LOGFD("单个RTP包 dataLen(%d) timestamp = %u", *dataLen, packet->GetTimestamp());
                }
                videoSession.DeletePacket(packet);
            }
        } while (videoSession.GotoNextSource());
    }
    videoSession.EndDataAccess();
    RTPTime::Wait(delay);
    return 0;
}

int receiveAudioPacket(void *data, size_t *dataLen) {
    RTPTime delay(0.020);
    audioSession.BeginDataAccess();
    if (audioSession.GotoFirstSource()) {
        do {
            RTPPacket *packet;
            while ((packet = audioSession.GetNextPacket()) != 0) {
//                LOGFD("Got packet with extended sequence number %u from SSRC %u", packet->GetExtendedSequenceNumber(), packet->GetSSRC());
//                LOGFD("Got packet len = %zd playload len = %zd  type(%u) timestamp = %u",
//                       packet->GetPacketLength(), packet->GetPayloadLength(), packet->GetPayloadType(), packet->GetTimestamp());

                memcpy(data, packet->GetPayloadData(), packet->GetPayloadLength());
                *dataLen = packet->GetPayloadLength();
                AnsyncDecoder_ReceiveData(decoder, data, (int)*dataLen, 0, 2);
//                LOGFD("单个 Audio RTP包 dataLen(%d) timestamp = %u", *dataLen, packet->GetTimestamp());
                audioSession.DeletePacket(packet);
            }
        } while (audioSession.GotoNextSource());
    }
    audioSession.EndDataAccess();
    RTPTime::Wait(delay);
    return 0;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_forrest_jrtplib_JrtplibUtil_createSendSession(JNIEnv *env, jobject instance, jbyteArray ip_) {
    BEGIN
    gObj = env->NewGlobalRef(instance);
    java_callback_init(env);

    jbyte *ip = env->GetByteArrayElements(ip_, NULL);
    createMediaSession((const uint8_t *) ip);
    env->ReleaseByteArrayElements(ip_, ip, 0);
    END
}

extern "C"
JNIEXPORT void JNICALL
Java_com_forrest_jrtplib_JrtplibUtil_destroySendSession(JNIEnv *env, jobject instance) {
    BEGIN
    destroyMediaSession();
    END
}

extern "C"
JNIEXPORT void JNICALL
Java_com_forrest_jrtplib_JrtplibUtil_sendData(JNIEnv *env, jobject instance, jbyteArray data_, jint dataLen, jint dataType) {
    jbyte *data = env->GetByteArrayElements(data_, NULL);
    sendMediaPacket(data, (size_t) dataLen, dataType);
    env->ReleaseByteArrayElements(data_, data, 0);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_forrest_jrtplib_JrtplibUtil_receiveData(JNIEnv *env, jobject instance) {
    receiveVideoPacket(recvData, &recvLen);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_forrest_jrtplib_JrtplibUtil_displayInit(JNIEnv *env, jobject instance) {
    glDisplay = display_init();
    glFrameData.data = (unsigned char *)malloc(1280 * 720 * 3);
    memset(glFrameData.data, 0, 1280 * 720 * 3);
    glFrameData.width = 0;
    glFrameData.height = 0;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_forrest_jrtplib_JrtplibUtil_displayDestroy(JNIEnv *env, jobject instance) {
    display_shutdown(glDisplay);
    glDisplay = NULL;
    free(glFrameData.data);
    glFrameData.data = NULL;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_forrest_jrtplib_JrtplibUtil_displayDraw(JNIEnv *env, jobject instance, jint x, jint y, jint w, jint h) {
    display_draw(glDisplay, &glFrameData, x, y, w, h);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_forrest_jrtplib_JrtplibUtil_setSurface(JNIEnv *env, jobject instance, jobject surface) {
    ANativeWindow *preview_window = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    window = preview_window;
    ANativeWindow_setBuffersGeometry(window, 1280, 720, AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM); //WINDOW_FORMAT_RGBX_8888
}

extern "C"
JNIEXPORT void JNICALL
Java_com_forrest_jrtplib_JrtplibUtil_releaseSurface(JNIEnv *env, jobject instance) {
    if (window) {
        ANativeWindow_release(window);
        window = NULL;
    }
}

