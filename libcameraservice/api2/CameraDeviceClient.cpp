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

#define LOG_TAG "CameraDeviceClient"
#define ATRACE_TAG ATRACE_TAG_CAMERA
//#define LOG_NDEBUG 0

#include <cutils/properties.h>
#include <utils/CameraThreadState.h>
#include <utils/Log.h>
#include <utils/SessionConfigurationUtils.h>
#include <utils/Trace.h>
#include <gui/Surface.h>
#include <camera/camera2/CaptureRequest.h>
#include <camera/CameraUtils.h>

#include "common/CameraDeviceBase.h"
#include "device3/Camera3Device.h"
#include "device3/Camera3OutputStream.h"
#include "api2/CameraDeviceClient.h"
#include "utils/CameraServiceProxyWrapper.h"

#include <camera_metadata_hidden.h>

#include "DepthCompositeStream.h"
#include "HeicCompositeStream.h"

// Convenience methods for constructing binder::Status objects for error returns

#define STATUS_ERROR(errorCode, errorString) \
    binder::Status::fromServiceSpecificError(errorCode, \
            String8::format("%s:%d: %s", __FUNCTION__, __LINE__, errorString))

#define STATUS_ERROR_FMT(errorCode, errorString, ...) \
    binder::Status::fromServiceSpecificError(errorCode, \
            String8::format("%s:%d: " errorString, __FUNCTION__, __LINE__, \
                    __VA_ARGS__))

namespace android {
using namespace camera2;
using camera3::camera_stream_rotation_t::CAMERA_STREAM_ROTATION_0;
using camera3::SessionConfigurationUtils;

CameraDeviceClientBase::CameraDeviceClientBase(
        const sp<CameraService>& cameraService,
        const sp<hardware::camera2::ICameraDeviceCallbacks>& remoteCallback,
        const String16& clientPackageName,
        const std::optional<String16>& clientFeatureId,
        const String8& cameraId,
        int api1CameraId,
        int cameraFacing,
        int sensorOrientation,
        int clientPid,
        uid_t clientUid,
        int servicePid) :
    BasicClient(cameraService,
            IInterface::asBinder(remoteCallback),
            clientPackageName,
            clientFeatureId,
            cameraId,
            cameraFacing,
            sensorOrientation,
            clientPid,
            clientUid,
            servicePid),
    mRemoteCallback(remoteCallback) {
    // We don't need it for API2 clients, but Camera2ClientBase requires it.
    (void) api1CameraId;
}

// Interface used by CameraService

CameraDeviceClient::CameraDeviceClient(const sp<CameraService>& cameraService,
        const sp<hardware::camera2::ICameraDeviceCallbacks>& remoteCallback,
        const String16& clientPackageName,
        const std::optional<String16>& clientFeatureId,
        const String8& cameraId,
        int cameraFacing,
        int sensorOrientation,
        int clientPid,
        uid_t clientUid,
        int servicePid,
        bool overrideForPerfClass) :
    Camera2ClientBase(cameraService, remoteCallback, clientPackageName, clientFeatureId,
                cameraId, /*API1 camera ID*/ -1, cameraFacing, sensorOrientation,
                clientPid, clientUid, servicePid, overrideForPerfClass),
    mInputStream(),
    mStreamingRequestId(REQUEST_ID_NONE),
    mRequestIdCounter(0),
    mOverrideForPerfClass(overrideForPerfClass) {

    ATRACE_CALL();
    ALOGI("CameraDeviceClient %s: Opened", cameraId.string());
}

status_t CameraDeviceClient::initialize(sp<CameraProviderManager> manager,
        const String8& monitorTags) {
    return initializeImpl(manager, monitorTags);
}

template<typename TProviderPtr>
status_t CameraDeviceClient::initializeImpl(TProviderPtr providerPtr, const String8& monitorTags) {
    ATRACE_CALL();
    status_t res;

    res = Camera2ClientBase::initialize(providerPtr, monitorTags);
    if (res != OK) {
        return res;
    }

    String8 threadName;
    mFrameProcessor = new FrameProcessorBase(mDevice);
    threadName = String8::format("CDU-%s-FrameProc", mCameraIdStr.string());
    mFrameProcessor->run(threadName.string());

    mFrameProcessor->registerListener(camera2::FrameProcessorBase::FRAME_PROCESSOR_LISTENER_MIN_ID,
                                      camera2::FrameProcessorBase::FRAME_PROCESSOR_LISTENER_MAX_ID,
                                      /*listener*/this,
                                      /*sendPartials*/true);

    const CameraMetadata &deviceInfo = mDevice->info();
    camera_metadata_ro_entry_t physicalKeysEntry = deviceInfo.find(
            ANDROID_REQUEST_AVAILABLE_PHYSICAL_CAMERA_REQUEST_KEYS);
    if (physicalKeysEntry.count > 0) {
        mSupportedPhysicalRequestKeys.insert(mSupportedPhysicalRequestKeys.begin(),
                physicalKeysEntry.data.i32,
                physicalKeysEntry.data.i32 + physicalKeysEntry.count);
    }

    mProviderManager = providerPtr;
    // Cache physical camera ids corresponding to this device and also the high
    // resolution sensors in this device + physical camera ids
    mProviderManager->isLogicalCamera(mCameraIdStr.string(), &mPhysicalCameraIds);
    if (isUltraHighResolutionSensor(mCameraIdStr)) {
        mHighResolutionSensors.insert(mCameraIdStr.string());
    }
    for (auto &physicalId : mPhysicalCameraIds) {
        if (isUltraHighResolutionSensor(String8(physicalId.c_str()))) {
            mHighResolutionSensors.insert(physicalId.c_str());
        }
    }
    return OK;
}

CameraDeviceClient::~CameraDeviceClient() {
}

binder::Status CameraDeviceClient::submitRequest(
        const hardware::camera2::CaptureRequest& request,
        bool streaming,
        /*out*/
        hardware::camera2::utils::SubmitInfo *submitInfo) {
    std::vector<hardware::camera2::CaptureRequest> requestList = { request };
    return submitRequestList(requestList, streaming, submitInfo);
}

binder::Status CameraDeviceClient::insertGbpLocked(const sp<IGraphicBufferProducer>& gbp,
        SurfaceMap* outSurfaceMap, Vector<int32_t>* outputStreamIds, int32_t *currentStreamId) {
    int compositeIdx;
    int idx = mStreamMap.indexOfKey(IInterface::asBinder(gbp));

    // Trying to submit request with surface that wasn't created
    if (idx == NAME_NOT_FOUND) {
        ALOGE("%s: Camera %s: Tried to submit a request with a surface that"
                " we have not called createStream on",
                __FUNCTION__, mCameraIdStr.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                "Request targets Surface that is not part of current capture session");
    } else if ((compositeIdx = mCompositeStreamMap.indexOfKey(IInterface::asBinder(gbp)))
            != NAME_NOT_FOUND) {
        mCompositeStreamMap.valueAt(compositeIdx)->insertGbp(outSurfaceMap, outputStreamIds,
                currentStreamId);
        return binder::Status::ok();
    }

    const StreamSurfaceId& streamSurfaceId = mStreamMap.valueAt(idx);
    if (outSurfaceMap->find(streamSurfaceId.streamId()) == outSurfaceMap->end()) {
        outputStreamIds->push_back(streamSurfaceId.streamId());
    }
    (*outSurfaceMap)[streamSurfaceId.streamId()].push_back(streamSurfaceId.surfaceId());

    ALOGV("%s: Camera %s: Appending output stream %d surface %d to request",
            __FUNCTION__, mCameraIdStr.string(), streamSurfaceId.streamId(),
            streamSurfaceId.surfaceId());

    if (currentStreamId != nullptr) {
        *currentStreamId = streamSurfaceId.streamId();
    }

    return binder::Status::ok();
}

static std::list<int> getIntersection(const std::unordered_set<int> &streamIdsForThisCamera,
        const Vector<int> &streamIdsForThisRequest) {
    std::list<int> intersection;
    for (auto &streamId : streamIdsForThisRequest) {
        if (streamIdsForThisCamera.find(streamId) != streamIdsForThisCamera.end()) {
            intersection.emplace_back(streamId);
        }
    }
    return intersection;
}

binder::Status CameraDeviceClient::submitRequestList(
        const std::vector<hardware::camera2::CaptureRequest>& requests,
        bool streaming,
        /*out*/
        hardware::camera2::utils::SubmitInfo *submitInfo) {
    ATRACE_CALL();
    ALOGV("%s-start of function. Request list size %zu", __FUNCTION__, requests.size());

    binder::Status res = binder::Status::ok();
    status_t err;
    if ( !(res = checkPidStatus(__FUNCTION__) ).isOk()) {
        return res;
    }

    Mutex::Autolock icl(mBinderSerializationLock);

    if (!mDevice.get()) {
        return STATUS_ERROR(CameraService::ERROR_DISCONNECTED, "Camera device no longer alive");
    }

    if (requests.empty()) {
        ALOGE("%s: Camera %s: Sent null request. Rejecting request.",
              __FUNCTION__, mCameraIdStr.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, "Empty request list");
    }

    List<const CameraDeviceBase::PhysicalCameraSettingsList> metadataRequestList;
    std::list<const SurfaceMap> surfaceMapList;
    submitInfo->mRequestId = mRequestIdCounter;
    uint32_t loopCounter = 0;

    for (auto&& request: requests) {
        if (request.mIsReprocess) {
            if (!mInputStream.configured) {
                ALOGE("%s: Camera %s: no input stream is configured.", __FUNCTION__,
                        mCameraIdStr.string());
                return STATUS_ERROR_FMT(CameraService::ERROR_ILLEGAL_ARGUMENT,
                        "No input configured for camera %s but request is for reprocessing",
                        mCameraIdStr.string());
            } else if (streaming) {
                ALOGE("%s: Camera %s: streaming reprocess requests not supported.", __FUNCTION__,
                        mCameraIdStr.string());
                return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                        "Repeating reprocess requests not supported");
            } else if (request.mPhysicalCameraSettings.size() > 1) {
                ALOGE("%s: Camera %s: reprocess requests not supported for "
                        "multiple physical cameras.", __FUNCTION__,
                        mCameraIdStr.string());
                return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                        "Reprocess requests not supported for multiple cameras");
            }
        }

