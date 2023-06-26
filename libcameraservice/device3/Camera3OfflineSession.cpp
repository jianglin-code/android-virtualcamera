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

#define LOG_TAG "Camera3-OffLnSsn"
#define ATRACE_TAG ATRACE_TAG_CAMERA
//#define LOG_NDEBUG 0
//#define LOG_NNDEBUG 0  // Per-frame verbose logging

#ifdef LOG_NNDEBUG
#define ALOGVV(...) ALOGV(__VA_ARGS__)
#else
#define ALOGVV(...) ((void)0)
#endif

#include <inttypes.h>

#include <utils/Trace.h>

#include <android/hardware/camera2/ICameraDeviceCallbacks.h>

#include "device3/Camera3OfflineSession.h"
#include "device3/Camera3OutputStream.h"
#include "device3/Camera3InputStream.h"
#include "device3/Camera3SharedOutputStream.h"
#include "utils/CameraTraces.h"

using namespace android::camera3;
using namespace android::hardware::camera;

namespace android {

Camera3OfflineSession::Camera3OfflineSession(const String8 &id,
        const sp<camera3::Camera3Stream>& inputStream,
        const camera3::StreamSet& offlineStreamSet,
        camera3::BufferRecords&& bufferRecords,
        const camera3::InFlightRequestMap& offlineReqs,
        const Camera3OfflineStates& offlineStates,
        sp<hardware::camera::device::V3_6::ICameraOfflineSession> offlineSession) :
        mId(id),
        mInputStream(inputStream),
        mOutputStreams(offlineStreamSet),
        mBufferRecords(std::move(bufferRecords)),
        mOfflineReqs(offlineReqs),
        mSession(offlineSession),
        mTagMonitor(offlineStates.mTagMonitor),
        mVendorTagId(offlineStates.mVendorTagId),
        mUseHalBufManager(offlineStates.mUseHalBufManager),
        mNeedFixupMonochromeTags(offlineStates.mNeedFixupMonochromeTags),
        mUsePartialResult(offlineStates.mUsePartialResult),
        mNumPartialResults(offlineStates.mNumPartialResults),
        mLastCompletedRegularFrameNumber(offlineStates.mLastCompletedRegularFrameNumber),
        mLastCompletedReprocessFrameNumber(offlineStates.mLastCompletedReprocessFrameNumber),
        mLastCompletedZslFrameNumber(offlineStates.mLastCompletedZslFrameNumber),
        mNextResultFrameNumber(offlineStates.mNextResultFrameNumber),
        mNextReprocessResultFrameNumber(offlineStates.mNextReprocessResultFrameNumber),
        mNextZslStillResultFrameNumber(offlineStates.mNextZslStillResultFrameNumber),
        mNextShutterFrameNumber(offlineStates.mNextShutterFrameNumber),
        mNextReprocessShutterFrameNumber(offlineStates.mNextReprocessShutterFrameNumber),
        mNextZslStillShutterFrameNumber(offlineStates.mNextZslStillShutterFrameNumber),
        mDeviceInfo(offlineStates.mDeviceInfo),
        mPhysicalDeviceInfoMap(offlineStates.mPhysicalDeviceInfoMap),
        mDistortionMappers(offlineStates.mDistortionMappers),
        mZoomRatioMappers(offlineStates.mZoomRatioMappers),
        mRotateAndCropMappers(offlineStates.mRotateAndCropMappers),
        mStatus(STATUS_UNINITIALIZED) {
    ATRACE_CALL();
    ALOGV("%s: Created offline session for camera %s", __FUNCTION__, mId.string());
}

Camera3OfflineSession::~Camera3OfflineSession() {
    ATRACE_CALL();
    ALOGV("%s: Tearing down offline session for camera id %s", __FUNCTION__, mId.string());
    disconnectImpl();
}

const String8& Camera3OfflineSession::getId() const {
    return mId;
}

status_t Camera3OfflineSession::initialize(wp<NotificationListener> listener) {
    ATRACE_CALL();

    if (mSession == nullptr) {
        ALOGE("%s: HIDL session is null!", __FUNCTION__);
        return DEAD_OBJECT;
    }

    {
        std::lock_guard<std::mutex> lock(mLock);

        mListener = listener;

        // setup result FMQ
        std::unique_ptr<ResultMetadataQueue>& resQueue = mResultMetadataQueue;
        auto resultQueueRet = mSession->getCaptureResultMetadataQueue(
            [&resQueue](const auto& descriptor) {
                resQueue = std::make_unique<ResultMetadataQueue>(descriptor);
                if (!resQueue->isValid() || resQueue->availableToWrite() <= 0) {
                    ALOGE("HAL returns empty result metadata fmq, not use it");
                    resQueue = nullptr;
                    // Don't use resQueue onwards.
                }
            });
        if (!resultQueueRet.isOk()) {
            ALOGE("Transaction error when getting result metadata queue from camera session: %s",
                    resultQueueRet.description().c_str());
            return DEAD_OBJECT;
        }
        mStatus = STATUS_ACTIVE;
    }

    mSession->setCallback(this);

    return OK;
}

status_t Camera3OfflineSession::dump(int /*fd*/) {
    ATRACE_CALL();
    std::lock_guard<std::mutex> il(mInterfaceLock);
    return OK;
}

status_t Camera3OfflineSession::disconnect() {
    ATRACE_CALL();
    return disconnectImpl();
}

status_t Camera3OfflineSession::disconnectImpl() {
    ATRACE_CALL();
    std::lock_guard<std::mutex> il(mInterfaceLock);

    sp<NotificationListener> listener;
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (mStatus == STATUS_CLOSED) {
            return OK; // don't close twice
        } else if (mStatus == STATUS_ERROR) {
            ALOGE("%s: offline session %s shutting down in error state",
                    __FUNCTION__, mId.string());
        }
        listener = mListener.promote();
    }

