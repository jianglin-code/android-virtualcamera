/*
 * Copyright (C) 2013-2018 The Android Open Source Project
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

#ifndef ANDROID_SERVERS_CAMERA3DEVICE_H
#define ANDROID_SERVERS_CAMERA3DEVICE_H

#include <utility>
#include <unordered_map>
#include <set>

#include <utils/Condition.h>
#include <utils/Errors.h>
#include <utils/List.h>
#include <utils/Mutex.h>
#include <utils/Thread.h>
#include <utils/KeyedVector.h>
#include <utils/Timers.h>

#include <android/hardware/camera/device/3.2/ICameraDevice.h>
#include <android/hardware/camera/device/3.2/ICameraDeviceSession.h>
#include <android/hardware/camera/device/3.3/ICameraDeviceSession.h>
#include <android/hardware/camera/device/3.4/ICameraDeviceSession.h>
#include <android/hardware/camera/device/3.5/ICameraDeviceSession.h>
#include <android/hardware/camera/device/3.6/ICameraDeviceSession.h>
#include <android/hardware/camera/device/3.7/ICameraDeviceSession.h>
#include <android/hardware/camera/device/3.2/ICameraDeviceCallback.h>
#include <android/hardware/camera/device/3.4/ICameraDeviceCallback.h>
#include <android/hardware/camera/device/3.5/ICameraDeviceCallback.h>
#include <fmq/MessageQueue.h>

#include <camera/CaptureResult.h>

#include "common/CameraDeviceBase.h"
#include "device3/BufferUtils.h"
#include "device3/StatusTracker.h"
#include "device3/Camera3BufferManager.h"
#include "device3/DistortionMapper.h"
#include "device3/ZoomRatioMapper.h"
#include "device3/RotateAndCropMapper.h"
#include "device3/UHRCropAndMeteringRegionMapper.h"
#include "device3/InFlightRequest.h"
#include "device3/Camera3OutputInterface.h"
#include "device3/Camera3OfflineSession.h"
#include "device3/Camera3StreamInterface.h"
#include "utils/TagMonitor.h"
#include "utils/LatencyHistogram.h"
#include <camera_metadata_hidden.h>

using android::camera3::camera_capture_request_t;
using android::camera3::camera_jpeg_blob_t;
using android::camera3::camera_request_template;
using android::camera3::camera_stream_buffer_t;
using android::camera3::camera_stream_configuration_t;
using android::camera3::camera_stream_configuration_mode_t;
using android::camera3::CAMERA_TEMPLATE_COUNT;
using android::camera3::OutputStreamInfo;

namespace android {

namespace camera3 {

class Camera3Stream;
class Camera3ZslStream;
class Camera3StreamInterface;

} // namespace camera3

/**
 * CameraDevice for HAL devices with version CAMERA_DEVICE_API_VERSION_3_0 or higher.
 */