        if (request.mPhysicalCameraSettings.empty()) {
            ALOGE("%s: Camera %s: request doesn't contain any settings.", __FUNCTION__,
                    mCameraIdStr.string());
            return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                    "Request doesn't contain any settings");
        }

        //The first capture settings should always match the logical camera id
        String8 logicalId(request.mPhysicalCameraSettings.begin()->id.c_str());
        if (mDevice->getId() != logicalId) {
            ALOGE("%s: Camera %s: Invalid camera request settings.", __FUNCTION__,
                    mCameraIdStr.string());
            return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                    "Invalid camera request settings");
        }

        if (request.mSurfaceList.isEmpty() && request.mStreamIdxList.size() == 0) {
            ALOGE("%s: Camera %s: Requests must have at least one surface target. "
                    "Rejecting request.", __FUNCTION__, mCameraIdStr.string());
            return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                    "Request has no output targets");
        }

        /**
         * Write in the output stream IDs and map from stream ID to surface ID
         * which we calculate from the capture request's list of surface target
         */
        SurfaceMap surfaceMap;
        Vector<int32_t> outputStreamIds;
        std::vector<std::string> requestedPhysicalIds;
        if (request.mSurfaceList.size() > 0) {
            for (const sp<Surface>& surface : request.mSurfaceList) {
                if (surface == 0) continue;

                int32_t streamId;
                sp<IGraphicBufferProducer> gbp = surface->getIGraphicBufferProducer();
                res = insertGbpLocked(gbp, &surfaceMap, &outputStreamIds, &streamId);
                if (!res.isOk()) {
                    return res;
                }

                ssize_t index = mConfiguredOutputs.indexOfKey(streamId);
                if (index >= 0) {
                    String8 requestedPhysicalId(
                            mConfiguredOutputs.valueAt(index).getPhysicalCameraId());
                    requestedPhysicalIds.push_back(requestedPhysicalId.string());
                } else {
                    ALOGW("%s: Output stream Id not found among configured outputs!", __FUNCTION__);
                }
            }
        } else {
            for (size_t i = 0; i < request.mStreamIdxList.size(); i++) {
                int streamId = request.mStreamIdxList.itemAt(i);
                int surfaceIdx = request.mSurfaceIdxList.itemAt(i);

                ssize_t index = mConfiguredOutputs.indexOfKey(streamId);
                if (index < 0) {
                    ALOGE("%s: Camera %s: Tried to submit a request with a surface that"
                            " we have not called createStream on: stream %d",
                            __FUNCTION__, mCameraIdStr.string(), streamId);
                    return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                            "Request targets Surface that is not part of current capture session");
                }

                const auto& gbps = mConfiguredOutputs.valueAt(index).getGraphicBufferProducers();
                if ((size_t)surfaceIdx >= gbps.size()) {
                    ALOGE("%s: Camera %s: Tried to submit a request with a surface that"
                            " we have not called createStream on: stream %d, surfaceIdx %d",
                            __FUNCTION__, mCameraIdStr.string(), streamId, surfaceIdx);
                    return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                            "Request targets Surface has invalid surface index");
                }

                res = insertGbpLocked(gbps[surfaceIdx], &surfaceMap, &outputStreamIds, nullptr);
                if (!res.isOk()) {
                    return res;
                }

                String8 requestedPhysicalId(
                        mConfiguredOutputs.valueAt(index).getPhysicalCameraId());
                requestedPhysicalIds.push_back(requestedPhysicalId.string());
            }
        }

        CameraDeviceBase::PhysicalCameraSettingsList physicalSettingsList;
        for (const auto& it : request.mPhysicalCameraSettings) {
            if (it.settings.isEmpty()) {
                ALOGE("%s: Camera %s: Sent empty metadata packet. Rejecting request.",
                        __FUNCTION__, mCameraIdStr.string());
                return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                        "Request settings are empty");
            }

            // Check whether the physical / logical stream has settings
            // consistent with the sensor pixel mode(s) it was configured with.
            // mCameraIdToStreamSet will only have ids that are high resolution
            const auto streamIdSetIt = mHighResolutionCameraIdToStreamIdSet.find(it.id);
            if (streamIdSetIt != mHighResolutionCameraIdToStreamIdSet.end()) {
                std::list<int> streamIdsUsedInRequest = getIntersection(streamIdSetIt->second,
                        outputStreamIds);
                if (!request.mIsReprocess &&
                        !isSensorPixelModeConsistent(streamIdsUsedInRequest, it.settings)) {
                     ALOGE("%s: Camera %s: Request settings CONTROL_SENSOR_PIXEL_MODE not "
                            "consistent with configured streams. Rejecting request.",
                            __FUNCTION__, it.id.c_str());
                    return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                        "Request settings CONTROL_SENSOR_PIXEL_MODE are not consistent with "
                        "streams configured");
                }
            }

            String8 physicalId(it.id.c_str());
            bool hasTestPatternModePhysicalKey = std::find(mSupportedPhysicalRequestKeys.begin(),
                    mSupportedPhysicalRequestKeys.end(), ANDROID_SENSOR_TEST_PATTERN_MODE) !=
                    mSupportedPhysicalRequestKeys.end();
            bool hasTestPatternDataPhysicalKey = std::find(mSupportedPhysicalRequestKeys.begin(),
                    mSupportedPhysicalRequestKeys.end(), ANDROID_SENSOR_TEST_PATTERN_DATA) !=
                    mSupportedPhysicalRequestKeys.end();
            if (physicalId != mDevice->getId()) {
                auto found = std::find(requestedPhysicalIds.begin(), requestedPhysicalIds.end(),
                        it.id);
                if (found == requestedPhysicalIds.end()) {
                    ALOGE("%s: Camera %s: Physical camera id: %s not part of attached outputs.",
                            __FUNCTION__, mCameraIdStr.string(), physicalId.string());
                    return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                            "Invalid physical camera id");
                }

                if (!mSupportedPhysicalRequestKeys.empty()) {
                    // Filter out any unsupported physical request keys.
                    CameraMetadata filteredParams(mSupportedPhysicalRequestKeys.size());
                    camera_metadata_t *meta = const_cast<camera_metadata_t *>(
                            filteredParams.getAndLock());
                    set_camera_metadata_vendor_id(meta, mDevice->getVendorTagId());
                    filteredParams.unlock(meta);

                    for (const auto& keyIt : mSupportedPhysicalRequestKeys) {
                        camera_metadata_ro_entry entry = it.settings.find(keyIt);
                        if (entry.count > 0) {
                            filteredParams.update(entry);
                        }
                    }

                    physicalSettingsList.push_back({it.id, filteredParams,
                            hasTestPatternModePhysicalKey, hasTestPatternDataPhysicalKey});
                }
            } else {
                physicalSettingsList.push_back({it.id, it.settings});
            }
        }

        if (!enforceRequestPermissions(physicalSettingsList.begin()->metadata)) {
            // Callee logs
            return STATUS_ERROR(CameraService::ERROR_PERMISSION_DENIED,
                    "Caller does not have permission to change restricted controls");
        }

        physicalSettingsList.begin()->metadata.update(ANDROID_REQUEST_OUTPUT_STREAMS,
                &outputStreamIds[0], outputStreamIds.size());

        if (request.mIsReprocess) {
            physicalSettingsList.begin()->metadata.update(ANDROID_REQUEST_INPUT_STREAMS,
                    &mInputStream.id, 1);
        }

        physicalSettingsList.begin()->metadata.update(ANDROID_REQUEST_ID,
                &(submitInfo->mRequestId), /*size*/1);
        loopCounter++; // loopCounter starts from 1
        ALOGV("%s: Camera %s: Creating request with ID %d (%d of %zu)",
                __FUNCTION__, mCameraIdStr.string(), submitInfo->mRequestId,
                loopCounter, requests.size());

        metadataRequestList.push_back(physicalSettingsList);
        surfaceMapList.push_back(surfaceMap);
    }
    mRequestIdCounter++;

    if (streaming) {
        err = mDevice->setStreamingRequestList(metadataRequestList, surfaceMapList,
                &(submitInfo->mLastFrameNumber));
        if (err != OK) {
            String8 msg = String8::format(
                "Camera %s:  Got error %s (%d) after trying to set streaming request",
                mCameraIdStr.string(), strerror(-err), err);
            ALOGE("%s: %s", __FUNCTION__, msg.string());
            res = STATUS_ERROR(CameraService::ERROR_INVALID_OPERATION,
                    msg.string());
        } else {
            Mutex::Autolock idLock(mStreamingRequestIdLock);
            mStreamingRequestId = submitInfo->mRequestId;
        }
    } else {
        err = mDevice->captureList(metadataRequestList, surfaceMapList,
                &(submitInfo->mLastFrameNumber));
        if (err != OK) {
            String8 msg = String8::format(
                "Camera %s: Got error %s (%d) after trying to submit capture request",
                mCameraIdStr.string(), strerror(-err), err);
            ALOGE("%s: %s", __FUNCTION__, msg.string());
            res = STATUS_ERROR(CameraService::ERROR_INVALID_OPERATION,
                    msg.string());
        }
        ALOGV("%s: requestId = %d ", __FUNCTION__, submitInfo->mRequestId);
    }

    ALOGV("%s: Camera %s: End of function", __FUNCTION__, mCameraIdStr.string());
    return res;
}

binder::Status CameraDeviceClient::cancelRequest(
        int requestId,
        /*out*/
        int64_t* lastFrameNumber) {
    ATRACE_CALL();
    ALOGV("%s, requestId = %d", __FUNCTION__, requestId);

    status_t err;
    binder::Status res;

    if (!(res = checkPidStatus(__FUNCTION__)).isOk()) return res;

    Mutex::Autolock icl(mBinderSerializationLock);

    if (!mDevice.get()) {
        return STATUS_ERROR(CameraService::ERROR_DISCONNECTED, "Camera device no longer alive");
    }

    Mutex::Autolock idLock(mStreamingRequestIdLock);
    if (mStreamingRequestId != requestId) {
        String8 msg = String8::format("Camera %s: Canceling request ID %d doesn't match "
                "current request ID %d", mCameraIdStr.string(), requestId, mStreamingRequestId);
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    }

    err = mDevice->clearStreamingRequest(lastFrameNumber);

    if (err == OK) {
        ALOGV("%s: Camera %s: Successfully cleared streaming request",
                __FUNCTION__, mCameraIdStr.string());
        mStreamingRequestId = REQUEST_ID_NONE;
    } else {
        res = STATUS_ERROR_FMT(CameraService::ERROR_INVALID_OPERATION,
                "Camera %s: Error clearing streaming request: %s (%d)",
                mCameraIdStr.string(), strerror(-err), err);
    }

    return res;
}