    ALOGV("%s: E", __FUNCTION__);

    {
        std::lock_guard<std::mutex> lock(mRequestBufferInterfaceLock);
        mAllowRequestBuffer = false;
    }

    std::vector<wp<Camera3StreamInterface>> streams;
    streams.reserve(mOutputStreams.size() + (mInputStream != nullptr ? 1 : 0));
    for (size_t i = 0; i < mOutputStreams.size(); i++) {
        streams.push_back(mOutputStreams[i]);
    }
    if (mInputStream != nullptr) {
        streams.push_back(mInputStream);
    }

    if (mSession != nullptr) {
        mSession->close();
    }

    FlushInflightReqStates states {
        mId, mOfflineReqsLock, mOfflineReqs, mUseHalBufManager,
        listener, *this, mBufferRecords, *this, mSessionStatsBuilder};

    camera3::flushInflightRequests(states);

    {
        std::lock_guard<std::mutex> lock(mLock);
        mSession.clear();
        mOutputStreams.clear();
        mInputStream.clear();
        mStatus = STATUS_CLOSED;
    }

    for (auto& weakStream : streams) {
        sp<Camera3StreamInterface> stream = weakStream.promote();
        if (stream != nullptr) {
            ALOGE("%s: Stream %d leaked! strong reference (%d)!",
                    __FUNCTION__, stream->getId(), stream->getStrongCount() - 1);
        }
    }

    ALOGV("%s: X", __FUNCTION__);
    return OK;
}

status_t Camera3OfflineSession::waitForNextFrame(nsecs_t timeout) {
    ATRACE_CALL();
    std::unique_lock<std::mutex> lk(mOutputLock);

    while (mResultQueue.empty()) {
        auto st = mResultSignal.wait_for(lk, std::chrono::nanoseconds(timeout));
        if (st == std::cv_status::timeout) {
            return TIMED_OUT;
        }
    }
    return OK;
}

status_t Camera3OfflineSession::getNextResult(CaptureResult* frame) {
    ATRACE_CALL();
    std::lock_guard<std::mutex> l(mOutputLock);

    if (mResultQueue.empty()) {
        return NOT_ENOUGH_DATA;
    }

    if (frame == nullptr) {
        ALOGE("%s: argument cannot be NULL", __FUNCTION__);
        return BAD_VALUE;
    }

    CaptureResult &result = *(mResultQueue.begin());
    frame->mResultExtras = result.mResultExtras;
    frame->mMetadata.acquire(result.mMetadata);
    frame->mPhysicalMetadatas = std::move(result.mPhysicalMetadatas);
    mResultQueue.erase(mResultQueue.begin());

    return OK;
}