class Camera3Device :
            public CameraDeviceBase,
            virtual public hardware::camera::device::V3_5::ICameraDeviceCallback,
            public camera3::SetErrorInterface,
            public camera3::InflightRequestUpdateInterface,
            public camera3::RequestBufferInterface,
            public camera3::FlushBufferInterface {
  public:

    explicit Camera3Device(const String8& id, bool overrideForPerfClass, bool legacyClient = false);

    virtual ~Camera3Device();

    /**
     * CameraDeviceBase interface
     */

    const String8& getId() const override;

    metadata_vendor_id_t getVendorTagId() const override { return mVendorTagId; }

    // Transitions to idle state on success.
    status_t initialize(sp<CameraProviderManager> manager, const String8& monitorTags) override;
    status_t disconnect() override;
    status_t dump(int fd, const Vector<String16> &args) override;
    const CameraMetadata& info() const override;
    const CameraMetadata& infoPhysical(const String8& physicalId) const override;

    // Capture and setStreamingRequest will configure streams if currently in
    // idle state
    status_t capture(CameraMetadata &request, int64_t *lastFrameNumber = NULL) override;
    status_t captureList(const List<const PhysicalCameraSettingsList> &requestsList,
            const std::list<const SurfaceMap> &surfaceMaps,
            int64_t *lastFrameNumber = NULL) override;
    status_t setStreamingRequest(const CameraMetadata &request,
            int64_t *lastFrameNumber = NULL) override;
    status_t setStreamingRequestList(const List<const PhysicalCameraSettingsList> &requestsList,
            const std::list<const SurfaceMap> &surfaceMaps,
            int64_t *lastFrameNumber = NULL) override;
    status_t clearStreamingRequest(int64_t *lastFrameNumber = NULL) override;

    status_t waitUntilRequestReceived(int32_t requestId, nsecs_t timeout) override;

    // Actual stream creation/deletion is delayed until first request is submitted
    // If adding streams while actively capturing, will pause device before adding
    // stream, reconfiguring device, and unpausing. If the client create a stream
    // with nullptr consumer surface, the client must then call setConsumers()
    // and finish the stream configuration before starting output streaming.
    status_t createStream(sp<Surface> consumer,
            uint32_t width, uint32_t height, int format,
            android_dataspace dataSpace, camera_stream_rotation_t rotation, int *id,
            const String8& physicalCameraId,
            const std::unordered_set<int32_t> &sensorPixelModesUsed,
            std::vector<int> *surfaceIds = nullptr,
            int streamSetId = camera3::CAMERA3_STREAM_SET_ID_INVALID,
            bool isShared = false, bool isMultiResolution = false,
            uint64_t consumerUsage = 0) override;

    status_t createStream(const std::vector<sp<Surface>>& consumers,
            bool hasDeferredConsumer, uint32_t width, uint32_t height, int format,
            android_dataspace dataSpace, camera_stream_rotation_t rotation, int *id,
            const String8& physicalCameraId,
            const std::unordered_set<int32_t> &sensorPixelModesUsed,
            std::vector<int> *surfaceIds = nullptr,
            int streamSetId = camera3::CAMERA3_STREAM_SET_ID_INVALID,
            bool isShared = false, bool isMultiResolution = false,
            uint64_t consumerUsage = 0) override;

    status_t createInputStream(
            uint32_t width, uint32_t height, int format, bool isMultiResolution,
            int *id) override;

    status_t getStreamInfo(int id, StreamInfo *streamInfo) override;
    status_t setStreamTransform(int id, int transform) override;

    status_t deleteStream(int id) override;

    status_t configureStreams(const CameraMetadata& sessionParams,
            int operatingMode =
            camera_stream_configuration_mode_t::CAMERA_STREAM_CONFIGURATION_NORMAL_MODE) override;
    status_t getInputBufferProducer(
            sp<IGraphicBufferProducer> *producer) override;

    void getOfflineStreamIds(std::vector<int> *offlineStreamIds) override;

    status_t createDefaultRequest(camera_request_template_t templateId,
            CameraMetadata *request) override;

    // Transitions to the idle state on success
    status_t waitUntilDrained() override;

    status_t setNotifyCallback(wp<NotificationListener> listener) override;
    bool     willNotify3A() override;
    status_t waitForNextFrame(nsecs_t timeout) override;
    status_t getNextResult(CaptureResult *frame) override;

    status_t triggerAutofocus(uint32_t id) override;
    status_t triggerCancelAutofocus(uint32_t id) override;
    status_t triggerPrecaptureMetering(uint32_t id) override;

    status_t flush(int64_t *lastFrameNumber = NULL) override;

    status_t prepare(int streamId) override;

    status_t tearDown(int streamId) override;

    status_t addBufferListenerForStream(int streamId,
            wp<camera3::Camera3StreamBufferListener> listener) override;

    status_t prepare(int maxCount, int streamId) override;

    ssize_t getJpegBufferSize(const CameraMetadata &info, uint32_t width,
            uint32_t height) const override;
    ssize_t getPointCloudBufferSize(const CameraMetadata &info) const;
    ssize_t getRawOpaqueBufferSize(const CameraMetadata &info, int32_t width, int32_t height,
            bool maxResolution) const;

    // Methods called by subclasses
    void             notifyStatus(bool idle); // updates from StatusTracker

    /**
     * Set the deferred consumer surfaces to the output stream and finish the deferred
     * consumer configuration.
     */
    status_t setConsumerSurfaces(
            int streamId, const std::vector<sp<Surface>>& consumers,
            std::vector<int> *surfaceIds /*out*/) override;

    /**
     * Update a given stream.
     */
    status_t updateStream(int streamId, const std::vector<sp<Surface>> &newSurfaces,
            const std::vector<OutputStreamInfo> &outputInfo,
            const std::vector<size_t> &removedSurfaceIds,
            KeyedVector<sp<Surface>, size_t> *outputMap/*out*/);

    /**
     * Drop buffers for stream of streamId if dropping is true. If dropping is false, do not
     * drop buffers for stream of streamId.
     */
    status_t dropStreamBuffers(bool dropping, int streamId) override;

    nsecs_t getExpectedInFlightDuration() override;

    status_t switchToOffline(const std::vector<int32_t>& streamsToKeep,
            /*out*/ sp<CameraOfflineSessionBase>* session) override;

    // RequestBufferInterface
    bool startRequestBuffer() override;
    void endRequestBuffer() override;
    nsecs_t getWaitDuration() override;

    // FlushBufferInterface
    void getInflightBufferKeys(std::vector<std::pair<int32_t, int32_t>>* out) override;
    void getInflightRequestBufferKeys(std::vector<uint64_t>* out) override;
    std::vector<sp<camera3::Camera3StreamInterface>> getAllStreams() override;

    /**
     * Set the current behavior for the ROTATE_AND_CROP control when in AUTO.
     *
     * The value must be one of the ROTATE_AND_CROP_* values besides AUTO,
     * and defaults to NONE.
     */
    status_t setRotateAndCropAutoBehavior(
            camera_metadata_enum_android_scaler_rotate_and_crop_t rotateAndCropValue);

    /**
     * Whether camera muting (producing black-only output) is supported.
     *
     * Calling setCameraMute(true) when this returns false will return an
     * INVALID_OPERATION error.
     */
    bool supportsCameraMute();

    /**
     * Mute the camera.
     *
     * When muted, black image data is output on all output streams.
     */
    status_t setCameraMute(bool enabled);

    // Get the status trackeer for the camera device
    wp<camera3::StatusTracker> getStatusTracker() { return mStatusTracker; }

    /**
     * The injection camera session to replace the internal camera
     * session.
     */
    status_t injectCamera(const String8& injectedCamId,
                          sp<CameraProviderManager> manager);

    /**
     * Stop the injection camera and restore to internal camera session.
     */
    status_t stopInjection();

    /**
     * Helper functions to map between framework and HIDL values
     */
    static hardware::graphics::common::V1_0::PixelFormat mapToPixelFormat(int frameworkFormat);
    static hardware::camera::device::V3_2::DataspaceFlags mapToHidlDataspace(
            android_dataspace dataSpace);
    static hardware::camera::device::V3_2::BufferUsageFlags mapToConsumerUsage(uint64_t usage);
    static hardware::camera::device::V3_2::StreamRotation mapToStreamRotation(
            camera_stream_rotation_t rotation);
    // Returns a negative error code if the passed-in operation mode is not valid.
    static status_t mapToStreamConfigurationMode(camera_stream_configuration_mode_t operationMode,
            /*out*/ hardware::camera::device::V3_2::StreamConfigurationMode *mode);
    static int mapToFrameworkFormat(hardware::graphics::common::V1_0::PixelFormat pixelFormat);
    static android_dataspace mapToFrameworkDataspace(
            hardware::camera::device::V3_2::DataspaceFlags);
    static uint64_t mapConsumerToFrameworkUsage(
            hardware::camera::device::V3_2::BufferUsageFlags usage);
    static uint64_t mapProducerToFrameworkUsage(
            hardware::camera::device::V3_2::BufferUsageFlags usage);

  private:
    status_t disconnectImpl();
    static status_t removeFwkOnlyRegionKeys(CameraMetadata *request);

    // internal typedefs
    using RequestMetadataQueue = hardware::MessageQueue<uint8_t, hardware::kSynchronizedReadWrite>;

    static const size_t        kDumpLockAttempts  = 10;
    static const size_t        kDumpSleepDuration = 100000; // 0.10 sec
    static const nsecs_t       kActiveTimeout     = 500000000;  // 500 ms
    static const nsecs_t       kMinWarnInflightDuration = 5000000000; // 5 s
    static const size_t        kInFlightWarnLimit = 30;
    static const size_t        kInFlightWarnLimitHighSpeed = 256; // batch size 32 * pipe depth 8
    static const nsecs_t       kDefaultExpectedDuration = 100000000; // 100 ms
    static const nsecs_t       kMinInflightDuration = 5000000000; // 5 s
    static const nsecs_t       kBaseGetBufferWait = 3000000000; // 3 sec.
    // SCHED_FIFO priority for request submission thread in HFR mode
    static const int           kRequestThreadPriority = 1;

    struct                     RequestTrigger;
    // minimal jpeg buffer size: 256KB + blob header
    static const ssize_t       kMinJpegBufferSize = 256 * 1024 + sizeof(camera_jpeg_blob_t);
    // Constant to use for stream ID when one doesn't exist
    static const int           NO_STREAM = -1;

    // A lock to enforce serialization on the input/configure side
    // of the public interface.
    // Not locked by methods guarded by mOutputLock, since they may act
    // concurrently to the input/configure side of the interface.
    // Must be locked before mLock if both will be locked by a method
    Mutex                      mInterfaceLock;

    // The main lock on internal state
    Mutex                      mLock;

    // Camera device ID
    const String8              mId;

    // Legacy camera client flag
    bool                       mLegacyClient;

    // Current stream configuration mode;
    int                        mOperatingMode;
    // Current session wide parameters
    hardware::camera2::impl::CameraMetadataNative mSessionParams;

    // Constant to use for no set operating mode
    static const int           NO_MODE = -1;

    // Flag indicating is the current active stream configuration is constrained high speed.
    bool                       mIsConstrainedHighSpeedConfiguration;

    // FMQ to write result on. Must be guarded by mProcessCaptureResultLock.
    std::unique_ptr<ResultMetadataQueue> mResultMetadataQueue;

    /**** Scope for mLock ****/

    /**
     * Adapter for legacy HAL / HIDL HAL interface calls; calls either into legacy HALv3 or the
     * HIDL HALv3 interfaces.
     */
    class HalInterface : public camera3::Camera3StreamBufferFreedListener,
            public camera3::BufferRecordsInterface {
      public:
        HalInterface(sp<hardware::camera::device::V3_2::ICameraDeviceSession> &session,
                     std::shared_ptr<RequestMetadataQueue> queue,
                     bool useHalBufManager, bool supportOfflineProcessing);
        HalInterface(const HalInterface &other);
        HalInterface();

        // Returns true if constructed with a valid device or session, and not yet cleared
        bool valid();

        // Reset this HalInterface object (does not call close())
        void clear();

        // Calls into the HAL interface

        // Caller takes ownership of requestTemplate
        status_t constructDefaultRequestSettings(camera_request_template templateId,
                /*out*/ camera_metadata_t **requestTemplate);
        status_t configureStreams(const camera_metadata_t *sessionParams,
                /*inout*/ camera_stream_configuration_t *config,
                const std::vector<uint32_t>& bufferSizes);

        // The injection camera configures the streams to hal.
        status_t configureInjectedStreams(
                const camera_metadata_t* sessionParams,
                /*inout*/ camera_stream_configuration_t* config,
                const std::vector<uint32_t>& bufferSizes,
                const CameraMetadata& cameraCharacteristics);

        // When the call succeeds, the ownership of acquire fences in requests is transferred to
        // HalInterface. More specifically, the current implementation will send the fence to
        // HAL process and close the FD in cameraserver process. When the call fails, the ownership
        // of the acquire fence still belongs to the caller.
        status_t processBatchCaptureRequests(
                std::vector<camera_capture_request_t*>& requests,
                /*out*/uint32_t* numRequestProcessed);
        status_t flush();
        status_t dump(int fd);
        status_t close();

        void signalPipelineDrain(const std::vector<int>& streamIds);
        bool isReconfigurationRequired(CameraMetadata& oldSessionParams,
                CameraMetadata& newSessionParams);

        // Upon successful return, HalInterface will return buffer maps needed for offline
        // processing, and clear all its internal buffer maps.
        status_t switchToOffline(
                const std::vector<int32_t>& streamsToKeep,
                /*out*/hardware::camera::device::V3_6::CameraOfflineSessionInfo* offlineSessionInfo,
                /*out*/sp<hardware::camera::device::V3_6::ICameraOfflineSession>* offlineSession,
                /*out*/camera3::BufferRecords* bufferRecords);

        /////////////////////////////////////////////////////////////////////
        // Implements BufferRecordsInterface

        std::pair<bool, uint64_t> getBufferId(
                const buffer_handle_t& buf, int streamId) override;

        uint64_t removeOneBufferCache(int streamId, const native_handle_t* handle) override;

        status_t popInflightBuffer(int32_t frameNumber, int32_t streamId,
                /*out*/ buffer_handle_t **buffer) override;

        status_t pushInflightRequestBuffer(
                uint64_t bufferId, buffer_handle_t* buf, int32_t streamId) override;

        status_t popInflightRequestBuffer(uint64_t bufferId,
                /*out*/ buffer_handle_t** buffer,
                /*optional out*/ int32_t* streamId = nullptr) override;

        /////////////////////////////////////////////////////////////////////

        // Get a vector of (frameNumber, streamId) pair of currently inflight
        // buffers
        void getInflightBufferKeys(std::vector<std::pair<int32_t, int32_t>>* out);

        // Get a vector of bufferId of currently inflight buffers
        void getInflightRequestBufferKeys(std::vector<uint64_t>* out);

        void onStreamReConfigured(int streamId);

      private:
        // Always valid
        sp<hardware::camera::device::V3_2::ICameraDeviceSession> mHidlSession;
        // Valid if ICameraDeviceSession is @3.3 or newer
        sp<hardware::camera::device::V3_3::ICameraDeviceSession> mHidlSession_3_3;
        // Valid if ICameraDeviceSession is @3.4 or newer
        sp<hardware::camera::device::V3_4::ICameraDeviceSession> mHidlSession_3_4;
        // Valid if ICameraDeviceSession is @3.5 or newer
        sp<hardware::camera::device::V3_5::ICameraDeviceSession> mHidlSession_3_5;
        // Valid if ICameraDeviceSession is @3.6 or newer
        sp<hardware::camera::device::V3_6::ICameraDeviceSession> mHidlSession_3_6;
        // Valid if ICameraDeviceSession is @3.7 or newer
        sp<hardware::camera::device::V3_7::ICameraDeviceSession> mHidlSession_3_7;

        std::shared_ptr<RequestMetadataQueue> mRequestMetadataQueue;

        // The output HIDL request still depends on input camera_capture_request_t
        // Do not free input camera_capture_request_t before output HIDL request
        status_t wrapAsHidlRequest(camera_capture_request_t* in,
                /*out*/hardware::camera::device::V3_2::CaptureRequest* out,
                /*out*/std::vector<native_handle_t*>* handlesCreated,
                /*out*/std::vector<std::pair<int32_t, int32_t>>* inflightBuffers);

        status_t pushInflightBufferLocked(int32_t frameNumber, int32_t streamId,
                buffer_handle_t *buffer);

        // Pop inflight buffers based on pairs of (frameNumber,streamId)
        void popInflightBuffers(const std::vector<std::pair<int32_t, int32_t>>& buffers);

        // Return true if the input caches match what we have; otherwise false
        bool verifyBufferIds(int32_t streamId, std::vector<uint64_t>& inBufIds);

        // Delete and optionally close native handles and clear the input vector afterward
        static void cleanupNativeHandles(
                std::vector<native_handle_t*> *handles, bool closeFd = false);

        virtual void onBufferFreed(int streamId, const native_handle_t* handle) override;

        std::mutex mFreedBuffersLock;
        std::vector<std::pair<int, uint64_t>> mFreedBuffers;

        // Keep track of buffer cache and inflight buffer records
        camera3::BufferRecords mBufferRecords;

        uint32_t mNextStreamConfigCounter = 1;

        const bool mUseHalBufManager;
        bool mIsReconfigurationQuerySupported;

        const bool mSupportOfflineProcessing;
    };

    sp<HalInterface> mInterface;

    CameraMetadata             mDeviceInfo;
    bool                       mSupportNativeZoomRatio;
    std::unordered_map<std::string, CameraMetadata> mPhysicalDeviceInfoMap;

    CameraMetadata             mRequestTemplateCache[CAMERA_TEMPLATE_COUNT];

    struct Size {
        uint32_t width;
        uint32_t height;
        explicit Size(uint32_t w = 0, uint32_t h = 0) : width(w), height(h){}
    };

    enum Status {
        STATUS_ERROR,
        STATUS_UNINITIALIZED,
        STATUS_UNCONFIGURED,
        STATUS_CONFIGURED,
        STATUS_ACTIVE
    }                          mStatus;

    // Only clear mRecentStatusUpdates, mStatusWaiters from waitUntilStateThenRelock
    Vector<Status>             mRecentStatusUpdates;
    int                        mStatusWaiters;

    Condition                  mStatusChanged;

    // Tracking cause of fatal errors when in STATUS_ERROR
    String8                    mErrorCause;

    camera3::StreamSet         mOutputStreams;
    sp<camera3::Camera3Stream> mInputStream;
    bool                       mIsInputStreamMultiResolution;
    SessionStatsBuilder        mSessionStatsBuilder;
    // Map from stream group ID to physical cameras backing the stream group
    std::map<int32_t, std::set<String8>> mGroupIdPhysicalCameraMap;

    int                        mNextStreamId;
    bool                       mNeedConfig;

    int                        mFakeStreamId;

    // Whether to send state updates upstream
    // Pause when doing transparent reconfiguration
    bool                       mPauseStateNotify;

    // Need to hold on to stream references until configure completes.
    Vector<sp<camera3::Camera3StreamInterface> > mDeletedStreams;

    // Whether the HAL will send partial result
    bool                       mUsePartialResult;

    // Number of partial results that will be delivered by the HAL.
    uint32_t                   mNumPartialResults;

    /**** End scope for mLock ****/

    // The offset converting from clock domain of other subsystem
    // (video/hardware composer) to that of camera. Assumption is that this
    // offset won't change during the life cycle of the camera device. In other
    // words, camera device shouldn't be open during CPU suspend.
    nsecs_t                    mTimestampOffset;

    class CaptureRequest : public LightRefBase<CaptureRequest> {
      public:
        PhysicalCameraSettingsList          mSettingsList;
        sp<camera3::Camera3Stream>          mInputStream;
        camera_stream_buffer_t              mInputBuffer;
        camera3::Size                       mInputBufferSize;
        Vector<sp<camera3::Camera3OutputStreamInterface> >
                                            mOutputStreams;
        SurfaceMap                          mOutputSurfaces;
        CaptureResultExtras                 mResultExtras;
        // The number of requests that should be submitted to HAL at a time.
        // For example, if batch size is 8, this request and the following 7
        // requests will be submitted to HAL at a time. The batch size for
        // the following 7 requests will be ignored by the request thread.
        int                                 mBatchSize;
        //  Whether this request is from a repeating or repeating burst.
        bool                                mRepeating;
        // Whether this request has ROTATE_AND_CROP_AUTO set, so needs both
        // overriding of ROTATE_AND_CROP value and adjustment of coordinates
        // in several other controls in both the request and the result
        bool                                mRotateAndCropAuto;

        // Whether this capture request has its zoom ratio set to 1.0x before
        // the framework overrides it for camera HAL consumption.
        bool                                mZoomRatioIs1x;
        // The systemTime timestamp when the request is created.
        nsecs_t                             mRequestTimeNs;

        // Whether this capture request's distortion correction update has
        // been done.
        bool                                mDistortionCorrectionUpdated = false;
        // Whether this capture request's rotation and crop update has been
        // done.
        bool                                mRotationAndCropUpdated = false;
        // Whether this capture request's zoom ratio update has been done.
        bool                                mZoomRatioUpdated = false;
        // Whether this max resolution capture request's  crop / metering region update has been
        // done.
        bool                                mUHRCropAndMeteringRegionsUpdated = false;
    };
    typedef List<sp<CaptureRequest> > RequestList;

    status_t checkStatusOkToCaptureLocked();

    status_t convertMetadataListToRequestListLocked(
            const List<const PhysicalCameraSettingsList> &metadataList,
            const std::list<const SurfaceMap> &surfaceMaps,
            bool repeating, nsecs_t requestTimeNs,
            /*out*/
            RequestList *requestList);

    void convertToRequestList(List<const PhysicalCameraSettingsList>& requestsList,
            std::list<const SurfaceMap>& surfaceMaps,
            const CameraMetadata& request);

    status_t submitRequestsHelper(const List<const PhysicalCameraSettingsList> &requestsList,
                                  const std::list<const SurfaceMap> &surfaceMaps,
                                  bool repeating,
                                  int64_t *lastFrameNumber = NULL);


    /**
     * Implementation of android::hardware::camera::device::V3_5::ICameraDeviceCallback
     */

    hardware::Return<void> processCaptureResult_3_4(
            const hardware::hidl_vec<
                    hardware::camera::device::V3_4::CaptureResult>& results) override;
    hardware::Return<void> processCaptureResult(
            const hardware::hidl_vec<
                    hardware::camera::device::V3_2::CaptureResult>& results) override;
    hardware::Return<void> notify(
            const hardware::hidl_vec<
                    hardware::camera::device::V3_2::NotifyMsg>& msgs) override;

    hardware::Return<void> requestStreamBuffers(
            const hardware::hidl_vec<
                    hardware::camera::device::V3_5::BufferRequest>& bufReqs,
            requestStreamBuffers_cb _hidl_cb) override;

    hardware::Return<void> returnStreamBuffers(
            const hardware::hidl_vec<
                    hardware::camera::device::V3_2::StreamBuffer>& buffers) override;

    // Handle one notify message
    void notify(const hardware::camera::device::V3_2::NotifyMsg& msg);

    // lock to ensure only one processCaptureResult is called at a time.
    Mutex mProcessCaptureResultLock;

    /**
     * Common initialization code shared by both HAL paths
     *
     * Must be called with mLock and mInterfaceLock held.
     */
    status_t initializeCommonLocked();

    /**
     * Get the last request submitted to the hal by the request thread.
     *
     * Must be called with mLock held.
     */
    virtual CameraMetadata getLatestRequestLocked();

    /**
     * Update the current device status and wake all waiting threads.
     *
     * Must be called with mLock held.
     */
    void internalUpdateStatusLocked(Status status);

    /**
     * Pause processing and flush everything, but don't tell the clients.
     * This is for reconfiguring outputs transparently when according to the
     * CameraDeviceBase interface we shouldn't need to.
     * Must be called with mLock and mInterfaceLock both held.
     */
    status_t internalPauseAndWaitLocked(nsecs_t maxExpectedDuration);

    /**
     * Resume work after internalPauseAndWaitLocked()
     * Must be called with mLock and mInterfaceLock both held.
     */
    status_t internalResumeLocked();

    /**
     * Wait until status tracker tells us we've transitioned to the target state
     * set, which is either ACTIVE when active==true or IDLE (which is any
     * non-ACTIVE state) when active==false.
     *
     * Needs to be called with mLock and mInterfaceLock held.  This means there
     * can ever only be one waiter at most.
     *
     * During the wait mLock is released.
     *
     */
    status_t waitUntilStateThenRelock(bool active, nsecs_t timeout);

    /**
     * Implementation of waitUntilDrained. On success, will transition to IDLE state.
     *
     * Need to be called with mLock and mInterfaceLock held.
     */
    status_t waitUntilDrainedLocked(nsecs_t maxExpectedDuration);

    /**
     * Do common work for setting up a streaming or single capture request.
     * On success, will transition to ACTIVE if in IDLE.
     */
    sp<CaptureRequest> setUpRequestLocked(const PhysicalCameraSettingsList &request,
                                          const SurfaceMap &surfaceMap);

    /**
     * Build a CaptureRequest request from the CameraDeviceBase request
     * settings.
     */
    sp<CaptureRequest> createCaptureRequest(const PhysicalCameraSettingsList &request,
                                            const SurfaceMap &surfaceMap);

    /**
     * Internally re-configure camera device using new session parameters.
     * This will get triggered by the request thread.
     */
    bool reconfigureCamera(const CameraMetadata& sessionParams, int clientStatusId);

    /**
     * Return true in case of any output or input abandoned streams,
     * otherwise return false.
     */
    bool checkAbandonedStreamsLocked();

    /**
     * Filter stream session parameters and configure camera HAL.
     */
    status_t filterParamsAndConfigureLocked(const CameraMetadata& sessionParams,
            int operatingMode);

    /**
     * Take the currently-defined set of streams and configure the HAL to use
     * them. This is a long-running operation (may be several hundered ms).
     */
    status_t           configureStreamsLocked(int operatingMode,
            const CameraMetadata& sessionParams, bool notifyRequestThread = true);

    /**
     * Cancel stream configuration that did not finish successfully.
     */
    void               cancelStreamsConfigurationLocked();

    /**
     * Add a fake stream to the current stream set as a workaround for
     * not allowing 0 streams in the camera HAL spec.
     */
    status_t           addFakeStreamLocked();

    /**
     * Remove a fake stream if the current config includes real streams.
     */
    status_t           tryRemoveFakeStreamLocked();

    /**
     * Set device into an error state due to some fatal failure, and set an
     * error message to indicate why. Only the first call's message will be
     * used. The message is also sent to the log.
     */
    void               setErrorState(const char *fmt, ...) override;
    void               setErrorStateLocked(const char *fmt, ...) override;
    void               setErrorStateV(const char *fmt, va_list args);
    void               setErrorStateLockedV(const char *fmt, va_list args);

    /////////////////////////////////////////////////////////////////////
    // Implements InflightRequestUpdateInterface

    void onInflightEntryRemovedLocked(nsecs_t duration) override;
    void checkInflightMapLengthLocked() override;
    void onInflightMapFlushedLocked() override;

    /////////////////////////////////////////////////////////////////////

    /**
     * Debugging trylock/spin method
     * Try to acquire a lock a few times with sleeps between before giving up.
     */
    bool               tryLockSpinRightRound(Mutex& lock);

    /**
     * Helper function to get the offset between MONOTONIC and BOOTTIME
     * timestamp.
     */
    static nsecs_t getMonoToBoottimeOffset();

    struct RequestTrigger {
        // Metadata tag number, e.g. android.control.aePrecaptureTrigger
        uint32_t metadataTag;
        // Metadata value, e.g. 'START' or the trigger ID
        int32_t entryValue;

        // The last part of the fully qualified path, e.g. afTrigger
        const char *getTagName() const {
            return get_camera_metadata_tag_name(metadataTag) ?: "NULL";
        }

        // e.g. TYPE_BYTE, TYPE_INT32, etc.
        int getTagType() const {
            return get_camera_metadata_tag_type(metadataTag);
        }
    };

    /**
     * Thread for managing capture request submission to HAL device.
     */
    class RequestThread : public Thread {

      public:

        RequestThread(wp<Camera3Device> parent,
                sp<camera3::StatusTracker> statusTracker,
                sp<HalInterface> interface,
                const Vector<int32_t>& sessionParamKeys,
                bool useHalBufManager,
                bool supportCameraMute);
        ~RequestThread();

        void     setNotificationListener(wp<NotificationListener> listener);

        /**
         * Call after stream (re)-configuration is completed.
         */
        void     configurationComplete(bool isConstrainedHighSpeed,
                const CameraMetadata& sessionParams,
                const std::map<int32_t, std::set<String8>>& groupIdPhysicalCameraMap);

        /**
         * Set or clear the list of repeating requests. Does not block
         * on either. Use waitUntilPaused to wait until request queue
         * has emptied out.
         */
        status_t setRepeatingRequests(const RequestList& requests,
                                      /*out*/
                                      int64_t *lastFrameNumber = NULL);
        status_t clearRepeatingRequests(/*out*/
                                        int64_t *lastFrameNumber = NULL);

        status_t queueRequestList(List<sp<CaptureRequest> > &requests,
                                  /*out*/
                                  int64_t *lastFrameNumber = NULL);

        /**
         * Remove all queued and repeating requests, and pending triggers
         */
        status_t clear(/*out*/int64_t *lastFrameNumber = NULL);

        /**
         * Flush all pending requests in HAL.
         */
        status_t flush();

        /**
         * Queue a trigger to be dispatched with the next outgoing
         * process_capture_request. The settings for that request only
         * will be temporarily rewritten to add the trigger tag/value.
         * Subsequent requests will not be rewritten (for this tag).
         */
        status_t queueTrigger(RequestTrigger trigger[], size_t count);

        /**
         * Pause/unpause the capture thread. Doesn't block, so use
         * waitUntilPaused to wait until the thread is paused.
         */
        void     setPaused(bool paused);

        /**
         * Wait until thread processes the capture request with settings'
         * android.request.id == requestId.
         *
         * Returns TIMED_OUT in case the thread does not process the request
         * within the timeout.
         */
        status_t waitUntilRequestProcessed(int32_t requestId, nsecs_t timeout);

        /**
         * Shut down the thread. Shutdown is asynchronous, so thread may
         * still be running once this method returns.
         */
        virtual void requestExit();

        /**
         * Get the latest request that was sent to the HAL
         * with process_capture_request.
         */
        CameraMetadata getLatestRequest() const;

        /**
         * Returns true if the stream is a target of any queued or repeating
         * capture request
         */
        bool isStreamPending(sp<camera3::Camera3StreamInterface>& stream);

        /**
         * Returns true if the surface is a target of any queued or repeating
         * capture request
         */
        bool isOutputSurfacePending(int streamId, size_t surfaceId);

        // dump processCaptureRequest latency
        void dumpCaptureRequestLatency(int fd, const char* name) {
            mRequestLatency.dump(fd, name);
        }

        void signalPipelineDrain(const std::vector<int>& streamIds);
        void resetPipelineDrain();

        status_t switchToOffline(
                const std::vector<int32_t>& streamsToKeep,
                /*out*/hardware::camera::device::V3_6::CameraOfflineSessionInfo* offlineSessionInfo,
                /*out*/sp<hardware::camera::device::V3_6::ICameraOfflineSession>* offlineSession,
                /*out*/camera3::BufferRecords* bufferRecords);

        void clearPreviousRequest();

        status_t setRotateAndCropAutoBehavior(
                camera_metadata_enum_android_scaler_rotate_and_crop_t rotateAndCropValue);
        status_t setComposerSurface(bool composerSurfacePresent);

        status_t setCameraMute(int32_t muteMode);

        status_t setHalInterface(sp<HalInterface> newHalInterface);

      protected:

        virtual bool threadLoop();

      private:
        static const String8& getId(const wp<Camera3Device> &device);

        status_t           queueTriggerLocked(RequestTrigger trigger);
        // Mix-in queued triggers into this request
        int32_t            insertTriggers(const sp<CaptureRequest> &request);
        // Purge the queued triggers from this request,
        //  restoring the old field values for those tags.
        status_t           removeTriggers(const sp<CaptureRequest> &request);

        // HAL workaround: Make sure a trigger ID always exists if
        // a trigger does
        status_t           addFakeTriggerIds(const sp<CaptureRequest> &request);

        // Override rotate_and_crop control if needed; returns true if the current value was changed
        bool               overrideAutoRotateAndCrop(const sp<CaptureRequest> &request);

        // Override test_pattern control if needed for camera mute; returns true
        // if the current value was changed
        bool               overrideTestPattern(const sp<CaptureRequest> &request);

        static const nsecs_t kRequestTimeout = 50e6; // 50 ms

        // TODO: does this need to be adjusted for long exposure requests?
        static const nsecs_t kRequestSubmitTimeout = 200e6; // 200 ms

        // Used to prepare a batch of requests.
        struct NextRequest {
            sp<CaptureRequest>              captureRequest;
            camera_capture_request_t       halRequest;
            Vector<camera_stream_buffer_t> outputBuffers;
            bool                            submitted;
        };

        // Wait for the next batch of requests and put them in mNextRequests. mNextRequests will
        // be empty if it times out.
        void waitForNextRequestBatch();

        // Waits for a request, or returns NULL if times out. Must be called with mRequestLock hold.
        sp<CaptureRequest> waitForNextRequestLocked();

        // Prepare HAL requests and output buffers in mNextRequests. Return TIMED_OUT if getting any
        // output buffer timed out. If an error is returned, the caller should clean up the pending
        // request batch.
        status_t prepareHalRequests();

        // Return buffers, etc, for requests in mNextRequests that couldn't be fully constructed and
        // send request errors if sendRequestError is true. The buffers will be returned in the
        // ERROR state to mark them as not having valid data. mNextRequests will be cleared.
        void cleanUpFailedRequests(bool sendRequestError);

        // Stop the repeating request if any of its output streams is abandoned.
        void checkAndStopRepeatingRequest();

        // Release physical camera settings and camera id resources.
        void cleanupPhysicalSettings(sp<CaptureRequest> request,
                /*out*/camera_capture_request_t *halRequest);

        // Pause handling
        bool               waitIfPaused();
        void               unpauseForNewRequests();

        // Relay error to parent device object setErrorState
        void               setErrorState(const char *fmt, ...);

        // If the input request is in mRepeatingRequests. Must be called with mRequestLock hold
        bool isRepeatingRequestLocked(const sp<CaptureRequest>&);

        // Clear repeating requests. Must be called with mRequestLock held.
        status_t clearRepeatingRequestsLocked(/*out*/ int64_t *lastFrameNumber = NULL);

        // send request in mNextRequests to HAL in a batch. Return true = sucssess
        bool sendRequestsBatch();

        // Calculate the expected maximum duration for a request
        nsecs_t calculateMaxExpectedDuration(const camera_metadata_t *request);

        // Check and update latest session parameters based on the current request settings.
        bool updateSessionParameters(const CameraMetadata& settings);

        // Check whether FPS range session parameter re-configuration is needed in constrained
        // high speed recording camera sessions.
        bool skipHFRTargetFPSUpdate(int32_t tag, const camera_metadata_ro_entry_t& newEntry,
                const camera_metadata_entry_t& currentEntry);

        // Update next request sent to HAL
        void updateNextRequest(NextRequest& nextRequest);

        wp<Camera3Device>  mParent;
        wp<camera3::StatusTracker>  mStatusTracker;
        sp<HalInterface>   mInterface;

        wp<NotificationListener> mListener;

        const String8&     mId;       // The camera ID
        int                mStatusId; // The RequestThread's component ID for
                                      // status tracking

        Mutex              mRequestLock;
        Condition          mRequestSignal;
        Condition          mRequestSubmittedSignal;
        RequestList        mRequestQueue;
        RequestList        mRepeatingRequests;
        bool               mFirstRepeating;
        // The next batch of requests being prepped for submission to the HAL, no longer
        // on the request queue. Read-only even with mRequestLock held, outside
        // of threadLoop
        Vector<NextRequest> mNextRequests;

        // To protect flush() and sending a request batch to HAL.
        Mutex              mFlushLock;

        bool               mReconfigured;

        // Used by waitIfPaused, waitForNextRequest, waitUntilPaused, and signalPipelineDrain
        Mutex              mPauseLock;
        bool               mDoPause;
        Condition          mDoPauseSignal;
        bool               mPaused;
        bool               mNotifyPipelineDrain;
        std::vector<int>   mStreamIdsToBeDrained;

        sp<CaptureRequest> mPrevRequest;
        int32_t            mPrevTriggers;
        std::set<std::string> mPrevCameraIdsWithZoom;

        uint32_t           mFrameNumber;

        mutable Mutex      mLatestRequestMutex;
        Condition          mLatestRequestSignal;
        // android.request.id for latest process_capture_request
        int32_t            mLatestRequestId;
        CameraMetadata     mLatestRequest;
        std::unordered_map<std::string, CameraMetadata> mLatestPhysicalRequest;

        typedef KeyedVector<uint32_t/*tag*/, RequestTrigger> TriggerMap;
        Mutex              mTriggerMutex;
        TriggerMap         mTriggerMap;
        TriggerMap         mTriggerRemovedMap;
        TriggerMap         mTriggerReplacedMap;
        uint32_t           mCurrentAfTriggerId;
        uint32_t           mCurrentPreCaptureTriggerId;
        camera_metadata_enum_android_scaler_rotate_and_crop_t mRotateAndCropOverride;
        bool               mComposerOutput;
        int32_t            mCameraMute; // 0 = no mute, otherwise the TEST_PATTERN_MODE to use
        bool               mCameraMuteChanged;

        int64_t            mRepeatingLastFrameNumber;

        // Flag indicating if we should prepare video stream for video requests.
        bool               mPrepareVideoStream;

        bool               mConstrainedMode;

        static const int32_t kRequestLatencyBinSize = 40; // in ms
        CameraLatencyHistogram mRequestLatency;

        Vector<int32_t>    mSessionParamKeys;
        CameraMetadata     mLatestSessionParams;

        std::map<int32_t, std::set<String8>> mGroupIdPhysicalCameraMap;

        const bool         mUseHalBufManager;
        const bool         mSupportCameraMute;
    };
    sp<RequestThread> mRequestThread;

    /**
     * In-flight queue for tracking completion of capture requests.
     */
    std::mutex                    mInFlightLock;
    camera3::InFlightRequestMap   mInFlightMap;
    nsecs_t                       mExpectedInflightDuration = 0;
    int64_t                       mLastCompletedRegularFrameNumber = -1;
    int64_t                       mLastCompletedReprocessFrameNumber = -1;
    int64_t                       mLastCompletedZslFrameNumber = -1;
    // End of mInFlightLock protection scope

    int mInFlightStatusId; // const after initialize

    status_t registerInFlight(uint32_t frameNumber,
            int32_t numBuffers, CaptureResultExtras resultExtras, bool hasInput,
            bool callback, nsecs_t maxExpectedDuration,
            const std::set<std::set<String8>>& physicalCameraIds,
            bool isStillCapture, bool isZslCapture, bool rotateAndCropAuto,
            const std::set<std::string>& cameraIdsWithZoom, const SurfaceMap& outputSurfaces,
            nsecs_t requestTimeNs);

    /**
     * Tracking for idle detection
     */
    sp<camera3::StatusTracker> mStatusTracker;

    /**
     * Graphic buffer manager for output streams. Each device has a buffer manager, which is used
     * by the output streams to get and return buffers if these streams are registered to this
     * buffer manager.
     */
    sp<camera3::Camera3BufferManager> mBufferManager;

    /**
     * Thread for preparing streams
     */
    class PreparerThread : private Thread, public virtual RefBase {
      public:
        PreparerThread();
        ~PreparerThread();

        void setNotificationListener(wp<NotificationListener> listener);

        /**
         * Queue up a stream to be prepared. Streams are processed by a background thread in FIFO
         * order.  Pre-allocate up to maxCount buffers for the stream, or the maximum number needed
         * for the pipeline if maxCount is ALLOCATE_PIPELINE_MAX.
         */
        status_t prepare(int maxCount, sp<camera3::Camera3StreamInterface>& stream);

        /**
         * Cancel all current and pending stream preparation
         */
        status_t clear();

        /**
         * Pause all preparation activities
         */
        void pause();

        /**
         * Resume preparation activities
         */
        status_t resume();

      private:
        Mutex mLock;
        Condition mThreadActiveSignal;

        virtual bool threadLoop();

        // Guarded by mLock

        wp<NotificationListener> mListener;
        std::unordered_map<int, sp<camera3::Camera3StreamInterface> > mPendingStreams;
        bool mActive;
        bool mCancelNow;

        // Only accessed by threadLoop and the destructor

        sp<camera3::Camera3StreamInterface> mCurrentStream;
        int mCurrentMaxCount;
        bool mCurrentPrepareComplete;
    };
    sp<PreparerThread> mPreparerThread;

    /**
     * Output result queue and current HAL device 3A state
     */

    // Lock for output side of device
    std::mutex             mOutputLock;

    /**** Scope for mOutputLock ****/
    // the minimal frame number of the next non-reprocess result
    uint32_t               mNextResultFrameNumber;
    // the minimal frame number of the next reprocess result
    uint32_t               mNextReprocessResultFrameNumber;
    // the minimal frame number of the next ZSL still capture result
    uint32_t               mNextZslStillResultFrameNumber;
    // the minimal frame number of the next non-reprocess shutter
    uint32_t               mNextShutterFrameNumber;
    // the minimal frame number of the next reprocess shutter
    uint32_t               mNextReprocessShutterFrameNumber;
    // the minimal frame number of the next ZSL still capture shutter
    uint32_t               mNextZslStillShutterFrameNumber;
    std::list<CaptureResult>    mResultQueue;
    std::condition_variable  mResultSignal;
    wp<NotificationListener> mListener;

    /**** End scope for mOutputLock ****/

    /**** Scope for mInFlightLock ****/

    // Remove the in-flight map entry of the given index from mInFlightMap.
    // It must only be called with mInFlightLock held.
    void removeInFlightMapEntryLocked(int idx);

    // Remove all in-flight requests and return all buffers.
    // This is used after HAL interface is closed to cleanup any request/buffers
    // not returned by HAL.
    void flushInflightRequests();

    /**** End scope for mInFlightLock ****/

    /**
     * Distortion correction support
     */
    // Map from camera IDs to its corresponding distortion mapper. Only contains
    // 1 ID if the device isn't a logical multi-camera. Otherwise contains both
    // logical camera and its physical subcameras.
    std::unordered_map<std::string, camera3::DistortionMapper> mDistortionMappers;

    /**
     * Zoom ratio mapper support
     */
    std::unordered_map<std::string, camera3::ZoomRatioMapper> mZoomRatioMappers;

    /**
     * UHR request crop / metering region mapper support
     */
    std::unordered_map<std::string, camera3::UHRCropAndMeteringRegionMapper>
            mUHRCropAndMeteringRegionMappers;

    /**
     * RotateAndCrop mapper support
     */
    std::unordered_map<std::string, camera3::RotateAndCropMapper> mRotateAndCropMappers;

    // Debug tracker for metadata tag value changes
    // - Enabled with the -m <taglist> option to dumpsys, such as
    //   dumpsys -m android.control.aeState,android.control.aeMode
    // - Disabled with -m off
    // - dumpsys -m 3a is a shortcut for ae/af/awbMode, State, and Triggers
    TagMonitor mTagMonitor;

    void monitorMetadata(TagMonitor::eventSource source, int64_t frameNumber,
            nsecs_t timestamp, const CameraMetadata& metadata,
            const std::unordered_map<std::string, CameraMetadata>& physicalMetadata);

    metadata_vendor_id_t mVendorTagId;

    // Cached last requested template id
    int mLastTemplateId;

    // Synchronizes access to status tracker between inflight updates and disconnect.
    // b/79972865
    Mutex mTrackerLock;

    // Whether HAL request buffers through requestStreamBuffers API
    bool mUseHalBufManager = false;

    // Lock to ensure requestStreamBuffers() callbacks are serialized
    std::mutex mRequestBufferInterfaceLock;

    // The state machine to control when requestStreamBuffers should allow
    // HAL to request buffers.
    enum RequestBufferState {
        /**
         * This is the initial state.
         * requestStreamBuffers call will return FAILED_CONFIGURING in this state.
         * Will switch to RB_STATUS_READY after a successful configureStreams or
         * processCaptureRequest call.
         */
        RB_STATUS_STOPPED,

        /**
         * requestStreamBuffers call will proceed in this state.
         * When device is asked to stay idle via waitUntilStateThenRelock() call:
         *     - Switch to RB_STATUS_STOPPED if there is no inflight requests and
         *       request thread is paused.
         *     - Switch to RB_STATUS_PENDING_STOP otherwise
         */
        RB_STATUS_READY,

        /**
         * requestStreamBuffers call will proceed in this state.
         * Switch to RB_STATUS_STOPPED when all inflight requests are fulfilled
         * and request thread is paused
         */
        RB_STATUS_PENDING_STOP,
    };

    class RequestBufferStateMachine {
      public:
        status_t initialize(sp<camera3::StatusTracker> statusTracker);

        // Return if the state machine currently allows for requestBuffers
        // If the state allows for it, mRequestBufferOngoing will be set to true
        // and caller must call endRequestBuffer() later to unset the flag
        bool startRequestBuffer();
        void endRequestBuffer();

        // Events triggered by application API call
        void onStreamsConfigured();
        void onWaitUntilIdle();

        // Events usually triggered by hwBinder processCaptureResult callback thread
        // But can also be triggered on request thread for failed request, or on
        // hwbinder notify callback thread for shutter/error callbacks
        void onInflightMapEmpty();

        // Events triggered by RequestThread
        void onSubmittingRequest();
        void onRequestThreadPaused();

        // Events triggered by successful switchToOffline call
        // Return true is there is no ongoing requestBuffer call.
        bool onSwitchToOfflineSuccess();

      private:
        void notifyTrackerLocked(bool active);

        // Switch to STOPPED state and return true if all conditions allows for it.
        // Otherwise do nothing and return false.
        bool checkSwitchToStopLocked();

        std::mutex mLock;
        RequestBufferState mStatus = RB_STATUS_STOPPED;

        bool mRequestThreadPaused = true;
        bool mInflightMapEmpty = true;
        bool mRequestBufferOngoing = false;
        bool mSwitchedToOffline = false;

        wp<camera3::StatusTracker> mStatusTracker;
        int  mRequestBufferStatusId;
    } mRequestBufferSM;

    // Fix up result metadata for monochrome camera.
    bool mNeedFixupMonochromeTags;

    // Whether HAL supports offline processing capability.
    bool mSupportOfflineProcessing = false;

    // Whether the HAL supports camera muting via test pattern
    bool mSupportCameraMute = false;
    // Whether the HAL supports SOLID_COLOR or BLACK if mSupportCameraMute is true
    bool mSupportTestPatternSolidColor = false;

    // Whether the camera framework overrides the device characteristics for
    // performance class.
    bool mOverrideForPerfClass;

    // Injection camera related methods.
    class Camera3DeviceInjectionMethods : public virtual RefBase {
      public:
        Camera3DeviceInjectionMethods(wp<Camera3Device> parent);

        ~Camera3DeviceInjectionMethods();

        // Initialize the injection camera and generate an hal interface.
        status_t injectionInitialize(
                const String8& injectedCamId, sp<CameraProviderManager> manager,
                const sp<
                    android::hardware::camera::device::V3_2 ::ICameraDeviceCallback>&
                    callback);

        // Injection camera will replace the internal camera and configure streams
        // when device is IDLE and request thread is paused.
        status_t injectCamera(
                camera3::camera_stream_configuration& injectionConfig,
                std::vector<uint32_t>& injectionBufferSizes);

        // Stop the injection camera and switch back to backup hal interface.
        status_t stopInjection();

        bool isInjecting();

        const String8& getInjectedCamId() const;

        void getInjectionConfig(/*out*/ camera3::camera_stream_configuration* injectionConfig,
                /*out*/ std::vector<uint32_t>* injectionBufferSizes);

      private:
        // Configure the streams of injection camera, it need wait until the
        // output streams are created and configured to the original camera before
        // proceeding.
        status_t injectionConfigureStreams(
                camera3::camera_stream_configuration& injectionConfig,
                std::vector<uint32_t>& injectionBufferSizes);

        // Disconnect the injection camera and delete the hal interface.
        void injectionDisconnectImpl();

        // Use injection camera hal interface to replace and backup original
        // camera hal interface.
        status_t replaceHalInterface(sp<HalInterface> newHalInterface,
                bool keepBackup);

        wp<Camera3Device> mParent;

        // Backup of the original camera hal interface.
        sp<HalInterface> mBackupHalInterface;

        // Generated injection camera hal interface.
        sp<HalInterface> mInjectedCamHalInterface;

        // Copy the configuration of the internal camera.
        camera3::camera_stream_configuration mInjectionConfig;

        // Copy the bufferSizes of the output streams of the internal camera.
        std::vector<uint32_t> mInjectionBufferSizes;

        // Synchronizes access to injection camera between initialize and
        // disconnect.
        Mutex mInjectionLock;

        // The injection camera ID.
        String8 mInjectedCamId;
    };
    sp<Camera3DeviceInjectionMethods> mInjectionMethods;

}; // class Camera3Device

}; // namespace android

#endif