binder::Status CameraDeviceClient::beginConfigure() {
    // TODO: Implement this.
    ATRACE_CALL();
    ALOGV("%s: Not implemented yet.", __FUNCTION__);
    return binder::Status::ok();
}

binder::Status CameraDeviceClient::endConfigure(int operatingMode,
        const hardware::camera2::impl::CameraMetadataNative& sessionParams, int64_t startTimeMs,
        std::vector<int>* offlineStreamIds /*out*/) {
    ATRACE_CALL();
    ALOGV("%s: ending configure (%d input stream, %zu output surfaces)",
            __FUNCTION__, mInputStream.configured ? 1 : 0,
            mStreamMap.size());

    binder::Status res;
    if (!(res = checkPidStatus(__FUNCTION__)).isOk()) return res;

    if (offlineStreamIds == nullptr) {
        String8 msg = String8::format("Invalid offline stream ids");
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    }

    Mutex::Autolock icl(mBinderSerializationLock);

    if (!mDevice.get()) {
        return STATUS_ERROR(CameraService::ERROR_DISCONNECTED, "Camera device no longer alive");
    }

    res = SessionConfigurationUtils::checkOperatingMode(operatingMode, mDevice->info(),
            mCameraIdStr);
    if (!res.isOk()) {
        return res;
    }

    status_t err = mDevice->configureStreams(sessionParams, operatingMode);
    if (err == BAD_VALUE) {
        String8 msg = String8::format("Camera %s: Unsupported set of inputs/outputs provided",
                mCameraIdStr.string());
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        res = STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    } else if (err != OK) {
        String8 msg = String8::format("Camera %s: Error configuring streams: %s (%d)",
                mCameraIdStr.string(), strerror(-err), err);
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        res = STATUS_ERROR(CameraService::ERROR_INVALID_OPERATION, msg.string());
    } else {
        offlineStreamIds->clear();
        mDevice->getOfflineStreamIds(offlineStreamIds);

        for (size_t i = 0; i < mCompositeStreamMap.size(); ++i) {
            err = mCompositeStreamMap.valueAt(i)->configureStream();
            if (err != OK) {
                String8 msg = String8::format("Camera %s: Error configuring composite "
                        "streams: %s (%d)", mCameraIdStr.string(), strerror(-err), err);
                ALOGE("%s: %s", __FUNCTION__, msg.string());
                res = STATUS_ERROR(CameraService::ERROR_INVALID_OPERATION, msg.string());
                break;
            }

            // Composite streams can only support offline mode in case all individual internal
            // streams are also supported.
            std::vector<int> internalStreams;
            mCompositeStreamMap.valueAt(i)->insertCompositeStreamIds(&internalStreams);
            offlineStreamIds->erase(
                    std::remove_if(offlineStreamIds->begin(), offlineStreamIds->end(),
                    [&internalStreams] (int streamId) {
                        auto it = std::find(internalStreams.begin(), internalStreams.end(),
                                streamId);
                        if (it != internalStreams.end()) {
                            internalStreams.erase(it);
                            return true;
                        }

                        return false;}), offlineStreamIds->end());
            if (internalStreams.empty()) {
                offlineStreamIds->push_back(mCompositeStreamMap.valueAt(i)->getStreamId());
            }
        }

        for (const auto& offlineStreamId : *offlineStreamIds) {
            mStreamInfoMap[offlineStreamId].supportsOffline = true;
        }

        nsecs_t configureEnd = systemTime();
        int32_t configureDurationMs = ns2ms(configureEnd) - startTimeMs;
        CameraServiceProxyWrapper::logStreamConfigured(mCameraIdStr, operatingMode,
                false /*internalReconfig*/, configureDurationMs);
    }

    return res;
}

binder::Status CameraDeviceClient::isSessionConfigurationSupported(
        const SessionConfiguration& sessionConfiguration, bool *status /*out*/) {

    ATRACE_CALL();
    binder::Status res;
    status_t ret = OK;
    if (!(res = checkPidStatus(__FUNCTION__)).isOk()) return res;

    Mutex::Autolock icl(mBinderSerializationLock);

    if (!mDevice.get()) {
        return STATUS_ERROR(CameraService::ERROR_DISCONNECTED, "Camera device no longer alive");
    }

    auto operatingMode = sessionConfiguration.getOperatingMode();
    res = SessionConfigurationUtils::checkOperatingMode(operatingMode, mDevice->info(),
            mCameraIdStr);
    if (!res.isOk()) {
        return res;
    }

    if (status == nullptr) {
        String8 msg = String8::format( "Camera %s: Invalid status!", mCameraIdStr.string());
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    }

    hardware::camera::device::V3_7::StreamConfiguration streamConfiguration;
    bool earlyExit = false;
    camera3::metadataGetter getMetadata = [this](const String8 &id, bool /*overrideForPerfClass*/) {
          return mDevice->infoPhysical(id);};
    std::vector<std::string> physicalCameraIds;
    mProviderManager->isLogicalCamera(mCameraIdStr.string(), &physicalCameraIds);
    res = SessionConfigurationUtils::convertToHALStreamCombination(sessionConfiguration,
            mCameraIdStr, mDevice->info(), getMetadata, physicalCameraIds, streamConfiguration,
            mOverrideForPerfClass, &earlyExit);
    if (!res.isOk()) {
        return res;
    }

    if (earlyExit) {
        *status = false;
        return binder::Status::ok();
    }

    *status = false;
    ret = mProviderManager->isSessionConfigurationSupported(mCameraIdStr.string(),
            streamConfiguration, status);
    switch (ret) {
        case OK:
            // Expected, do nothing.
            break;
        case INVALID_OPERATION: {
                String8 msg = String8::format(
                        "Camera %s: Session configuration query not supported!",
                        mCameraIdStr.string());
                ALOGD("%s: %s", __FUNCTION__, msg.string());
                res = STATUS_ERROR(CameraService::ERROR_INVALID_OPERATION, msg.string());
            }

            break;
        default: {
                String8 msg = String8::format( "Camera %s: Error: %s (%d)", mCameraIdStr.string(),
                        strerror(-ret), ret);
                ALOGE("%s: %s", __FUNCTION__, msg.string());
                res = STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                        msg.string());
            }
    }

    return res;
}

binder::Status CameraDeviceClient::deleteStream(int streamId) {
    ATRACE_CALL();
    ALOGV("%s (streamId = 0x%x)", __FUNCTION__, streamId);

    binder::Status res;
    if (!(res = checkPidStatus(__FUNCTION__)).isOk()) return res;

    Mutex::Autolock icl(mBinderSerializationLock);

    if (!mDevice.get()) {
        return STATUS_ERROR(CameraService::ERROR_DISCONNECTED, "Camera device no longer alive");
    }

    bool isInput = false;
    std::vector<sp<IBinder>> surfaces;
    ssize_t dIndex = NAME_NOT_FOUND;
    ssize_t compositeIndex  = NAME_NOT_FOUND;

    if (mInputStream.configured && mInputStream.id == streamId) {
        isInput = true;
    } else {
        // Guard against trying to delete non-created streams
        for (size_t i = 0; i < mStreamMap.size(); ++i) {
            if (streamId == mStreamMap.valueAt(i).streamId()) {
                surfaces.push_back(mStreamMap.keyAt(i));
            }
        }

        // See if this stream is one of the deferred streams.
        for (size_t i = 0; i < mDeferredStreams.size(); ++i) {
            if (streamId == mDeferredStreams[i]) {
                dIndex = i;
                break;
            }
        }

        for (size_t i = 0; i < mCompositeStreamMap.size(); ++i) {
            if (streamId == mCompositeStreamMap.valueAt(i)->getStreamId()) {
                compositeIndex = i;
                break;
            }
        }

        if (surfaces.empty() && dIndex == NAME_NOT_FOUND) {
            String8 msg = String8::format("Camera %s: Invalid stream ID (%d) specified, no such"
                    " stream created yet", mCameraIdStr.string(), streamId);
            ALOGW("%s: %s", __FUNCTION__, msg.string());
            return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
        }
    }

    // Also returns BAD_VALUE if stream ID was not valid
    status_t err = mDevice->deleteStream(streamId);

    if (err != OK) {
        String8 msg = String8::format("Camera %s: Unexpected error %s (%d) when deleting stream %d",
                mCameraIdStr.string(), strerror(-err), err, streamId);
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        res = STATUS_ERROR(CameraService::ERROR_INVALID_OPERATION, msg.string());
    } else {
        if (isInput) {
            mInputStream.configured = false;
        } else {
            for (auto& surface : surfaces) {
                mStreamMap.removeItem(surface);
            }

            mConfiguredOutputs.removeItem(streamId);

            if (dIndex != NAME_NOT_FOUND) {
                mDeferredStreams.removeItemsAt(dIndex);
            }

            if (compositeIndex != NAME_NOT_FOUND) {
                status_t ret;
                if ((ret = mCompositeStreamMap.valueAt(compositeIndex)->deleteStream())
                        != OK) {
                    String8 msg = String8::format("Camera %s: Unexpected error %s (%d) when "
                            "deleting composite stream %d", mCameraIdStr.string(), strerror(-err), err,
                            streamId);
                    ALOGE("%s: %s", __FUNCTION__, msg.string());
                    res = STATUS_ERROR(CameraService::ERROR_INVALID_OPERATION, msg.string());
                }
                mCompositeStreamMap.removeItemsAt(compositeIndex);
            }
            for (auto &mapIt: mHighResolutionCameraIdToStreamIdSet) {
                auto &streamSet = mapIt.second;
                if (streamSet.find(streamId) != streamSet.end()) {
                    streamSet.erase(streamId);
                    break;
                }
            }
        }
    }

    return res;
}

