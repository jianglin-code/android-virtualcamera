/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef ANDROID_SERVERS_CAMERA3OFFLINESESSION_H
#define ANDROID_SERVERS_CAMERA3OFFLINESESSION_H

#include <memory>
#include <mutex>

#include <utils/String8.h>
#include <utils/String16.h>

#include <android/hardware/camera/device/3.6/ICameraOfflineSession.h>

#include <fmq/MessageQueue.h>

#include "common/CameraOfflineSessionBase.h"

#include "device3/Camera3BufferManager.h"
#include "device3/DistortionMapper.h"
#include "device3/InFlightRequest.h"
#include "device3/Camera3OutputUtils.h"
#include "device3/RotateAndCropMapper.h"
#include "device3/ZoomRatioMapper.h"
#include "utils/TagMonitor.h"
#include <camera_metadata_hidden.h>

namespace android {

namespace camera3 {

class Camera3Stream;
class Camera3OutputStreamInterface;
class Camera3StreamInterface;

} // namespace camera3


// An immutable struct containing general states that will be copied from Camera3Device to
// Camera3OfflineSession
struct Camera3OfflineStates {
    Camera3OfflineStates(
            const TagMonitor& tagMonitor, const metadata_vendor_id_t vendorTagId,
            const bool useHalBufManager, const bool needFixupMonochromeTags,
            const bool usePartialResult, const uint32_t numPartialResults,
            const int64_t lastCompletedRegularFN, const int64_t lastCompletedReprocessFN,
            const int64_t lastCompletedZslFN, const uint32_t nextResultFN,
            const uint32_t nextReprocResultFN, const uint32_t nextZslResultFN,
            const uint32_t nextShutterFN, const uint32_t nextReprocShutterFN,
            const uint32_t nextZslShutterFN, const CameraMetadata& deviceInfo,
            const std::unordered_map<std::string, CameraMetadata>& physicalDeviceInfoMap,
            const std::unordered_map<std::string, camera3::DistortionMapper>& distortionMappers,
            const std::unordered_map<std::string, camera3::ZoomRatioMapper>& zoomRatioMappers,
            const std::unordered_map<std::string, camera3::RotateAndCropMapper>&
                rotateAndCropMappers) :
            mTagMonitor(tagMonitor), mVendorTagId(vendorTagId),
            mUseHalBufManager(useHalBufManager), mNeedFixupMonochromeTags(needFixupMonochromeTags),
            mUsePartialResult(usePartialResult), mNumPartialResults(numPartialResults),
            mLastCompletedRegularFrameNumber(lastCompletedRegularFN),
            mLastCompletedReprocessFrameNumber(lastCompletedReprocessFN),
            mLastCompletedZslFrameNumber(lastCompletedZslFN),
            mNextResultFrameNumber(nextResultFN),
            mNextReprocessResultFrameNumber(nextReprocResultFN),
            mNextZslStillResultFrameNumber(nextZslResultFN),
            mNextShutterFrameNumber(nextShutterFN),
            mNextReprocessShutterFrameNumber(nextReprocShutterFN),
            mNextZslStillShutterFrameNumber(nextZslShutterFN),
            mDeviceInfo(deviceInfo),
            mPhysicalDeviceInfoMap(physicalDeviceInfoMap),
            mDistortionMappers(distortionMappers),
            mZoomRatioMappers(zoomRatioMappers),
            mRotateAndCropMappers(rotateAndCropMappers) {}

    const TagMonitor& mTagMonitor;
    const metadata_vendor_id_t mVendorTagId;

    const bool mUseHalBufManager;
    const bool mNeedFixupMonochromeTags;

    const bool mUsePartialResult;
    const uint32_t mNumPartialResults;

    // The last completed (buffers, result metadata, and error notify) regular
    // request frame number
    const int64_t mLastCompletedRegularFrameNumber;
    // The last completed (buffers, result metadata, and error notify) reprocess
    // request frame number
    const int64_t mLastCompletedReprocessFrameNumber;
    // The last completed (buffers, result metadata, and error notify) zsl
    // request frame number
    const int64_t mLastCompletedZslFrameNumber;
    // the minimal frame number of the next non-reprocess result
    const uint32_t mNextResultFrameNumber;
    // the minimal frame number of the next reprocess result
    const uint32_t mNextReprocessResultFrameNumber;
    // the minimal frame number of the next ZSL still capture result
    const uint32_t mNextZslStillResultFrameNumber;
    // the minimal frame number of the next non-reprocess shutter
    const uint32_t mNextShutterFrameNumber;
    // the minimal frame number of the next reprocess shutter
    const uint32_t mNextReprocessShutterFrameNumber;
    // the minimal frame number of the next ZSL still capture shutter
    const uint32_t mNextZslStillShutterFrameNumber;

    const CameraMetadata& mDeviceInfo;

    const std::unordered_map<std::string, CameraMetadata>& mPhysicalDeviceInfoMap;

    const std::unordered_map<std::string, camera3::DistortionMapper>& mDistortionMappers;

    const std::unordered_map<std::string, camera3::ZoomRatioMapper>& mZoomRatioMappers;