hardware::Return<void> Camera3OfflineSession::processCaptureResult_3_4(
        const hardware::hidl_vec<
                hardware::camera::device::V3_4::CaptureResult>& results) {
    sp<NotificationListener> listener;
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (mStatus != STATUS_ACTIVE) {
            ALOGE("%s called in wrong state %d", __FUNCTION__, mStatus);
            return hardware::Void();
        }
        listener = mListener.promote();
    }

    CaptureOutputStates states {
        mId,
        mOfflineReqsLock, mLastCompletedRegularFrameNumber,
        mLastCompletedReprocessFrameNumber, mLastCompletedZslFrameNumber,
        mOfflineReqs, mOutputLock, mResultQueue, mResultSignal,
        mNextShutterFrameNumber,
        mNextReprocessShutterFrameNumber, mNextZslStillShutterFrameNumber,
        mNextResultFrameNumber,
        mNextReprocessResultFrameNumber, mNextZslStillResultFrameNumber,
        mUseHalBufManager, mUsePartialResult, mNeedFixupMonochromeTags,
        mNumPartialResults, mVendorTagId, mDeviceInfo, mPhysicalDeviceInfoMap,
        mResultMetadataQueue, mDistortionMappers, mZoomRatioMappers, mRotateAndCropMappers,
        mTagMonitor, mInputStream, mOutputStreams, mSessionStatsBuilder, listener, *this, *this,
        mBufferRecords, /*legacyClient*/ false
    };

    std::lock_guard<std::mutex> lock(mProcessCaptureResultLock);
    for (const auto& result : results) {
        processOneCaptureResultLocked(states, result.v3_2, result.physicalCameraMetadata);
    }
    return hardware::Void();
}

hardware::Return<void> Camera3OfflineSession::processCaptureResult(
        const hardware::hidl_vec<
                hardware::camera::device::V3_2::CaptureResult>& results) {
    // TODO: changed impl to call into processCaptureResult_3_4 instead?
    //       might need to figure how to reduce copy though.
    sp<NotificationListener> listener;
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (mStatus != STATUS_ACTIVE) {
            ALOGE("%s called in wrong state %d", __FUNCTION__, mStatus);
            return hardware::Void();
        }
        listener = mListener.promote();
    }

    hardware::hidl_vec<hardware::camera::device::V3_4::PhysicalCameraMetadata> noPhysMetadata;

    CaptureOutputStates states {
        mId,
        mOfflineReqsLock, mLastCompletedRegularFrameNumber,
        mLastCompletedReprocessFrameNumber, mLastCompletedZslFrameNumber,
        mOfflineReqs, mOutputLock, mResultQueue, mResultSignal,
        mNextShutterFrameNumber,
        mNextReprocessShutterFrameNumber, mNextZslStillShutterFrameNumber,
        mNextResultFrameNumber,
        mNextReprocessResultFrameNumber, mNextZslStillResultFrameNumber,
        mUseHalBufManager, mUsePartialResult, mNeedFixupMonochromeTags,
        mNumPartialResults, mVendorTagId, mDeviceInfo, mPhysicalDeviceInfoMap,
        mResultMetadataQueue, mDistortionMappers, mZoomRatioMappers, mRotateAndCropMappers,
        mTagMonitor, mInputStream, mOutputStreams, mSessionStatsBuilder, listener, *this, *this,
        mBufferRecords, /*legacyClient*/ false
    };

    std::lock_guard<std::mutex> lock(mProcessCaptureResultLock);
    for (const auto& result : results) {
        processOneCaptureResultLocked(states, result, noPhysMetadata);
    }
    return hardware::Void();
}

hardware::Return<void> Camera3OfflineSession::notify(
        const hardware::hidl_vec<hardware::camera::device::V3_2::NotifyMsg>& msgs) {
    sp<NotificationListener> listener;
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (mStatus != STATUS_ACTIVE) {
            ALOGE("%s called in wrong state %d", __FUNCTION__, mStatus);
            return hardware::Void();
        }
        listener = mListener.promote();
    }

    CaptureOutputStates states {
        mId,
        mOfflineReqsLock, mLastCompletedRegularFrameNumber,
        mLastCompletedReprocessFrameNumber, mLastCompletedZslFrameNumber,
        mOfflineReqs, mOutputLock, mResultQueue, mResultSignal,
        mNextShutterFrameNumber,
        mNextReprocessShutterFrameNumber, mNextZslStillShutterFrameNumber,
        mNextResultFrameNumber,
        mNextReprocessResultFrameNumber, mNextZslStillResultFrameNumber,
        mUseHalBufManager, mUsePartialResult, mNeedFixupMonochromeTags,
        mNumPartialResults, mVendorTagId, mDeviceInfo, mPhysicalDeviceInfoMap,
        mResultMetadataQueue, mDistortionMappers, mZoomRatioMappers, mRotateAndCropMappers,
        mTagMonitor, mInputStream, mOutputStreams, mSessionStatsBuilder, listener, *this, *this,
        mBufferRecords, /*legacyClient*/ false
    };
    for (const auto& msg : msgs) {
        camera3::notify(states, msg);
    }
    return hardware::Void();
}