binder::Status CameraDeviceClient::createStream(
        const hardware::camera2::params::OutputConfiguration &outputConfiguration,
        /*out*/
        int32_t* newStreamId) {
    ATRACE_CALL();

    binder::Status res;
    if (!(res = checkPidStatus(__FUNCTION__)).isOk()) return res;

    Mutex::Autolock icl(mBinderSerializationLock);

    const std::vector<sp<IGraphicBufferProducer>>& bufferProducers =
            outputConfiguration.getGraphicBufferProducers();
    size_t numBufferProducers = bufferProducers.size();
    bool deferredConsumer = outputConfiguration.isDeferred();
    bool isShared = outputConfiguration.isShared();
    String8 physicalCameraId = String8(outputConfiguration.getPhysicalCameraId());
    bool deferredConsumerOnly = deferredConsumer && numBufferProducers == 0;
    bool isMultiResolution = outputConfiguration.isMultiResolution();

    res = SessionConfigurationUtils::checkSurfaceType(numBufferProducers, deferredConsumer,
            outputConfiguration.getSurfaceType());
    if (!res.isOk()) {
        return res;
    }

    if (!mDevice.get()) {
        return STATUS_ERROR(CameraService::ERROR_DISCONNECTED, "Camera device no longer alive");
    }
    res = SessionConfigurationUtils::checkPhysicalCameraId(mPhysicalCameraIds,
            physicalCameraId, mCameraIdStr);
    if (!res.isOk()) {
        return res;
    }

    std::vector<sp<Surface>> surfaces;
    std::vector<sp<IBinder>> binders;
    status_t err;

    // Create stream for deferred surface case.
    if (deferredConsumerOnly) {
        return createDeferredSurfaceStreamLocked(outputConfiguration, isShared, newStreamId);
    }

    OutputStreamInfo streamInfo;
    bool isStreamInfoValid = false;
    const std::vector<int32_t> &sensorPixelModesUsed =
            outputConfiguration.getSensorPixelModesUsed();
    for (auto& bufferProducer : bufferProducers) {
        // Don't create multiple streams for the same target surface
        sp<IBinder> binder = IInterface::asBinder(bufferProducer);
        ssize_t index = mStreamMap.indexOfKey(binder);
        if (index != NAME_NOT_FOUND) {
            String8 msg = String8::format("Camera %s: Surface already has a stream created for it "
                    "(ID %zd)", mCameraIdStr.string(), index);
            ALOGW("%s: %s", __FUNCTION__, msg.string());
            return STATUS_ERROR(CameraService::ERROR_ALREADY_EXISTS, msg.string());
        }

        sp<Surface> surface;
        res = SessionConfigurationUtils::createSurfaceFromGbp(streamInfo,
                isStreamInfoValid, surface, bufferProducer, mCameraIdStr,
                mDevice->infoPhysical(physicalCameraId), sensorPixelModesUsed);

        if (!res.isOk())
            return res;

        if (!isStreamInfoValid) {
            isStreamInfoValid = true;
        }

        binders.push_back(IInterface::asBinder(bufferProducer));
        surfaces.push_back(surface);
    }

    // If mOverrideForPerfClass is true, do not fail createStream() for small
    // JPEG sizes because existing createSurfaceFromGbp() logic will find the
    // closest possible supported size.

    int streamId = camera3::CAMERA3_STREAM_ID_INVALID;
    std::vector<int> surfaceIds;
    bool isDepthCompositeStream =
            camera3::DepthCompositeStream::isDepthCompositeStream(surfaces[0]);
    bool isHeicCompisiteStream = camera3::HeicCompositeStream::isHeicCompositeStream(surfaces[0]);
    if (isDepthCompositeStream || isHeicCompisiteStream) {
        sp<CompositeStream> compositeStream;
        if (isDepthCompositeStream) {
            compositeStream = new camera3::DepthCompositeStream(mDevice, getRemoteCallback());
        } else {
            compositeStream = new camera3::HeicCompositeStream(mDevice, getRemoteCallback());
        }

        err = compositeStream->createStream(surfaces, deferredConsumer, streamInfo.width,
                streamInfo.height, streamInfo.format,
                static_cast<camera_stream_rotation_t>(outputConfiguration.getRotation()),
                &streamId, physicalCameraId, streamInfo.sensorPixelModesUsed, &surfaceIds,
                outputConfiguration.getSurfaceSetID(), isShared, isMultiResolution);
        if (err == OK) {
            mCompositeStreamMap.add(IInterface::asBinder(surfaces[0]->getIGraphicBufferProducer()),
                    compositeStream);
        }
    } else {
        err = mDevice->createStream(surfaces, deferredConsumer, streamInfo.width,
                streamInfo.height, streamInfo.format, streamInfo.dataSpace,
                static_cast<camera_stream_rotation_t>(outputConfiguration.getRotation()),
                &streamId, physicalCameraId, streamInfo.sensorPixelModesUsed, &surfaceIds,
                outputConfiguration.getSurfaceSetID(), isShared, isMultiResolution);
    }

    if (err != OK) {
        res = STATUS_ERROR_FMT(CameraService::ERROR_INVALID_OPERATION,
                "Camera %s: Error creating output stream (%d x %d, fmt %x, dataSpace %x): %s (%d)",
                mCameraIdStr.string(), streamInfo.width, streamInfo.height, streamInfo.format,
                streamInfo.dataSpace, strerror(-err), err);
    } else {
        int i = 0;
        for (auto& binder : binders) {
            ALOGV("%s: mStreamMap add binder %p streamId %d, surfaceId %d",
                    __FUNCTION__, binder.get(), streamId, i);
            mStreamMap.add(binder, StreamSurfaceId(streamId, surfaceIds[i]));
            i++;
        }

        mConfiguredOutputs.add(streamId, outputConfiguration);
        mStreamInfoMap[streamId] = streamInfo;

        ALOGV("%s: Camera %s: Successfully created a new stream ID %d for output surface"
                    " (%d x %d) with format 0x%x.",
                  __FUNCTION__, mCameraIdStr.string(), streamId, streamInfo.width,
                  streamInfo.height, streamInfo.format);

        // Set transform flags to ensure preview to be rotated correctly.
        res = setStreamTransformLocked(streamId);

        // Fill in mHighResolutionCameraIdToStreamIdSet map
        const String8 &cameraIdUsed =
                physicalCameraId.size() != 0 ? physicalCameraId : mCameraIdStr;
        const char *cameraIdUsedCStr = cameraIdUsed.string();
        // Only needed for high resolution sensors
        if (mHighResolutionSensors.find(cameraIdUsedCStr) !=
                mHighResolutionSensors.end()) {
            mHighResolutionCameraIdToStreamIdSet[cameraIdUsedCStr].insert(streamId);
        }

        *newStreamId = streamId;
    }

    return res;
}

binder::Status CameraDeviceClient::createDeferredSurfaceStreamLocked(
        const hardware::camera2::params::OutputConfiguration &outputConfiguration,
        bool isShared,
        /*out*/
        int* newStreamId) {
    int width, height, format, surfaceType;
    uint64_t consumerUsage;
    android_dataspace dataSpace;
    status_t err;
    binder::Status res;

    if (!mDevice.get()) {
        return STATUS_ERROR(CameraService::ERROR_DISCONNECTED, "Camera device no longer alive");
    }

    // Infer the surface info for deferred surface stream creation.
    width = outputConfiguration.getWidth();
    height = outputConfiguration.getHeight();
    surfaceType = outputConfiguration.getSurfaceType();
    format = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
    dataSpace = android_dataspace_t::HAL_DATASPACE_UNKNOWN;
    // Hardcode consumer usage flags: SurfaceView--0x900, SurfaceTexture--0x100.
    consumerUsage = GraphicBuffer::USAGE_HW_TEXTURE;
    if (surfaceType == OutputConfiguration::SURFACE_TYPE_SURFACE_VIEW) {
        consumerUsage |= GraphicBuffer::USAGE_HW_COMPOSER;
    }
    int streamId = camera3::CAMERA3_STREAM_ID_INVALID;
    std::vector<sp<Surface>> noSurface;
    std::vector<int> surfaceIds;
    String8 physicalCameraId(outputConfiguration.getPhysicalCameraId());
    const String8 &cameraIdUsed =
            physicalCameraId.size() != 0 ? physicalCameraId : mCameraIdStr;
    // Here, we override sensor pixel modes
    std::unordered_set<int32_t> overriddenSensorPixelModesUsed;
    const std::vector<int32_t> &sensorPixelModesUsed =
            outputConfiguration.getSensorPixelModesUsed();
    if (SessionConfigurationUtils::checkAndOverrideSensorPixelModesUsed(
            sensorPixelModesUsed, format, width, height, getStaticInfo(cameraIdUsed),
            /*allowRounding*/ false, &overriddenSensorPixelModesUsed) != OK) {
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                "sensor pixel modes used not valid for deferred stream");
    }

    err = mDevice->createStream(noSurface, /*hasDeferredConsumer*/true, width,
            height, format, dataSpace,
            static_cast<camera_stream_rotation_t>(outputConfiguration.getRotation()),
            &streamId, physicalCameraId,
            overriddenSensorPixelModesUsed,
            &surfaceIds,
            outputConfiguration.getSurfaceSetID(), isShared,
            outputConfiguration.isMultiResolution(), consumerUsage);

    if (err != OK) {
        res = STATUS_ERROR_FMT(CameraService::ERROR_INVALID_OPERATION,
                "Camera %s: Error creating output stream (%d x %d, fmt %x, dataSpace %x): %s (%d)",
                mCameraIdStr.string(), width, height, format, dataSpace, strerror(-err), err);
    } else {
        // Can not add streamId to mStreamMap here, as the surface is deferred. Add it to
        // a separate list to track. Once the deferred surface is set, this id will be
        // relocated to mStreamMap.
        mDeferredStreams.push_back(streamId);
        mStreamInfoMap.emplace(std::piecewise_construct, std::forward_as_tuple(streamId),
                std::forward_as_tuple(width, height, format, dataSpace, consumerUsage,
                        overriddenSensorPixelModesUsed));

        ALOGV("%s: Camera %s: Successfully created a new stream ID %d for a deferred surface"
                " (%d x %d) stream with format 0x%x.",
              __FUNCTION__, mCameraIdStr.string(), streamId, width, height, format);

        // Set transform flags to ensure preview to be rotated correctly.
        res = setStreamTransformLocked(streamId);

        *newStreamId = streamId;
        // Fill in mHighResolutionCameraIdToStreamIdSet
        const char *cameraIdUsedCStr = cameraIdUsed.string();
        // Only needed for high resolution sensors
        if (mHighResolutionSensors.find(cameraIdUsedCStr) !=
                mHighResolutionSensors.end()) {
            mHighResolutionCameraIdToStreamIdSet[cameraIdUsed.string()].insert(streamId);
        }
    }
    return res;
}

binder::Status CameraDeviceClient::setStreamTransformLocked(int streamId) {
    int32_t transform = 0;
    status_t err;
    binder::Status res;

    if (!mDevice.get()) {
        return STATUS_ERROR(CameraService::ERROR_DISCONNECTED, "Camera device no longer alive");
    }

    err = getRotationTransformLocked(&transform);
    if (err != OK) {
        // Error logged by getRotationTransformLocked.
        return STATUS_ERROR(CameraService::ERROR_INVALID_OPERATION,
                "Unable to calculate rotation transform for new stream");
    }

    err = mDevice->setStreamTransform(streamId, transform);
    if (err != OK) {
        String8 msg = String8::format("Failed to set stream transform (stream id %d)",
                streamId);
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_INVALID_OPERATION, msg.string());
    }

    return res;
}