    const std::unordered_map<std::string, camera3::RotateAndCropMapper>& mRotateAndCropMappers;
};

/**
 * Camera3OfflineSession for offline session defined in HIDL ICameraOfflineSession@3.6 or higher
 */
class Camera3OfflineSession :
            public CameraOfflineSessionBase,
            virtual public hardware::camera::device::V3_5::ICameraDeviceCallback,
            public camera3::SetErrorInterface,
            public camera3::InflightRequestUpdateInterface,
            public camera3::RequestBufferInterface,
            public camera3::FlushBufferInterface {
  public:

    // initialize by Camera3Device.
    explicit Camera3OfflineSession(const String8& id,
            const sp<camera3::Camera3Stream>& inputStream,
            const camera3::StreamSet& offlineStreamSet,
            camera3::BufferRecords&& bufferRecords,
            const camera3::InFlightRequestMap& offlineReqs,
            const Camera3OfflineStates& offlineStates,
            sp<hardware::camera::device::V3_6::ICameraOfflineSession> offlineSession);

    virtual ~Camera3OfflineSession();

    virtual status_t initialize(wp<NotificationListener> listener) override;

    /**
     * CameraOfflineSessionBase interface
     */
    status_t disconnect() override;
    status_t dump(int fd) override;

    /**
     * FrameProducer interface
     */
    const String8& getId() const override;
    const CameraMetadata& info() const override;
    status_t waitForNextFrame(nsecs_t timeout) override;
    status_t getNextResult(CaptureResult *frame) override;

    // TODO: methods for notification (error/idle/finished etc) passing

    /**
     * End of CameraOfflineSessionBase interface
     */

    /**
     * HIDL ICameraDeviceCallback interface
     */

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

    /**
     * End of CameraOfflineSessionBase interface
     */

  private:
    // Camera device ID
    const String8 mId;
    sp<camera3::Camera3Stream> mInputStream;
    camera3::StreamSet mOutputStreams;
    camera3::BufferRecords mBufferRecords;
    SessionStatsBuilder mSessionStatsBuilder;

    std::mutex mOfflineReqsLock;
    camera3::InFlightRequestMap mOfflineReqs;

    sp<hardware::camera::device::V3_6::ICameraOfflineSession> mSession;

    TagMonitor mTagMonitor;
    const metadata_vendor_id_t mVendorTagId;

    const bool mUseHalBufManager;
    const bool mNeedFixupMonochromeTags;

    const bool mUsePartialResult;
    const uint32_t mNumPartialResults;

    std::mutex mOutputLock;
    std::list<CaptureResult> mResultQueue;
    std::condition_variable mResultSignal;
    // the last completed frame number of regular requests
    int64_t mLastCompletedRegularFrameNumber;
    // the last completed frame number of reprocess requests
    int64_t mLastCompletedReprocessFrameNumber;
    // the last completed frame number of ZSL still capture requests
    int64_t mLastCompletedZslFrameNumber;
    // the minimal frame number of the next non-reprocess result
    uint32_t mNextResultFrameNumber;
    // the minimal frame number of the next reprocess result
    uint32_t mNextReprocessResultFrameNumber;
    // the minimal frame number of the next ZSL still capture result
    uint32_t mNextZslStillResultFrameNumber;
    // the minimal frame number of the next non-reprocess shutter
    uint32_t mNextShutterFrameNumber;
    // the minimal frame number of the next reprocess shutter
    uint32_t mNextReprocessShutterFrameNumber;
    // the minimal frame number of the next ZSL still capture shutter
    uint32_t mNextZslStillShutterFrameNumber;
    // End of mOutputLock scope

    const CameraMetadata mDeviceInfo;
    std::unordered_map<std::string, CameraMetadata> mPhysicalDeviceInfoMap;

    std::unordered_map<std::string, camera3::DistortionMapper> mDistortionMappers;

    std::unordered_map<std::string, camera3::ZoomRatioMapper> mZoomRatioMappers;

    std::unordered_map<std::string, camera3::RotateAndCropMapper> mRotateAndCropMappers;

    mutable std::mutex mLock;

    enum Status {
        STATUS_UNINITIALIZED = 0,
        STATUS_ACTIVE,
        STATUS_ERROR,
        STATUS_CLOSED
    } mStatus;

    wp<NotificationListener> mListener;
    // End of mLock protect scope

    std::mutex mProcessCaptureResultLock;
    // FMQ to write result on. Must be guarded by mProcessCaptureResultLock.
    std::unique_ptr<ResultMetadataQueue> mResultMetadataQueue;

    // Tracking cause of fatal errors when in STATUS_ERROR
    String8 mErrorCause;

    // Lock to ensure requestStreamBuffers() callbacks are serialized
    std::mutex mRequestBufferInterfaceLock;
    // allow request buffer until all requests are processed or disconnectImpl is called
    bool mAllowRequestBuffer = true;

    // For client methods such as disconnect/dump
    std::mutex mInterfaceLock;

    // SetErrorInterface
    void setErrorState(const char *fmt, ...) override;
    void setErrorStateLocked(const char *fmt, ...) override;

    // InflightRequestUpdateInterface
    void onInflightEntryRemovedLocked(nsecs_t duration) override;
    void checkInflightMapLengthLocked() override;
    void onInflightMapFlushedLocked() override;

    // RequestBufferInterface
    bool startRequestBuffer() override;
    void endRequestBuffer() override;
    nsecs_t getWaitDuration() override;

    // FlushBufferInterface
    void getInflightBufferKeys(std::vector<std::pair<int32_t, int32_t>>* out) override;
    void getInflightRequestBufferKeys(std::vector<uint64_t>* out) override;
    std::vector<sp<camera3::Camera3StreamInterface>> getAllStreams() override;

    void setErrorStateLockedV(const char *fmt, va_list args);

    status_t disconnectImpl();
}; // class Camera3OfflineSession

}; // namespace android

#endif