hardware::Return<void> Camera3OfflineSession::requestStreamBuffers(
        const hardware::hidl_vec<hardware::camera::device::V3_5::BufferRequest>& bufReqs,
        requestStreamBuffers_cb _hidl_cb) {
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (mStatus != STATUS_ACTIVE) {
            ALOGE("%s called in wrong state %d", __FUNCTION__, mStatus);
            return hardware::Void();
        }
    }

    RequestBufferStates states {
        mId, mRequestBufferInterfaceLock, mUseHalBufManager, mOutputStreams, mSessionStatsBuilder,
        *this, mBufferRecords, *this};
    camera3::requestStreamBuffers(states, bufReqs, _hidl_cb);
    return hardware::Void();
}

hardware::Return<void> Camera3OfflineSession::returnStreamBuffers(
        const hardware::hidl_vec<hardware::camera::device::V3_2::StreamBuffer>& buffers) {
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (mStatus != STATUS_ACTIVE) {
            ALOGE("%s called in wrong state %d", __FUNCTION__, mStatus);
            return hardware::Void();
        }
    }

    ReturnBufferStates states {
        mId, mUseHalBufManager, mOutputStreams, mSessionStatsBuilder, mBufferRecords};
    camera3::returnStreamBuffers(states, buffers);
    return hardware::Void();
}

void Camera3OfflineSession::setErrorState(const char *fmt, ...) {
    ATRACE_CALL();
    std::lock_guard<std::mutex> lock(mLock);
    va_list args;
    va_start(args, fmt);

    setErrorStateLockedV(fmt, args);

    va_end(args);

    //FIXME: automatically disconnect here?
}

void Camera3OfflineSession::setErrorStateLocked(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    setErrorStateLockedV(fmt, args);

    va_end(args);
}

void Camera3OfflineSession::setErrorStateLockedV(const char *fmt, va_list args) {
    // Print out all error messages to log
    String8 errorCause = String8::formatV(fmt, args);
    ALOGE("Camera %s: %s", mId.string(), errorCause.string());

    // But only do error state transition steps for the first error
    if (mStatus == STATUS_ERROR || mStatus == STATUS_UNINITIALIZED) return;

    mErrorCause = errorCause;

    mStatus = STATUS_ERROR;

    // Notify upstream about a device error
    sp<NotificationListener> listener = mListener.promote();
    if (listener != NULL) {
        listener->notifyError(hardware::camera2::ICameraDeviceCallbacks::ERROR_CAMERA_DEVICE,
                CaptureResultExtras());
    }

    // Save stack trace. View by dumping it later.
    CameraTraces::saveTrace();
}

void Camera3OfflineSession::onInflightEntryRemovedLocked(nsecs_t /*duration*/) {
    if (mOfflineReqs.size() == 0) {
        std::lock_guard<std::mutex> lock(mRequestBufferInterfaceLock);
        mAllowRequestBuffer = false;
    }
}

void Camera3OfflineSession::checkInflightMapLengthLocked() {
    // Intentional empty impl.
}

void Camera3OfflineSession::onInflightMapFlushedLocked() {
    // Intentional empty impl.
}

bool Camera3OfflineSession::startRequestBuffer() {
    return mAllowRequestBuffer;
}

void Camera3OfflineSession::endRequestBuffer() {
    // Intentional empty impl.
}

nsecs_t Camera3OfflineSession::getWaitDuration() {
    const nsecs_t kBaseGetBufferWait = 3000000000; // 3 sec.
    return kBaseGetBufferWait;
}

void Camera3OfflineSession::getInflightBufferKeys(std::vector<std::pair<int32_t, int32_t>>* out) {
    mBufferRecords.getInflightBufferKeys(out);
}

void Camera3OfflineSession::getInflightRequestBufferKeys(std::vector<uint64_t>* out) {
    mBufferRecords.getInflightRequestBufferKeys(out);
}

std::vector<sp<Camera3StreamInterface>> Camera3OfflineSession::getAllStreams() {
    std::vector<sp<Camera3StreamInterface>> ret;
    bool hasInputStream = mInputStream != nullptr;
    ret.reserve(mOutputStreams.size() + ((hasInputStream) ? 1 : 0));
    if (hasInputStream) {
        ret.push_back(mInputStream);
    }
    for (size_t i = 0; i < mOutputStreams.size(); i++) {
        ret.push_back(mOutputStreams[i]);
    }
    return ret;
}

const CameraMetadata& Camera3OfflineSession::info() const {
    return mDeviceInfo;
}

}; // namespace android