binder::Status CameraDeviceClient::createInputStream(
        int width, int height, int format, bool isMultiResolution,
        /*out*/
        int32_t* newStreamId) {

    ATRACE_CALL();
    ALOGV("%s (w = %d, h = %d, f = 0x%x, isMultiResolution %d)", __FUNCTION__,
            width, height, format, isMultiResolution);

    binder::Status res;
    if (!(res = checkPidStatus(__FUNCTION__)).isOk()) return res;

    Mutex::Autolock icl(mBinderSerializationLock);

    if (!mDevice.get()) {
        return STATUS_ERROR(CameraService::ERROR_DISCONNECTED, "Camera device no longer alive");
    }

    if (mInputStream.configured) {
        String8 msg = String8::format("Camera %s: Already has an input stream "
                "configured (ID %d)", mCameraIdStr.string(), mInputStream.id);
        ALOGE("%s: %s", __FUNCTION__, msg.string() );
        return STATUS_ERROR(CameraService::ERROR_ALREADY_EXISTS, msg.string());
    }

    int streamId = -1;
    status_t err = mDevice->createInputStream(width, height, format, isMultiResolution, &streamId);
    if (err == OK) {
        mInputStream.configured = true;
        mInputStream.width = width;
        mInputStream.height = height;
        mInputStream.format = format;
        mInputStream.id = streamId;

        ALOGV("%s: Camera %s: Successfully created a new input stream ID %d",
                __FUNCTION__, mCameraIdStr.string(), streamId);

        *newStreamId = streamId;
    } else {
        res = STATUS_ERROR_FMT(CameraService::ERROR_INVALID_OPERATION,
                "Camera %s: Error creating new input stream: %s (%d)", mCameraIdStr.string(),
                strerror(-err), err);
    }

    return res;
}

binder::Status CameraDeviceClient::getInputSurface(/*out*/ view::Surface *inputSurface) {

    binder::Status res;
    if (!(res = checkPidStatus(__FUNCTION__)).isOk()) return res;

    if (inputSurface == NULL) {
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, "Null input surface");
    }

    Mutex::Autolock icl(mBinderSerializationLock);
    if (!mDevice.get()) {
        return STATUS_ERROR(CameraService::ERROR_DISCONNECTED, "Camera device no longer alive");
    }
    sp<IGraphicBufferProducer> producer;
    status_t err = mDevice->getInputBufferProducer(&producer);
    if (err != OK) {
        res = STATUS_ERROR_FMT(CameraService::ERROR_INVALID_OPERATION,
                "Camera %s: Error getting input Surface: %s (%d)",
                mCameraIdStr.string(), strerror(-err), err);
    } else {
        inputSurface->name = String16("CameraInput");
        inputSurface->graphicBufferProducer = producer;
    }
    return res;
}

binder::Status CameraDeviceClient::updateOutputConfiguration(int streamId,
        const hardware::camera2::params::OutputConfiguration &outputConfiguration) {
    ATRACE_CALL();

    binder::Status res;
    if (!(res = checkPidStatus(__FUNCTION__)).isOk()) return res;

    Mutex::Autolock icl(mBinderSerializationLock);

    if (!mDevice.get()) {
        return STATUS_ERROR(CameraService::ERROR_DISCONNECTED, "Camera device no longer alive");
    }

    const std::vector<sp<IGraphicBufferProducer> >& bufferProducers =
            outputConfiguration.getGraphicBufferProducers();
    String8 physicalCameraId(outputConfiguration.getPhysicalCameraId());

    auto producerCount = bufferProducers.size();
    if (producerCount == 0) {
        ALOGE("%s: bufferProducers must not be empty", __FUNCTION__);
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                "bufferProducers must not be empty");
    }

    // The first output is the one associated with the output configuration.
    // It should always be present, valid and the corresponding stream id should match.
    sp<IBinder> binder = IInterface::asBinder(bufferProducers[0]);
    ssize_t index = mStreamMap.indexOfKey(binder);
    if (index == NAME_NOT_FOUND) {
        ALOGE("%s: Outputconfiguration is invalid", __FUNCTION__);
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                "OutputConfiguration is invalid");
    }
    if (mStreamMap.valueFor(binder).streamId() != streamId) {
        ALOGE("%s: Stream Id: %d provided doesn't match the id: %d in the stream map",
                __FUNCTION__, streamId, mStreamMap.valueFor(binder).streamId());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                "Stream id is invalid");
    }

    std::vector<size_t> removedSurfaceIds;
    std::vector<sp<IBinder>> removedOutputs;
    std::vector<sp<Surface>> newOutputs;
    std::vector<OutputStreamInfo> streamInfos;
    KeyedVector<sp<IBinder>, sp<IGraphicBufferProducer>> newOutputsMap;
    for (auto &it : bufferProducers) {
        newOutputsMap.add(IInterface::asBinder(it), it);
    }

    for (size_t i = 0; i < mStreamMap.size(); i++) {
        ssize_t idx = newOutputsMap.indexOfKey(mStreamMap.keyAt(i));
        if (idx == NAME_NOT_FOUND) {
            if (mStreamMap[i].streamId() == streamId) {
                removedSurfaceIds.push_back(mStreamMap[i].surfaceId());
                removedOutputs.push_back(mStreamMap.keyAt(i));
            }
        } else {
            if (mStreamMap[i].streamId() != streamId) {
                ALOGE("%s: Output surface already part of a different stream", __FUNCTION__);
                return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                        "Target Surface is invalid");
            }
            newOutputsMap.removeItemsAt(idx);
        }
    }
    const std::vector<int32_t> &sensorPixelModesUsed =
            outputConfiguration.getSensorPixelModesUsed();

    for (size_t i = 0; i < newOutputsMap.size(); i++) {
        OutputStreamInfo outInfo;
        sp<Surface> surface;
        res = SessionConfigurationUtils::createSurfaceFromGbp(outInfo,
                /*isStreamInfoValid*/ false, surface, newOutputsMap.valueAt(i), mCameraIdStr,
                mDevice->infoPhysical(physicalCameraId), sensorPixelModesUsed);
        if (!res.isOk())
            return res;

        streamInfos.push_back(outInfo);
        newOutputs.push_back(surface);
    }

    //Trivial case no changes required
    if (removedSurfaceIds.empty() && newOutputs.empty()) {
        return binder::Status::ok();
    }

    KeyedVector<sp<Surface>, size_t> outputMap;
    auto ret = mDevice->updateStream(streamId, newOutputs, streamInfos, removedSurfaceIds,
            &outputMap);
    if (ret != OK) {
        switch (ret) {
            case NAME_NOT_FOUND:
            case BAD_VALUE:
            case -EBUSY:
                res = STATUS_ERROR_FMT(CameraService::ERROR_ILLEGAL_ARGUMENT,
                        "Camera %s: Error updating stream: %s (%d)",
                        mCameraIdStr.string(), strerror(ret), ret);
                break;
            default:
                res = STATUS_ERROR_FMT(CameraService::ERROR_INVALID_OPERATION,
                        "Camera %s: Error updating stream: %s (%d)",
                        mCameraIdStr.string(), strerror(ret), ret);
                break;
        }
    } else {
        for (const auto &it : removedOutputs) {
            mStreamMap.removeItem(it);
        }

        for (size_t i = 0; i < outputMap.size(); i++) {
            mStreamMap.add(IInterface::asBinder(outputMap.keyAt(i)->getIGraphicBufferProducer()),
                    StreamSurfaceId(streamId, outputMap.valueAt(i)));
        }

        mConfiguredOutputs.replaceValueFor(streamId, outputConfiguration);

        ALOGV("%s: Camera %s: Successful stream ID %d update",
                  __FUNCTION__, mCameraIdStr.string(), streamId);
    }

    return res;
}

// Create a request object from a template.
binder::Status CameraDeviceClient::createDefaultRequest(int templateId,
        /*out*/
        hardware::camera2::impl::CameraMetadataNative* request)
{
    ATRACE_CALL();
    ALOGV("%s (templateId = 0x%x)", __FUNCTION__, templateId);

    binder::Status res;
    if (!(res = checkPidStatus(__FUNCTION__)).isOk()) return res;

    Mutex::Autolock icl(mBinderSerializationLock);

    if (!mDevice.get()) {
        return STATUS_ERROR(CameraService::ERROR_DISCONNECTED, "Camera device no longer alive");
    }

    status_t err;
    camera_request_template_t tempId = camera_request_template_t::CAMERA_TEMPLATE_COUNT;
    if (!(res = mapRequestTemplate(templateId, &tempId)).isOk()) return res;

    CameraMetadata metadata;
    if ( (err = mDevice->createDefaultRequest(tempId, &metadata) ) == OK &&
        request != NULL) {

        request->swap(metadata);
    } else if (err == BAD_VALUE) {
        res = STATUS_ERROR_FMT(CameraService::ERROR_ILLEGAL_ARGUMENT,
                "Camera %s: Template ID %d is invalid or not supported: %s (%d)",
                mCameraIdStr.string(), templateId, strerror(-err), err);

    } else {
        res = STATUS_ERROR_FMT(CameraService::ERROR_INVALID_OPERATION,
                "Camera %s: Error creating default request for template %d: %s (%d)",
                mCameraIdStr.string(), templateId, strerror(-err), err);
    }
    return res;
}

binder::Status CameraDeviceClient::getCameraInfo(
        /*out*/
        hardware::camera2::impl::CameraMetadataNative* info)
{
    ATRACE_CALL();
    ALOGV("%s", __FUNCTION__);

    binder::Status res;

    if (!(res = checkPidStatus(__FUNCTION__)).isOk()) return res;

    Mutex::Autolock icl(mBinderSerializationLock);

    if (!mDevice.get()) {
        return STATUS_ERROR(CameraService::ERROR_DISCONNECTED, "Camera device no longer alive");
    }

    if (info != NULL) {
        *info = mDevice->info(); // static camera metadata
        // TODO: merge with device-specific camera metadata
    }

    return res;
}

