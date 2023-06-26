/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_HARDWARE_CAMERA_DEVICE_V3_4_VIRCAMERADEVICESESSION_H
#define ANDROID_HARDWARE_CAMERA_DEVICE_V3_4_VIRCAMERADEVICESESSION_H

#include <android/hardware/camera/device/3.2/ICameraDevice.h>
#include <android/hardware/camera/device/3.4/ICameraDeviceSession.h>
#include <fmq/MessageQueue.h>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>
#include <include/convert.h>
#include <chrono>
#include <condition_variable>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include "CameraMetadata.h"
#include "HandleImporter.h"
#include "Exif.h"
#include "utils/KeyedVector.h"
#include "utils/Mutex.h"
#include "utils/Thread.h"
#include "android-base/unique_fd.h"
#include "VirtualCameraUtils_3.4.h"
#include "MpiJpegDecoder.h"
#include <utils/Singleton.h>
#include "VirtualCameraMemManager.h"
#include <linux/videodev2.h>


namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace V3_4 {
namespace virtuals {
namespace implementation {

using ::android::hardware::camera::device::V3_2::BufferCache;
using ::android::hardware::camera::device::V3_2::BufferStatus;
using ::android::hardware::camera::device::V3_2::CameraMetadata;
using ::android::hardware::camera::device::V3_2::CaptureRequest;
using ::android::hardware::camera::device::V3_2::CaptureResult;
using ::android::hardware::camera::device::V3_2::ErrorCode;
using ::android::hardware::camera::device::V3_2::ICameraDeviceCallback;
using ::android::hardware::camera::device::V3_2::MsgType;
using ::android::hardware::camera::device::V3_2::NotifyMsg;
using ::android::hardware::camera::device::V3_2::RequestTemplate;
using ::android::hardware::camera::device::V3_2::Stream;
using ::android::hardware::camera::device::V3_4::StreamConfiguration;
using ::android::hardware::camera::device::V3_2::StreamConfigurationMode;
using ::android::hardware::camera::device::V3_2::StreamRotation;
using ::android::hardware::camera::device::V3_2::StreamType;
using ::android::hardware::camera::device::V3_2::DataspaceFlags;
using ::android::hardware::camera::device::V3_2::CameraBlob;
using ::android::hardware::camera::device::V3_2::CameraBlobId;
using ::android::hardware::camera::device::V3_4::HalStreamConfiguration;
using ::android::hardware::camera::device::V3_4::ICameraDeviceSession;
using ::android::hardware::camera::common::V1_0::Status;
using ::android::hardware::camera::common::V1_0::helper::HandleImporter;
using ::android::hardware::camera::common::V1_0::helper::ExifUtils;
using ::android::hardware::camera::virtuals::common::VirtualCameraConfig;
using ::android::hardware::camera::virtuals::common::Size;
using ::android::hardware::camera::virtuals::common::SizeHasher;
using ::android::hardware::graphics::common::V1_0::BufferUsage;
using ::android::hardware::graphics::common::V1_0::Dataspace;
using ::android::hardware::graphics::common::V1_0::PixelFormat;
using ::android::hardware::kSynchronizedReadWrite;
using ::android::hardware::MessageQueue;
using ::android::hardware::MQDescriptorSync;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::hidl_vec;
using ::android::hardware::hidl_string;
using ::android::sp;
using ::android::Mutex;
using ::android::base::unique_fd;
using ::android::virtuals::bufferinfo_s;

struct VirtualCameraDeviceSession : public virtual RefBase,
        public virtual OutputThreadInterface {

    VirtualCameraDeviceSession(const sp<ICameraDeviceCallback>&,
            const VirtualCameraConfig& cfg,
            const std::vector<SupportedV4L2Format>& sortedFormats,
            const CroppingType& croppingType,
            const common::V1_0::helper::CameraMetadata& chars,
            const std::string& cameraId,
            unique_fd v4l2Fd);
    virtual ~VirtualCameraDeviceSession();
    // Call by CameraDevice to dump active device states
    void dumpState(const native_handle_t*);
    // Caller must use this method to check if CameraDeviceSession ctor failed
    bool isInitFailed();
    bool isClosed();

    // Retrieve the HIDL interface, split into its own class to avoid inheritance issues when
    // dealing with minor version revs and simultaneous implementation and interface inheritance
    virtual sp<ICameraDeviceSession> getInterface() {
        return new TrampolineSessionInterface_3_4(this);
    }

    static const int kMaxProcessedStream = 3;
    static const int kMaxStallStream = 1;
    static const uint32_t kMaxBytesPerPixel = 2;
	void createPreviewBuffer();
   static buffer_handle_t  mBufferHandle;
    class OutputThread : public android::Thread{
    public:
        OutputThread(wp<OutputThreadInterface> parent, CroppingType,
                const common::V1_0::helper::CameraMetadata&);
        virtual ~OutputThread();

        Status allocateIntermediateBuffers(
                const Size& v4lSize, const Size& thumbSize,
                const hidl_vec<Stream>& streams,
                uint32_t blobBufferSize);
        Status submitRequest(const std::shared_ptr<HalRequest>&);
        void flush();
        void dump(int fd);
        virtual bool threadLoop() override;

        void setExifMakeModel(const std::string& make, const std::string& model);

        // The remaining request list is returned for offline processing
        std::list<std::shared_ptr<HalRequest>> switchToOffline();

        bool isSubDevice(){
                auto parent = mParent.promote();
                return parent->isSubDevice();
        }

        bool isMainDevice(){
                auto parent = mParent.promote();
                return parent->isMainDevice();
        }

    protected:
        // Methods to request output buffer in parallel
        // No-op for device@3.4. Implemented in device@3.5
        virtual int requestBufferStart(const std::vector<HalStreamBuffer>&) { return 0; }
        virtual int waitForBufferRequestDone(
                /*out*/std::vector<HalStreamBuffer>*) { return 0; }

        static const int kFlushWaitTimeoutSec = 3; // 3 sec
        static const int kReqWaitTimeoutMs = 33;   // 33ms
        static const int kReqWaitTimesMax = 90;    // 33ms * 90 ~= 3 sec

        void waitForNextRequest(std::shared_ptr<HalRequest>* out);
        void signalRequestDone();

        int cropAndScaleLocked(
                sp<AllocatedFrame>& in, const Size& outSize,
                YCbCrLayout* out);

        int cropAndScaleThumbLocked(
                sp<AllocatedFrame>& in, const Size& outSize,
                YCbCrLayout* out);

        int createJpegLocked(HalStreamBuffer &halBuf,
                const common::V1_0::helper::CameraMetadata& settings);

        void clearIntermediateBuffers();

        const wp<OutputThreadInterface> mParent;
        const CroppingType mCroppingType;
        const common::V1_0::helper::CameraMetadata mCameraCharacteristics;

        mutable std::mutex mRequestListLock;      // Protect acccess to mRequestList,
                                                  // mProcessingRequest and mProcessingFrameNumer
        mutable std::mutex mFramePushListLock;
        std::condition_variable mFramePushCond;
        std::condition_variable mRequestCond;     // signaled when a new request is submitted
        std::condition_variable mRequestDoneCond; // signaled when a request is done processing
        std::list<std::shared_ptr<HalRequest>> mRequestList;
        std::list<void*> mFramePushList;
        bool mProcessingRequest = false;
        uint32_t mProcessingFrameNumer = 0;

        // V4L2 frameIn
        // (MJPG decode)-> mYu12Frame
        // (Scale)-> mScaledYu12Frames
        // (Format convert) -> output gralloc frames
        mutable std::mutex mBufferLock; // Protect access to intermediate buffers
        sp<AllocatedFrame> mYu12Frame;
        sp<AllocatedFrame> mYu12ThumbFrame;
        std::unordered_map<Size, sp<AllocatedFrame>, SizeHasher> mIntermediateBuffers;
        std::unordered_map<Size, sp<AllocatedFrame>, SizeHasher> mScaledYu12Frames;
        YCbCrLayout mYu12FrameLayout;
        YCbCrLayout mYu12ThumbFrameLayout;
        uint32_t mBlobBufferSize = 0; // 0 -> HAL derive buffer size, else: use given size

        std::string mExifMake;
        std::string mExifModel;
    };

    class FormatConvertThread : public android::Thread {
    public:
        FormatConvertThread(sp<OutputThread>& mOutputThread);
        ~FormatConvertThread();
        void createJpegDecoder();
        void destroyJpegDecoder();
        Status submitRequest(const std::shared_ptr<HalRequest>&);
        virtual bool threadLoop() override;

        sp <::android::virtuals::MemManagerBase> mCamMemManager;
    private:
        int jpegDecoder(unsigned int mShareFd, uint8_t* inData, size_t inDataSize);
        void yuyvToNv12(int v4l2_fmt_dst, char *srcbuf, char *dstbuf,
                int src_w, int src_h,int dst_w, int dst_h);
        void setOutputThread(sp<OutputThread>& mOutputThread);
        void waitForNextRequest(std::shared_ptr<HalRequest>* out);
        //void signalRequestDone();

        MpiJpegDecoder mHWJpegDecoder;
        MpiJpegDecoder::OutputFrame_t mHWDecoderFrameOut;
        sp<OutputThread> mFmtOutputThread;
        mutable std::mutex mRequestListLock;      // Protect acccess to mRequestList,
                                                  // mProcessingRequest and mProcessingFrameNumer
        mutable std::mutex mFramePushListLock;      // Protect acccess to mRequestList,
        std::condition_variable mRequestCond;     // signaled when a new request is submitted
        std::condition_variable mFramePushCond;     // signaled when a new request is submitted
        std::list<std::shared_ptr<HalRequest>> mRequestList;
        std::list<void*> mFramePushList;
        static const int kReqWaitTimeoutMs = 33;   // 33ms
        static const int kReqWaitTimesMax = 90;    // 33ms * 90 ~= 3 sec
    };

    class EventThread : public android::Thread {
    public:
        EventThread(sp<OutputThread>& mOutputThread, sp<FormatConvertThread>& mFormatConvertThread);
        ~EventThread();
        virtual bool threadLoop() override;
        int switch_flood(int isOn);
        int switch_projector(int isOn);
        int set_pro_current(void);
    private:
        void setOutputThread(sp<OutputThread>& mOutputThread);
        void setFormatConvertThread(sp<FormatConvertThread>& mFormatConvertThread);

        sp<OutputThread> mFmtOutputThread;
        sp<FormatConvertThread> mFormatConvertThread;

        unique_fd mRK803Fd;
        unique_fd mSensorFd;
        unique_fd mMipiFd;

        static const int kReqWaitTimeoutMs = 33;   // 33ms
        static const int kReqWaitTimesMax = 90;    // 33ms * 90 ~= 3 sec
    };

protected:

    // Methods from ::android::hardware::camera::device::V3_2::ICameraDeviceSession follow

    Return<void> constructDefaultRequestSettings(
            RequestTemplate,
            ICameraDeviceSession::constructDefaultRequestSettings_cb _hidl_cb);

    Return<void> configureStreams(
            const V3_2::StreamConfiguration&,
            ICameraDeviceSession::configureStreams_cb);

    Return<void> getCaptureRequestMetadataQueue(
        ICameraDeviceSession::getCaptureRequestMetadataQueue_cb);

    Return<void> getCaptureResultMetadataQueue(
        ICameraDeviceSession::getCaptureResultMetadataQueue_cb);

    Return<void> processCaptureRequest(
            const hidl_vec<CaptureRequest>&,
            const hidl_vec<BufferCache>&,
            ICameraDeviceSession::processCaptureRequest_cb);

    Return<Status> flush();
    Return<void> close(bool callerIsDtor = false);

    Return<void> configureStreams_3_3(
            const V3_2::StreamConfiguration&,
            ICameraDeviceSession::configureStreams_3_3_cb);

    Return<void> configureStreams_3_4(
            const V3_4::StreamConfiguration& requestedConfiguration,
            ICameraDeviceSession::configureStreams_3_4_cb _hidl_cb);

    Return<void> processCaptureRequest_3_4(
            const hidl_vec<V3_4::CaptureRequest>& requests,
            const hidl_vec<V3_2::BufferCache>& cachesToRemove,
            ICameraDeviceSession::processCaptureRequest_3_4_cb _hidl_cb);

protected:
    // Methods from OutputThreadInterface
    virtual Status importBuffer(int32_t streamId,
            uint64_t bufId, buffer_handle_t buf,
            /*out*/buffer_handle_t** outBufPtr,
            bool allowEmptyBuf) override;

    virtual Status processCaptureResult(std::shared_ptr<HalRequest>&) override;

    virtual Status processCaptureRequestError(const std::shared_ptr<HalRequest>&,
        /*out*/std::vector<NotifyMsg>* msgs = nullptr,
        /*out*/std::vector<CaptureResult>* results = nullptr) override;

    virtual ssize_t getJpegBufferSize(uint32_t width, uint32_t height) const override;

    virtual bool isSubDevice() const override;

    virtual bool isMainDevice() const override;

    virtual void notifyError(uint32_t frameNumber, int32_t streamId, ErrorCode ec) override;
    // End of OutputThreadInterface methods

    Status constructDefaultRequestSettingsRaw(RequestTemplate type,
            V3_2::CameraMetadata *outMetadata);

    bool initialize();
    // To init/close different version of output thread
    virtual void initOutputThread();
    virtual void closeOutputThread();
    void closeOutputThreadImpl();

    Status initStatus() const;
    status_t initDefaultRequests();
    status_t fillCaptureResult(common::V1_0::helper::CameraMetadata& md, nsecs_t timestamp);
    Status configureStreams(const V3_2::StreamConfiguration&,
            V3_3::HalStreamConfiguration* out,
            // Only filled by configureStreams_3_4, and only one blob stream supported
            uint32_t blobBufferSize = 0);
    // fps = 0.0 means default, which is
    // slowest fps that is at least 30, or fastest fps if 30 is not supported
    int configureV4l2StreamLocked(SupportedV4L2Format& fmt, double fps = 0.0);
    int v4l2StreamOffLocked();
    int setV4l2FpsLocked(double fps);
    static Status isStreamCombinationSupported(const V3_2::StreamConfiguration& config,
            const std::vector<SupportedV4L2Format>& supportedFormats,
            const VirtualCameraConfig& devCfg);

    // TODO: change to unique_ptr for better tracking
    sp<V4L2Frame> dequeueV4l2FrameLocked(/*out*/nsecs_t* shutterTs); // Called with mLock hold
    void enqueueV4l2Frame(const sp<V4L2Frame>&);

    // Check if input Stream is one of supported stream setting on this device
    static bool isSupported(const Stream& stream,
            const std::vector<SupportedV4L2Format>& supportedFormats,
            const VirtualCameraConfig& cfg);

    // Validate and import request's output buffers and acquire fence
    virtual Status importRequestLocked(
            const CaptureRequest& request,
            hidl_vec<buffer_handle_t*>& allBufPtrs,
            hidl_vec<int>& allFences);

    Status importRequestLockedImpl(
            const CaptureRequest& request,
            hidl_vec<buffer_handle_t*>& allBufPtrs,
            hidl_vec<int>& allFences,
            // Optional argument for ICameraDeviceSession@3.5 impl
            bool allowEmptyBuf = false);

    Status importBufferLocked(int32_t streamId,
            uint64_t bufId, buffer_handle_t buf,
            /*out*/buffer_handle_t** outBufPtr,
            bool allowEmptyBuf);

    static void cleanupInflightFences(
            hidl_vec<int>& allFences, size_t numFences);
    void cleanupBuffersLocked(int id);
    void updateBufferCaches(const hidl_vec<BufferCache>& cachesToRemove);

    Status processOneCaptureRequest(const CaptureRequest& request);

    void notifyShutter(uint32_t frameNumber, nsecs_t shutterTs);
    void invokeProcessCaptureResultCallback(
            hidl_vec<CaptureResult> &results, bool tryWriteFmq);

    Size getMaxJpegResolution() const;
    Size getMaxThumbResolution() const;

    int waitForV4L2BufferReturnLocked(std::unique_lock<std::mutex>& lk);

    // Protect (most of) HIDL interface methods from synchronized-entering
    mutable Mutex mInterfaceLock;

    mutable Mutex mLock; // Protect all private members except otherwise noted
    const sp<ICameraDeviceCallback> mCallback;
    const VirtualCameraConfig& mCfg;
    const common::V1_0::helper::CameraMetadata mCameraCharacteristics;
    const std::vector<SupportedV4L2Format> mSupportedFormats;
    const CroppingType mCroppingType;
    const std::string mCameraId;

    // Not protected by mLock, this is almost a const.
    // Setup in constructor, reset in close() after OutputThread is joined
    unique_fd mV4l2Fd;

    bool mSubDevice = false;
    bool mMainDevice = false;

    // device is closed either
    //    - closed by user
    //    - init failed
    //    - camera disconnected
    bool mClosed = false;
    bool mInitialized = false;
    bool mInitFail = false;
    bool mFirstRequest = false;
    common::V1_0::helper::CameraMetadata mLatestReqSetting;

    bool mV4l2Streaming = false;
    SupportedV4L2Format mV4l2StreamingFmt;
    double mV4l2StreamingFps = 0.0;
    size_t mV4L2BufferCount = 0;
    struct v4l2_plane planes[1];
    struct v4l2_capability mCapability;

    static const int kBufferWaitTimeoutSec = 3; // TODO: handle long exposure (or not allowing)
    std::mutex mV4l2BufferLock; // protect the buffer count and condition below
    std::condition_variable mV4L2BufferReturned;
    std::mutex mFramePushLock;
    std::condition_variable mFramePushed;
    size_t mNumDequeuedV4l2Buffers = 0;
    uint32_t mMaxV4L2BufferSize = 0;

    static std::mutex sSubDeviceBufferLock;
    static std::condition_variable sSubDeviceBufferPushed;

    // Not protected by mLock (but might be used when mLock is locked)
    sp<OutputThread> mOutputThread;
    sp<FormatConvertThread> mFormatConvertThread;
    sp<EventThread> mEventThread;

    // Stream ID -> Camera3Stream cache
    std::unordered_map<int, Stream> mStreamMap;

    std::mutex mInflightFramesLock; // protect mInflightFrames
    std::unordered_set<uint32_t>  mInflightFrames;

    // Stream ID -> circulating buffers map
    std::map<int, CirculatingBuffers> mCirculatingBuffers;
    // Protect mCirculatingBuffers, must not lock mLock after acquiring this lock
    mutable Mutex mCbsLock;

    std::mutex mAfTriggerLock; // protect mAfTrigger
    bool mAfTrigger = false;

    uint32_t mBlobBufferSize = 0;

    static HandleImporter sHandleImporter;

    /* Beginning of members not changed after initialize() */
    using RequestMetadataQueue = MessageQueue<uint8_t, kSynchronizedReadWrite>;
    std::unique_ptr<RequestMetadataQueue> mRequestMetadataQueue;
    using ResultMetadataQueue = MessageQueue<uint8_t, kSynchronizedReadWrite>;
    std::shared_ptr<ResultMetadataQueue> mResultMetadataQueue;

    // Protect against invokeProcessCaptureResultCallback()
    Mutex mProcessCaptureResultLock;

    std::unordered_map<RequestTemplate, CameraMetadata> mDefaultRequests;

    const Size mMaxThumbResolution;
    const Size mMaxJpegResolution;

    std::string mExifMake;
    std::string mExifModel;
    /* End of members not changed after initialize() */

private:

    struct TrampolineSessionInterface_3_4 : public ICameraDeviceSession {
        TrampolineSessionInterface_3_4(sp<VirtualCameraDeviceSession> parent) :
                mParent(parent) {}

        virtual Return<void> constructDefaultRequestSettings(
                RequestTemplate type,
                V3_3::ICameraDeviceSession::constructDefaultRequestSettings_cb _hidl_cb) override {
            return mParent->constructDefaultRequestSettings(type, _hidl_cb);
        }

        virtual Return<void> configureStreams(
                const V3_2::StreamConfiguration& requestedConfiguration,
                V3_3::ICameraDeviceSession::configureStreams_cb _hidl_cb) override {
            return mParent->configureStreams(requestedConfiguration, _hidl_cb);
        }

        virtual Return<void> processCaptureRequest(const hidl_vec<V3_2::CaptureRequest>& requests,
                const hidl_vec<V3_2::BufferCache>& cachesToRemove,
                V3_3::ICameraDeviceSession::processCaptureRequest_cb _hidl_cb) override {
            return mParent->processCaptureRequest(requests, cachesToRemove, _hidl_cb);
        }

        virtual Return<void> getCaptureRequestMetadataQueue(
                V3_3::ICameraDeviceSession::getCaptureRequestMetadataQueue_cb _hidl_cb) override  {
            return mParent->getCaptureRequestMetadataQueue(_hidl_cb);
        }

        virtual Return<void> getCaptureResultMetadataQueue(
                V3_3::ICameraDeviceSession::getCaptureResultMetadataQueue_cb _hidl_cb) override  {
            return mParent->getCaptureResultMetadataQueue(_hidl_cb);
        }

        virtual Return<Status> flush() override {
            return mParent->flush();
        }

        virtual Return<void> close() override {
            return mParent->close();
        }

        virtual Return<void> configureStreams_3_3(
                const V3_2::StreamConfiguration& requestedConfiguration,
                configureStreams_3_3_cb _hidl_cb) override {
            return mParent->configureStreams_3_3(requestedConfiguration, _hidl_cb);
        }

        virtual Return<void> configureStreams_3_4(
                const V3_4::StreamConfiguration& requestedConfiguration,
                configureStreams_3_4_cb _hidl_cb) override {
            return mParent->configureStreams_3_4(requestedConfiguration, _hidl_cb);
        }

        virtual Return<void> processCaptureRequest_3_4(const hidl_vec<V3_4::CaptureRequest>& requests,
                const hidl_vec<V3_2::BufferCache>& cachesToRemove,
                ICameraDeviceSession::processCaptureRequest_3_4_cb _hidl_cb) override {
            return mParent->processCaptureRequest_3_4(requests, cachesToRemove, _hidl_cb);
        }

    private:
        sp<VirtualCameraDeviceSession> mParent;
    };
};

}  // namespace implementation
}  // namespace V3_4
}  // namespace virtuals
}  // namespace device
}  // namespace camera
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_CAMERA_DEVICE_V3_4_VIRCAMERADEVICESESSION_H
