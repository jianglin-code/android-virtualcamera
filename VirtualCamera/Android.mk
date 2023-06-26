LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS) 
LOCAL_PREBUILT_LIBS := ffmpeg/libs/android/arm64-v8a/libavcodec.a
LOCAL_PREBUILT_LIBS += ffmpeg/libs/android/arm64-v8a/libavdevice.a
LOCAL_PREBUILT_LIBS += ffmpeg/libs/android/arm64-v8a/libavfilter.a
LOCAL_PREBUILT_LIBS += ffmpeg/libs/android/arm64-v8a/libavutil.a
LOCAL_PREBUILT_LIBS += ffmpeg/libs/android/arm64-v8a/libpostproc.a
LOCAL_PREBUILT_LIBS += ffmpeg/libs/android/arm64-v8a/libswresample.a
LOCAL_PREBUILT_LIBS += ffmpeg/libs/android/arm64-v8a/libswscale.a
LOCAL_PREBUILT_LIBS += ffmpeg/libs/android/arm64-v8a/libavformat.a
include $(BUILD_MULTI_PREBUILT) 

#
# custommade service
#
include $(CLEAR_VARS)
LOCAL_CFLAGS :=
LOCAL_SRC_FILES:= \
    main_virtualcamera.cpp  \
    VirtualCameraService.cpp  \
    IVirtualCameraService.cpp  \
    AnsyncDecoder/ff_mp4.c  \
    AnsyncDecoder/sps_pps.c  \
    AnsyncDecoder/AnsyncDecoder.c  \
    jthread/jmutex.cpp  \
    jthread/jthread.cpp  \
    Common/dtimenow.c  \
    Common/circular_list.c  \
    Common/network/NetworkSocket.c  \
    Common/thread/linux/mutex_pthread.c  \
    Common/thread/linux/thread_pthread.c  \
    Common/thread/linux/Semaphore_linux.c  \
    JRTPLIB/src/rtpdebug.cpp  \
    JRTPLIB/src/rtperrors.cpp  \
    JRTPLIB/src/rtppacket.cpp  \
    JRTPLIB/src/rtprandom.cpp  \
    JRTPLIB/src/rtcppacket.cpp  \
    JRTPLIB/src/rtpsession.cpp  \
    JRTPLIB/src/rtpsources.cpp  \
    JRTPLIB/src/rtcprrpacket.cpp  \
    JRTPLIB/src/rtcpsdesinfo.cpp  \
    JRTPLIB/src/rtcpsrpacket.cpp  \
    JRTPLIB/src/rtcpapppacket.cpp  \
    JRTPLIB/src/rtcpbyepacket.cpp  \
    JRTPLIB/src/rtcpscheduler.cpp  \
    JRTPLIB/src/rtppollthread.cpp  \
    JRTPLIB/src/rtpsourcedata.cpp  \
    JRTPLIB/src/rtptcpaddress.cpp  \
    JRTPLIB/src/rtcpsdespacket.cpp  \
    JRTPLIB/src/rtpbyteaddress.cpp  \
    JRTPLIB/src/rtpipv4address.cpp  \
    JRTPLIB/src/rtpipv6address.cpp  \
    JRTPLIB/src/rtprandomrands.cpp  \
    JRTPLIB/src/rtprandomrand48.cpp  \
    JRTPLIB/src/rtpcollisionlist.cpp  \
    JRTPLIB/src/rtppacketbuilder.cpp  \
    JRTPLIB/src/rtprandomurandom.cpp  \
    JRTPLIB/src/rtpsecuresession.cpp  \
    JRTPLIB/src/rtpsessionparams.cpp  \
    JRTPLIB/src/rtptimeutilities.cpp  \
    JRTPLIB/src/rtcppacketbuilder.cpp  \
    JRTPLIB/src/rtplibraryversion.cpp  \
    JRTPLIB/src/rtpsessionsources.cpp  \
    JRTPLIB/src/rtptcptransmitter.cpp  \
    JRTPLIB/src/rtcpcompoundpacket.cpp  \
    JRTPLIB/src/rtpipv4destination.cpp  \
    JRTPLIB/src/rtpipv6destination.cpp  \
    JRTPLIB/src/rtpabortdescriptors.cpp  \
    JRTPLIB/src/rtpudpv4transmitter.cpp  \
    JRTPLIB/src/rtpudpv6transmitter.cpp  \
    JRTPLIB/src/rtpinternalsourcedata.cpp  \
    JRTPLIB/src/rtpexternaltransmitter.cpp  \
    JRTPLIB/src/rtcpcompoundpacketbuilder.cpp  

LOCAL_MODULE := virtualcamera
LOCAL_MODULE_TAGS := optional
LOCAL_SHARED_LIBRARIES := libm libcutils libc libbinder libutils libgui liblog libnativewindow libui
LOCAL_STATIC_LIBRARIES += libavfilter
LOCAL_STATIC_LIBRARIES += libpostproc
LOCAL_STATIC_LIBRARIES += libswresample
LOCAL_STATIC_LIBRARIES += libswscale
LOCAL_STATIC_LIBRARIES += libavformat
LOCAL_STATIC_LIBRARIES += libavdevice
LOCAL_STATIC_LIBRARIES += libavcodec
LOCAL_STATIC_LIBRARIES += libavutil
LOCAL_LDFLAGS := -lz
#LOCAL_C_INCLUDES += libavcodec
#LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT_SBIN)
include $(BUILD_EXECUTABLE)