binder::Status CameraDeviceClient::waitUntilIdle()
{
    ATRACE_CALL();
    ALOGV("%s", __FUNCTION__);

    binder::Status res;
    if (!(res = checkPidStatus(__FUNCTION__)).isOk()) return res;

    Mutex::Autolock icl(mBinderSerializationLock);

    if (!mDevice.get()) {
        return STATUS_ERROR(CameraService::ERROR_DISCONNECTED, "Camera device no longer alive");
    }

    // FIXME: Also need check repeating burst.
    Mutex::Autolock idLock(mStreamingRequestIdLock);
    if (mStreamingRequestId != REQUEST_ID_NONE) {
        String8 msg = String8::format(
            "Camera %s: Try to waitUntilIdle when there are active streaming requests",
            mCameraIdStr.string());
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_INVALID_OPERATION, msg.string());
    }
    status_t err = mDevice->waitUntilDrained();
    if (err != OK) {
        res = STATUS_ERROR_FMT(CameraService::ERROR_INVALID_OPERATION,
                "Camera %s: Error waiting to drain: %s (%d)",
                mCameraIdStr.string(), strerror(-err), err);
    }
    ALOGV("%s Done", __FUNCTION__);
    return res;
}

binder::Status CameraDeviceClient::flush(
        /*out*/
        int64_t* lastFrameNumber) {
    ATRACE_CALL();
    ALOGV("%s", __FUNCTION__);

    binder::Status res;
    if (!(res = checkPidStatus(__FUNCTION__)).isOk()) return res;

    Mutex::Autolock icl(mBinderSerializationLock);

    if (!mDevice.get()) {
        return STATUS_ERROR(CameraService::ERROR_DISCONNECTED, "Camera device no longer alive");
    }

    Mutex::Autolock idLock(mStreamingRequestIdLock);
    mStreamingRequestId = REQUEST_ID_NONE;
    status_t err = mDevice->flush(lastFrameNumber);
    if (err != OK) {
        res = STATUS_ERROR_FMT(CameraService::ERROR_INVALID_OPERATION,
                "Camera %s: Error flushing device: %s (%d)", mCameraIdStr.string(), strerror(-err), err);
    }
    return res;
}

binder::Status CameraDeviceClient::prepare(int streamId) {
    ATRACE_CALL();
    ALOGV("%s", __FUNCTION__);

    binder::Status res;
    if (!(res = checkPidStatus(__FUNCTION__)).isOk()) return res;

    Mutex::Autolock icl(mBinderSerializationLock);

    // Guard against trying to prepare non-created streams
    ssize_t index = NAME_NOT_FOUND;
    for (size_t i = 0; i < mStreamMap.size(); ++i) {
        if (streamId == mStreamMap.valueAt(i).streamId()) {
            index = i;
            break;
        }
    }

    if (index == NAME_NOT_FOUND) {
        String8 msg = String8::format("Camera %s: Invalid stream ID (%d) specified, no stream "
              "with that ID exists", mCameraIdStr.string(), streamId);
        ALOGW("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    }

    // Also returns BAD_VALUE if stream ID was not valid, or stream already
    // has been used
    status_t err = mDevice->prepare(streamId);
    if (err == BAD_VALUE) {
        res = STATUS_ERROR_FMT(CameraService::ERROR_ILLEGAL_ARGUMENT,
                "Camera %s: Stream %d has already been used, and cannot be prepared",
                mCameraIdStr.string(), streamId);
    } else if (err != OK) {
        res = STATUS_ERROR_FMT(CameraService::ERROR_INVALID_OPERATION,
                "Camera %s: Error preparing stream %d: %s (%d)", mCameraIdStr.string(), streamId,
                strerror(-err), err);
    }
    return res;
}

binder::Status CameraDeviceClient::prepare2(int maxCount, int streamId) {
    ATRACE_CALL();
    ALOGV("%s", __FUNCTION__);

    binder::Status res;
    if (!(res = checkPidStatus(__FUNCTION__)).isOk()) return res;

    Mutex::Autolock icl(mBinderSerializationLock);

    // Guard against trying to prepare non-created streams
    ssize_t index = NAME_NOT_FOUND;
    for (size_t i = 0; i < mStreamMap.size(); ++i) {
        if (streamId == mStreamMap.valueAt(i).streamId()) {
            index = i;
            break;
        }
    }

    if (index == NAME_NOT_FOUND) {
        String8 msg = String8::format("Camera %s: Invalid stream ID (%d) specified, no stream "
              "with that ID exists", mCameraIdStr.string(), streamId);
        ALOGW("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    }

    if (maxCount <= 0) {
        String8 msg = String8::format("Camera %s: maxCount (%d) must be greater than 0",
                mCameraIdStr.string(), maxCount);
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    }

    // Also returns BAD_VALUE if stream ID was not valid, or stream already
    // has been used
    status_t err = mDevice->prepare(maxCount, streamId);
    if (err == BAD_VALUE) {
        res = STATUS_ERROR_FMT(CameraService::ERROR_ILLEGAL_ARGUMENT,
                "Camera %s: Stream %d has already been used, and cannot be prepared",
                mCameraIdStr.string(), streamId);
    } else if (err != OK) {
        res = STATUS_ERROR_FMT(CameraService::ERROR_INVALID_OPERATION,
                "Camera %s: Error preparing stream %d: %s (%d)", mCameraIdStr.string(), streamId,
                strerror(-err), err);
    }

    return res;
}

binder::Status CameraDeviceClient::tearDown(int streamId) {
    ATRACE_CALL();
    ALOGV("%s", __FUNCTION__);

    binder::Status res;
    if (!(res = checkPidStatus(__FUNCTION__)).isOk()) return res;

    Mutex::Autolock icl(mBinderSerializationLock);

    // Guard against trying to prepare non-created streams
    ssize_t index = NAME_NOT_FOUND;
    for (size_t i = 0; i < mStreamMap.size(); ++i) {
        if (streamId == mStreamMap.valueAt(i).streamId()) {
            index = i;
            break;
        }
    }

    if (index == NAME_NOT_FOUND) {
        String8 msg = String8::format("Camera %s: Invalid stream ID (%d) specified, no stream "
              "with that ID exists", mCameraIdStr.string(), streamId);
        ALOGW("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    }

    // Also returns BAD_VALUE if stream ID was not valid or if the stream is in
    // use
    status_t err = mDevice->tearDown(streamId);
    if (err == BAD_VALUE) {
        res = STATUS_ERROR_FMT(CameraService::ERROR_ILLEGAL_ARGUMENT,
                "Camera %s: Stream %d is still in use, cannot be torn down",
                mCameraIdStr.string(), streamId);
    } else if (err != OK) {
        res = STATUS_ERROR_FMT(CameraService::ERROR_INVALID_OPERATION,
                "Camera %s: Error tearing down stream %d: %s (%d)", mCameraIdStr.string(), streamId,
                strerror(-err), err);
    }

    return res;
}

binder::Status CameraDeviceClient::finalizeOutputConfigurations(int32_t streamId,
        const hardware::camera2::params::OutputConfiguration &outputConfiguration) {
    ATRACE_CALL();

    binder::Status res;
    if (!(res = checkPidStatus(__FUNCTION__)).isOk()) return res;

    Mutex::Autolock icl(mBinderSerializationLock);

    const std::vector<sp<IGraphicBufferProducer> >& bufferProducers =
            outputConfiguration.getGraphicBufferProducers();
    String8 physicalId(outputConfiguration.getPhysicalCameraId());

    if (bufferProducers.size() == 0) {
        ALOGE("%s: bufferProducers must not be empty", __FUNCTION__);
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, "Target Surface is invalid");
    }

    // streamId should be in mStreamMap if this stream already has a surface attached
    // to it. Otherwise, it should be in mDeferredStreams.
    bool streamIdConfigured = false;
    ssize_t deferredStreamIndex = NAME_NOT_FOUND;
    for (size_t i = 0; i < mStreamMap.size(); i++) {
        if (mStreamMap.valueAt(i).streamId() == streamId) {
            streamIdConfigured = true;
            break;
        }
    }
    for (size_t i = 0; i < mDeferredStreams.size(); i++) {
        if (streamId == mDeferredStreams[i]) {
            deferredStreamIndex = i;
            break;
        }

    }
    if (deferredStreamIndex == NAME_NOT_FOUND && !streamIdConfigured) {
        String8 msg = String8::format("Camera %s: deferred surface is set to a unknown stream"
                "(ID %d)", mCameraIdStr.string(), streamId);
        ALOGW("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    }

    if (mStreamInfoMap[streamId].finalized) {
        String8 msg = String8::format("Camera %s: finalizeOutputConfigurations has been called"
                " on stream ID %d", mCameraIdStr.string(), streamId);
        ALOGW("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    }

    if (!mDevice.get()) {
        return STATUS_ERROR(CameraService::ERROR_DISCONNECTED, "Camera device no longer alive");
    }

    std::vector<sp<Surface>> consumerSurfaces;
    const std::vector<int32_t> &sensorPixelModesUsed =
            outputConfiguration.getSensorPixelModesUsed();
    for (auto& bufferProducer : bufferProducers) {
        // Don't create multiple streams for the same target surface
        ssize_t index = mStreamMap.indexOfKey(IInterface::asBinder(bufferProducer));
        if (index != NAME_NOT_FOUND) {
            ALOGV("Camera %s: Surface already has a stream created "
                    " for it (ID %zd)", mCameraIdStr.string(), index);
            continue;
        }

        sp<Surface> surface;
        res = SessionConfigurationUtils::createSurfaceFromGbp(mStreamInfoMap[streamId],
                true /*isStreamInfoValid*/, surface, bufferProducer, mCameraIdStr,
                mDevice->infoPhysical(physicalId), sensorPixelModesUsed);

        if (!res.isOk())
            return res;

        consumerSurfaces.push_back(surface);
    }

    // Gracefully handle case where finalizeOutputConfigurations is called
    // without any new surface.
    if (consumerSurfaces.size() == 0) {
        mStreamInfoMap[streamId].finalized = true;
        return res;
    }

    // Finish the deferred stream configuration with the surface.
    status_t err;
    std::vector<int> consumerSurfaceIds;
    err = mDevice->setConsumerSurfaces(streamId, consumerSurfaces, &consumerSurfaceIds);
    if (err == OK) {
        for (size_t i = 0; i < consumerSurfaces.size(); i++) {
            sp<IBinder> binder = IInterface::asBinder(
                    consumerSurfaces[i]->getIGraphicBufferProducer());
            ALOGV("%s: mStreamMap add binder %p streamId %d, surfaceId %d", __FUNCTION__,
                    binder.get(), streamId, consumerSurfaceIds[i]);
            mStreamMap.add(binder, StreamSurfaceId(streamId, consumerSurfaceIds[i]));
        }
        if (deferredStreamIndex != NAME_NOT_FOUND) {
            mDeferredStreams.removeItemsAt(deferredStreamIndex);
        }
        mStreamInfoMap[streamId].finalized = true;
        mConfiguredOutputs.replaceValueFor(streamId, outputConfiguration);
    } else if (err == NO_INIT) {
        res = STATUS_ERROR_FMT(CameraService::ERROR_ILLEGAL_ARGUMENT,
                "Camera %s: Deferred surface is invalid: %s (%d)",
                mCameraIdStr.string(), strerror(-err), err);
    } else {
        res = STATUS_ERROR_FMT(CameraService::ERROR_INVALID_OPERATION,
                "Camera %s: Error setting output stream deferred surface: %s (%d)",
                mCameraIdStr.string(), strerror(-err), err);
    }

    return res;
}

binder::Status CameraDeviceClient::setCameraAudioRestriction(int32_t mode) {
    ATRACE_CALL();
    binder::Status res;
    if (!(res = checkPidStatus(__FUNCTION__)).isOk()) return res;

    if (!isValidAudioRestriction(mode)) {
        String8 msg = String8::format("Camera %s: invalid audio restriction mode %d",
                mCameraIdStr.string(), mode);
        ALOGW("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    }

    Mutex::Autolock icl(mBinderSerializationLock);
    BasicClient::setAudioRestriction(mode);
    return binder::Status::ok();
}

binder::Status CameraDeviceClient::getGlobalAudioRestriction(/*out*/ int32_t* outMode) {
    ATRACE_CALL();
    binder::Status res;
    if (!(res = checkPidStatus(__FUNCTION__)).isOk()) return res;
    Mutex::Autolock icl(mBinderSerializationLock);
    if (outMode != nullptr) {
        *outMode = BasicClient::getServiceAudioRestriction();
    }
    return binder::Status::ok();
}

status_t CameraDeviceClient::setRotateAndCropOverride(uint8_t rotateAndCrop) {
    if (rotateAndCrop > ANDROID_SCALER_ROTATE_AND_CROP_AUTO) return BAD_VALUE;

    return mDevice->setRotateAndCropAutoBehavior(
        static_cast<camera_metadata_enum_android_scaler_rotate_and_crop_t>(rotateAndCrop));
}

bool CameraDeviceClient::supportsCameraMute() {
    return mDevice->supportsCameraMute();
}

status_t CameraDeviceClient::setCameraMute(bool enabled) {
    return mDevice->setCameraMute(enabled);
}

binder::Status CameraDeviceClient::switchToOffline(
        const sp<hardware::camera2::ICameraDeviceCallbacks>& cameraCb,
        const std::vector<int>& offlineOutputIds,
        /*out*/
        sp<hardware::camera2::ICameraOfflineSession>* session) {
    ATRACE_CALL();

    binder::Status res;
    if (!(res = checkPidStatus(__FUNCTION__)).isOk()) return res;

    Mutex::Autolock icl(mBinderSerializationLock);

    if (!mDevice.get()) {
        return STATUS_ERROR(CameraService::ERROR_DISCONNECTED, "Camera device no longer alive");
    }

    if (offlineOutputIds.empty()) {
        String8 msg = String8::format("Offline surfaces must not be empty");
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    }

    if (session == nullptr) {
        String8 msg = String8::format("Invalid offline session");
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    }

    std::vector<int32_t> offlineStreamIds;
    offlineStreamIds.reserve(offlineOutputIds.size());
    KeyedVector<sp<IBinder>, sp<CompositeStream>> offlineCompositeStreamMap;
    for (const auto& streamId : offlineOutputIds) {
        ssize_t index = mConfiguredOutputs.indexOfKey(streamId);
        if (index == NAME_NOT_FOUND) {
            String8 msg = String8::format("Offline surface with id: %d is not registered",
                    streamId);
            ALOGE("%s: %s", __FUNCTION__, msg.string());
            return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
        }

        if (!mStreamInfoMap[streamId].supportsOffline) {
            String8 msg = String8::format("Offline surface with id: %d doesn't support "
                    "offline mode", streamId);
            ALOGE("%s: %s", __FUNCTION__, msg.string());
            return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
        }

        bool isCompositeStream = false;
        for (const auto& gbp : mConfiguredOutputs[streamId].getGraphicBufferProducers()) {
            sp<Surface> s = new Surface(gbp, false /*controlledByApp*/);
            isCompositeStream = camera3::DepthCompositeStream::isDepthCompositeStream(s) |
                camera3::HeicCompositeStream::isHeicCompositeStream(s);
            if (isCompositeStream) {
                auto compositeIdx = mCompositeStreamMap.indexOfKey(IInterface::asBinder(gbp));
                if (compositeIdx == NAME_NOT_FOUND) {
                    ALOGE("%s: Unknown composite stream", __FUNCTION__);
                    return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                            "Unknown composite stream");
                }

                mCompositeStreamMap.valueAt(compositeIdx)->insertCompositeStreamIds(
                        &offlineStreamIds);
                offlineCompositeStreamMap.add(mCompositeStreamMap.keyAt(compositeIdx),
                        mCompositeStreamMap.valueAt(compositeIdx));
                break;
            }
        }

        if (!isCompositeStream) {
            offlineStreamIds.push_back(streamId);
        }
    }

    sp<CameraOfflineSessionBase> offlineSession;
    auto ret = mDevice->switchToOffline(offlineStreamIds, &offlineSession);
    if (ret != OK) {
        return STATUS_ERROR_FMT(CameraService::ERROR_ILLEGAL_ARGUMENT,
                "Camera %s: Error switching to offline mode: %s (%d)",
                mCameraIdStr.string(), strerror(ret), ret);
    }

    sp<CameraOfflineSessionClient> offlineClient;
    if (offlineSession.get() != nullptr) {
        offlineClient = new CameraOfflineSessionClient(sCameraService,
                offlineSession, offlineCompositeStreamMap, cameraCb, mClientPackageName,
                mClientFeatureId, mCameraIdStr, mCameraFacing, mOrientation, mClientPid, mClientUid,
                mServicePid);
        ret = sCameraService->addOfflineClient(mCameraIdStr, offlineClient);
    }

    if (ret == OK) {
        // A successful offline session switch must reset the current camera client
        // and release any resources occupied by previously configured streams.
        mStreamMap.clear();
        mConfiguredOutputs.clear();
        mDeferredStreams.clear();
        mStreamInfoMap.clear();
        mCompositeStreamMap.clear();
        mInputStream = {false, 0, 0, 0, 0};
    } else {
        switch(ret) {
            case BAD_VALUE:
                return STATUS_ERROR_FMT(CameraService::ERROR_ILLEGAL_ARGUMENT,
                        "Illegal argument to HAL module for camera \"%s\"", mCameraIdStr.c_str());
            case TIMED_OUT:
                return STATUS_ERROR_FMT(CameraService::ERROR_CAMERA_IN_USE,
                        "Camera \"%s\" is already open", mCameraIdStr.c_str());
            default:
                return STATUS_ERROR_FMT(CameraService::ERROR_INVALID_OPERATION,
                        "Failed to initialize camera \"%s\": %s (%d)", mCameraIdStr.c_str(),
                        strerror(-ret), ret);
        }
    }

    *session = offlineClient;

    return binder::Status::ok();
}

status_t CameraDeviceClient::dump(int fd, const Vector<String16>& args) {
    return BasicClient::dump(fd, args);
}

status_t CameraDeviceClient::dumpClient(int fd, const Vector<String16>& args) {
    dprintf(fd, "  CameraDeviceClient[%s] (%p) dump:\n",
            mCameraIdStr.string(),
            (getRemoteCallback() != NULL ?
                    IInterface::asBinder(getRemoteCallback()).get() : NULL) );
    dprintf(fd, "    Current client UID %u\n", mClientUid);

    dprintf(fd, "    State:\n");
    dprintf(fd, "      Request ID counter: %d\n", mRequestIdCounter);
    if (mInputStream.configured) {
        dprintf(fd, "      Current input stream ID: %d\n", mInputStream.id);
    } else {
        dprintf(fd, "      No input stream configured.\n");
    }
    if (!mStreamMap.isEmpty()) {
        dprintf(fd, "      Current output stream/surface IDs:\n");
        for (size_t i = 0; i < mStreamMap.size(); i++) {
            dprintf(fd, "        Stream %d Surface %d\n",
                                mStreamMap.valueAt(i).streamId(),
                                mStreamMap.valueAt(i).surfaceId());
        }
    } else if (!mDeferredStreams.isEmpty()) {
        dprintf(fd, "      Current deferred surface output stream IDs:\n");
        for (auto& streamId : mDeferredStreams) {
            dprintf(fd, "        Stream %d\n", streamId);
        }
    } else {
        dprintf(fd, "      No output streams configured.\n");
    }
    // TODO: print dynamic/request section from most recent requests
    mFrameProcessor->dump(fd, args);

    return dumpDevice(fd, args);
}

void CameraDeviceClient::notifyError(int32_t errorCode,
                                     const CaptureResultExtras& resultExtras) {
    // Thread safe. Don't bother locking.
    sp<hardware::camera2::ICameraDeviceCallbacks> remoteCb = getRemoteCallback();

    // Composites can have multiple internal streams. Error notifications coming from such internal
    // streams may need to remain within camera service.
    bool skipClientNotification = false;
    for (size_t i = 0; i < mCompositeStreamMap.size(); i++) {
        skipClientNotification |= mCompositeStreamMap.valueAt(i)->onError(errorCode, resultExtras);
    }

    if ((remoteCb != 0) && (!skipClientNotification)) {
        remoteCb->onDeviceError(errorCode, resultExtras);
    }
}

void CameraDeviceClient::notifyRepeatingRequestError(long lastFrameNumber) {
    sp<hardware::camera2::ICameraDeviceCallbacks> remoteCb = getRemoteCallback();

    if (remoteCb != 0) {
        remoteCb->onRepeatingRequestError(lastFrameNumber, mStreamingRequestId);
    }

    Mutex::Autolock idLock(mStreamingRequestIdLock);
    mStreamingRequestId = REQUEST_ID_NONE;
}

void CameraDeviceClient::notifyIdle(
        int64_t requestCount, int64_t resultErrorCount, bool deviceError,
        const std::vector<hardware::CameraStreamStats>& streamStats) {
    // Thread safe. Don't bother locking.
    sp<hardware::camera2::ICameraDeviceCallbacks> remoteCb = getRemoteCallback();

    if (remoteCb != 0) {
        remoteCb->onDeviceIdle();
    }
    Camera2ClientBase::notifyIdle(requestCount, resultErrorCount, deviceError, streamStats);
}

void CameraDeviceClient::notifyShutter(const CaptureResultExtras& resultExtras,
        nsecs_t timestamp) {
    // Thread safe. Don't bother locking.
    sp<hardware::camera2::ICameraDeviceCallbacks> remoteCb = getRemoteCallback();
    if (remoteCb != 0) {
        remoteCb->onCaptureStarted(resultExtras, timestamp);
    }
    Camera2ClientBase::notifyShutter(resultExtras, timestamp);

    for (size_t i = 0; i < mCompositeStreamMap.size(); i++) {
        mCompositeStreamMap.valueAt(i)->onShutter(resultExtras, timestamp);
    }
}

void CameraDeviceClient::notifyPrepared(int streamId) {
    // Thread safe. Don't bother locking.
    sp<hardware::camera2::ICameraDeviceCallbacks> remoteCb = getRemoteCallback();
    if (remoteCb != 0) {
        remoteCb->onPrepared(streamId);
    }
}

void CameraDeviceClient::notifyRequestQueueEmpty() {
    // Thread safe. Don't bother locking.
    sp<hardware::camera2::ICameraDeviceCallbacks> remoteCb = getRemoteCallback();
    if (remoteCb != 0) {
        remoteCb->onRequestQueueEmpty();
    }
}

void CameraDeviceClient::detachDevice() {
    if (mDevice == 0) return;

    nsecs_t startTime = systemTime();
    ALOGV("Camera %s: Stopping processors", mCameraIdStr.string());

    mFrameProcessor->removeListener(camera2::FrameProcessorBase::FRAME_PROCESSOR_LISTENER_MIN_ID,
                                    camera2::FrameProcessorBase::FRAME_PROCESSOR_LISTENER_MAX_ID,
                                    /*listener*/this);
    mFrameProcessor->requestExit();
    ALOGV("Camera %s: Waiting for threads", mCameraIdStr.string());
    mFrameProcessor->join();
    ALOGV("Camera %s: Disconnecting device", mCameraIdStr.string());

    // WORKAROUND: HAL refuses to disconnect while there's streams in flight
    {
        int64_t lastFrameNumber;
        status_t code;
        if ((code = mDevice->flush(&lastFrameNumber)) != OK) {
            ALOGE("%s: flush failed with code 0x%x", __FUNCTION__, code);
        }

        if ((code = mDevice->waitUntilDrained()) != OK) {
            ALOGE("%s: waitUntilDrained failed with code 0x%x", __FUNCTION__,
                  code);
        }
    }

    for (size_t i = 0; i < mCompositeStreamMap.size(); i++) {
        auto ret = mCompositeStreamMap.valueAt(i)->deleteInternalStreams();
        if (ret != OK) {
            ALOGE("%s: Failed removing composite stream  %s (%d)", __FUNCTION__,
                    strerror(-ret), ret);
        }
    }
    mCompositeStreamMap.clear();

    Camera2ClientBase::detachDevice();

    int32_t closeLatencyMs = ns2ms(systemTime() - startTime);
    CameraServiceProxyWrapper::logClose(mCameraIdStr, closeLatencyMs);
}

/** Device-related methods */
void CameraDeviceClient::onResultAvailable(const CaptureResult& result) {
    ATRACE_CALL();
    ALOGV("%s", __FUNCTION__);

    // Thread-safe. No lock necessary.
    sp<hardware::camera2::ICameraDeviceCallbacks> remoteCb = mRemoteCallback;
    if (remoteCb != NULL) {
        remoteCb->onResultReceived(result.mMetadata, result.mResultExtras,
                result.mPhysicalMetadatas);
    }

    for (size_t i = 0; i < mCompositeStreamMap.size(); i++) {
        mCompositeStreamMap.valueAt(i)->onResultAvailable(result);
    }
}

binder::Status CameraDeviceClient::checkPidStatus(const char* checkLocation) {
    if (mDisconnected) {
        return STATUS_ERROR(CameraService::ERROR_DISCONNECTED,
                "The camera device has been disconnected");
    }
    status_t res = checkPid(checkLocation);
    return (res == OK) ? binder::Status::ok() :
            STATUS_ERROR(CameraService::ERROR_PERMISSION_DENIED,
                    "Attempt to use camera from a different process than original client");
}

// TODO: move to Camera2ClientBase
bool CameraDeviceClient::enforceRequestPermissions(CameraMetadata& metadata) {

    const int pid = CameraThreadState::getCallingPid();
    const int selfPid = getpid();
    camera_metadata_entry_t entry;

    /**
     * Mixin default important security values
     * - android.led.transmit = defaulted ON
     */
    CameraMetadata staticInfo = mDevice->info();
    entry = staticInfo.find(ANDROID_LED_AVAILABLE_LEDS);
    for(size_t i = 0; i < entry.count; ++i) {
        uint8_t led = entry.data.u8[i];

        switch(led) {
            case ANDROID_LED_AVAILABLE_LEDS_TRANSMIT: {
                uint8_t transmitDefault = ANDROID_LED_TRANSMIT_ON;
                if (!metadata.exists(ANDROID_LED_TRANSMIT)) {
                    metadata.update(ANDROID_LED_TRANSMIT,
                                    &transmitDefault, 1);
                }
                break;
            }
        }
    }

    // We can do anything!
    if (pid == selfPid) {
        return true;
    }

    /**
     * Permission check special fields in the request
     * - android.led.transmit = android.permission.CAMERA_DISABLE_TRANSMIT
     */
    entry = metadata.find(ANDROID_LED_TRANSMIT);
    if (entry.count > 0 && entry.data.u8[0] != ANDROID_LED_TRANSMIT_ON) {
        String16 permissionString =
            String16("android.permission.CAMERA_DISABLE_TRANSMIT_LED");
        if (!checkCallingPermission(permissionString)) {
            const int uid = CameraThreadState::getCallingUid();
            ALOGE("Permission Denial: "
                  "can't disable transmit LED pid=%d, uid=%d", pid, uid);
            return false;
        }
    }

    return true;
}

status_t CameraDeviceClient::getRotationTransformLocked(int32_t* transform) {
    ALOGV("%s: begin", __FUNCTION__);

    const CameraMetadata& staticInfo = mDevice->info();
    return CameraUtils::getRotationTransform(staticInfo, transform);
}

binder::Status CameraDeviceClient::mapRequestTemplate(int templateId,
        camera_request_template_t* tempId /*out*/) {
    binder::Status ret = binder::Status::ok();

    if (tempId == nullptr) {
        ret = STATUS_ERROR_FMT(CameraService::ERROR_ILLEGAL_ARGUMENT,
                "Camera %s: Invalid template argument", mCameraIdStr.string());
        return ret;
    }
    switch(templateId) {
        case ICameraDeviceUser::TEMPLATE_PREVIEW:
            *tempId = camera_request_template_t::CAMERA_TEMPLATE_PREVIEW;
            break;
        case ICameraDeviceUser::TEMPLATE_RECORD:
            *tempId = camera_request_template_t::CAMERA_TEMPLATE_VIDEO_RECORD;
            break;
        case ICameraDeviceUser::TEMPLATE_STILL_CAPTURE:
            *tempId = camera_request_template_t::CAMERA_TEMPLATE_STILL_CAPTURE;
            break;
        case ICameraDeviceUser::TEMPLATE_VIDEO_SNAPSHOT:
            *tempId = camera_request_template_t::CAMERA_TEMPLATE_VIDEO_SNAPSHOT;
            break;
        case ICameraDeviceUser::TEMPLATE_ZERO_SHUTTER_LAG:
            *tempId = camera_request_template_t::CAMERA_TEMPLATE_ZERO_SHUTTER_LAG;
            break;
        case ICameraDeviceUser::TEMPLATE_MANUAL:
            *tempId = camera_request_template_t::CAMERA_TEMPLATE_MANUAL;
            break;
        default:
            ret = STATUS_ERROR_FMT(CameraService::ERROR_ILLEGAL_ARGUMENT,
                    "Camera %s: Template ID %d is invalid or not supported",
                    mCameraIdStr.string(), templateId);
            return ret;
    }

    return ret;
}

const CameraMetadata &CameraDeviceClient::getStaticInfo(const String8 &cameraId) {
    if (mDevice->getId() == cameraId) {
        return mDevice->info();
    }
    return mDevice->infoPhysical(cameraId);
}

bool CameraDeviceClient::isUltraHighResolutionSensor(const String8 &cameraId) {
    const CameraMetadata &deviceInfo = getStaticInfo(cameraId);
    return SessionConfigurationUtils::isUltraHighResolutionSensor(deviceInfo);
}

bool CameraDeviceClient::isSensorPixelModeConsistent(
        const std::list<int> &streamIdList, const CameraMetadata &settings) {
    // First we get the sensorPixelMode from the settings metadata.
    int32_t sensorPixelMode = ANDROID_SENSOR_PIXEL_MODE_DEFAULT;
    camera_metadata_ro_entry sensorPixelModeEntry = settings.find(ANDROID_SENSOR_PIXEL_MODE);
    if (sensorPixelModeEntry.count != 0) {
        sensorPixelMode = sensorPixelModeEntry.data.u8[0];
        if (sensorPixelMode != ANDROID_SENSOR_PIXEL_MODE_DEFAULT &&
            sensorPixelMode != ANDROID_SENSOR_PIXEL_MODE_MAXIMUM_RESOLUTION) {
            ALOGE("%s: Request sensor pixel mode not is not one of the valid values %d",
                      __FUNCTION__, sensorPixelMode);
            return false;
        }
    }
    // Check whether each stream has max resolution allowed.
    bool consistent = true;
    for (auto it : streamIdList) {
        auto const streamInfoIt = mStreamInfoMap.find(it);
        if (streamInfoIt == mStreamInfoMap.end()) {
            ALOGE("%s: stream id %d not created, skipping", __FUNCTION__, it);
            return false;
        }
        consistent =
                streamInfoIt->second.sensorPixelModesUsed.find(sensorPixelMode) !=
                        streamInfoIt->second.sensorPixelModesUsed.end();
        if (!consistent) {
            ALOGE("sensorPixelMode used %i not consistent with configured modes", sensorPixelMode);
            for (auto m : streamInfoIt->second.sensorPixelModesUsed) {
                ALOGE("sensor pixel mode used list: %i", m);
            }
            break;
        }
    }

    return consistent;
}

} // namespace android
