/*
 * Copyright (C) 2021 The Android Open Source Project
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

#define LOG_TAG "Camera3DeviceInjectionMethods"
#define ATRACE_TAG ATRACE_TAG_CAMERA
//#define LOG_NDEBUG 0

#include <utils/Log.h>
#include <utils/Trace.h>

#include "common/CameraProviderManager.h"
#include "device3/Camera3Device.h"

namespace android {

using hardware::camera::device::V3_2::ICameraDeviceSession;

Camera3Device::Camera3DeviceInjectionMethods::Camera3DeviceInjectionMethods(
        wp<Camera3Device> parent)
        : mParent(parent) {
    ALOGV("%s: Created injection camera methods", __FUNCTION__);
}

Camera3Device::Camera3DeviceInjectionMethods::~Camera3DeviceInjectionMethods() {
    ALOGV("%s: Removed injection camera methods", __FUNCTION__);
    injectionDisconnectImpl();
}

status_t Camera3Device::Camera3DeviceInjectionMethods::injectionInitialize(
        const String8& injectedCamId, sp<CameraProviderManager> manager,
        const sp<android::hardware::camera::device::V3_2::ICameraDeviceCallback>&
                callback) {
    ATRACE_CALL();
    Mutex::Autolock lock(mInjectionLock);

    if (manager == nullptr) {
        ALOGE("%s: manager does not exist!", __FUNCTION__);
        return INVALID_OPERATION;
    }

    sp<Camera3Device> parent = mParent.promote();
    if (parent == nullptr) {
        ALOGE("%s: parent does not exist!", __FUNCTION__);
        return INVALID_OPERATION;
    }

    mInjectedCamId = injectedCamId;
    sp<ICameraDeviceSession> session;
    ATRACE_BEGIN("Injection CameraHal::openSession");
    status_t res = manager->openSession(injectedCamId.string(), callback,
                                          /*out*/ &session);
    ATRACE_END();
    if (res != OK) {
        ALOGE("Injection camera could not open camera session: %s (%d)",
                strerror(-res), res);
        return res;
    }

    std::shared_ptr<RequestMetadataQueue> queue;
    auto requestQueueRet =
        session->getCaptureRequestMetadataQueue([&queue](const auto& descriptor) {
            queue = std::make_shared<RequestMetadataQueue>(descriptor);
            if (!queue->isValid() || queue->availableToWrite() <= 0) {
                ALOGE("Injection camera HAL returns empty request metadata fmq, not "
                        "use it");
                queue = nullptr;
                // don't use the queue onwards.
            }
        });
    if (!requestQueueRet.isOk()) {
        ALOGE("Injection camera transaction error when getting request metadata fmq: "
                "%s, not use it", requestQueueRet.description().c_str());
        return DEAD_OBJECT;
    }

    std::unique_ptr<ResultMetadataQueue>& resQueue = parent->mResultMetadataQueue;
    auto resultQueueRet = session->getCaptureResultMetadataQueue(
        [&resQueue](const auto& descriptor) {
            resQueue = std::make_unique<ResultMetadataQueue>(descriptor);
            if (!resQueue->isValid() || resQueue->availableToWrite() <= 0) {
                ALOGE("Injection camera HAL returns empty result metadata fmq, not use "
                        "it");
                resQueue = nullptr;
                // Don't use the resQueue onwards.
            }
        });
    if (!resultQueueRet.isOk()) {
        ALOGE("Injection camera transaction error when getting result metadata queue "
                "from camera session: %s", resultQueueRet.description().c_str());
        return DEAD_OBJECT;
    }
    IF_ALOGV() {
        session->interfaceChain(
                [](::android::hardware::hidl_vec<::android::hardware::hidl_string>
                        interfaceChain) {
                        ALOGV("Injection camera session interface chain:");
                        for (const auto& iface : interfaceChain) {
                            ALOGV("  %s", iface.c_str());
                        }
                });
    }

    ALOGV("%s: Injection camera interface = new HalInterface()", __FUNCTION__);
    mInjectedCamHalInterface =
            new HalInterface(session, queue, parent->mUseHalBufManager,
                       parent->mSupportOfflineProcessing);
    if (mInjectedCamHalInterface == nullptr) {
        ALOGE("%s: mInjectedCamHalInterface does not exist!", __FUNCTION__);
        return DEAD_OBJECT;
    }

    return OK;
}

status_t Camera3Device::Camera3DeviceInjectionMethods::injectCamera(
        camera3::camera_stream_configuration& injectionConfig,
        std::vector<uint32_t>& injectionBufferSizes) {
    status_t res = NO_ERROR;
    mInjectionConfig = injectionConfig;
    mInjectionBufferSizes = injectionBufferSizes;

    if (mInjectedCamHalInterface == nullptr) {
        ALOGE("%s: mInjectedCamHalInterface does not exist!", __FUNCTION__);
        return DEAD_OBJECT;
    }

    sp<Camera3Device> parent = mParent.promote();
    if (parent == nullptr) {
        ALOGE("%s: parent does not exist!", __FUNCTION__);
        return INVALID_OPERATION;
    }

    nsecs_t maxExpectedDuration = parent->getExpectedInFlightDuration();
    bool wasActive = false;
    if (parent->mStatus == STATUS_ACTIVE) {
        ALOGV("%s: Let the device be IDLE and the request thread is paused",
                __FUNCTION__);
        parent->mPauseStateNotify = true;
        res = parent->internalPauseAndWaitLocked(maxExpectedDuration);
        if (res != OK) {
            ALOGE("%s: Can't pause captures to inject camera!", __FUNCTION__);
            return res;
        }
        wasActive = true;
    }

    ALOGV("%s: Injection camera: replaceHalInterface", __FUNCTION__);
    res = replaceHalInterface(mInjectedCamHalInterface, true);
    if (res != OK) {
        ALOGE("%s: Failed to replace the new HalInterface!", __FUNCTION__);
        injectionDisconnectImpl();
        return res;
    }

    res = parent->mRequestThread->setHalInterface(mInjectedCamHalInterface);
    if (res != OK) {
        ALOGE("%s: Failed to set new HalInterface in RequestThread!", __FUNCTION__);
        replaceHalInterface(mBackupHalInterface, false);
        injectionDisconnectImpl();
        return res;
    }

    parent->mNeedConfig = true;
    res = injectionConfigureStreams(injectionConfig, injectionBufferSizes);
    parent->mNeedConfig = false;
    if (res != OK) {
        ALOGE("Can't injectionConfigureStreams device for streams:  %d: %s "
                "(%d)", parent->mNextStreamId, strerror(-res), res);
        replaceHalInterface(mBackupHalInterface, false);
        injectionDisconnectImpl();
        return res;
    }

    if (wasActive) {
        ALOGV("%s: Restarting activity to inject camera", __FUNCTION__);
        // Reuse current operating mode and session parameters for new stream
        // config.
        parent->internalUpdateStatusLocked(STATUS_ACTIVE);
    }

    return OK;
}

status_t Camera3Device::Camera3DeviceInjectionMethods::stopInjection() {
    status_t res = NO_ERROR;

    sp<Camera3Device> parent = mParent.promote();
    if (parent == nullptr) {
        ALOGE("%s: parent does not exist!", __FUNCTION__);
        return DEAD_OBJECT;
    }

    nsecs_t maxExpectedDuration = parent->getExpectedInFlightDuration();
    bool wasActive = false;
    if (parent->mStatus == STATUS_ACTIVE) {
        ALOGV("%s: Let the device be IDLE and the request thread is paused",
                __FUNCTION__);
        parent->mPauseStateNotify = true;
        res = parent->internalPauseAndWaitLocked(maxExpectedDuration);
        if (res != OK) {
            ALOGE("%s: Can't pause captures to stop injection!", __FUNCTION__);
            return res;
        }
        wasActive = true;
    }

    res = replaceHalInterface(mBackupHalInterface, false);
    if (res != OK) {
        ALOGE("%s: Failed to restore the backup HalInterface!", __FUNCTION__);
        injectionDisconnectImpl();
        return res;
    }
    injectionDisconnectImpl();

    if (wasActive) {
        ALOGV("%s: Restarting activity to stop injection", __FUNCTION__);
        // Reuse current operating mode and session parameters for new stream
        // config.
        parent->internalUpdateStatusLocked(STATUS_ACTIVE);
    }

    return OK;
}

bool Camera3Device::Camera3DeviceInjectionMethods::isInjecting() {
    if (mInjectedCamHalInterface == nullptr) {
        return false;
    } else {
        return true;
    }
}

const String8& Camera3Device::Camera3DeviceInjectionMethods::getInjectedCamId()
        const {
    return mInjectedCamId;
}

void Camera3Device::Camera3DeviceInjectionMethods::getInjectionConfig(
        /*out*/ camera3::camera_stream_configuration* injectionConfig,
        /*out*/ std::vector<uint32_t>* injectionBufferSizes) {
    if (injectionConfig == nullptr || injectionBufferSizes == nullptr) {
        ALOGE("%s: Injection configuration arguments must not be null!", __FUNCTION__);
        return;
    }

    *injectionConfig = mInjectionConfig;
    *injectionBufferSizes = mInjectionBufferSizes;
}


status_t Camera3Device::Camera3DeviceInjectionMethods::injectionConfigureStreams(
        camera3::camera_stream_configuration& injectionConfig,
        std::vector<uint32_t>& injectionBufferSizes) {
    ATRACE_CALL();
    status_t res = NO_ERROR;

    sp<Camera3Device> parent = mParent.promote();
    if (parent == nullptr) {
        ALOGE("%s: parent does not exist!", __FUNCTION__);
        return INVALID_OPERATION;
    }

    if (parent->mOperatingMode < 0) {
        ALOGE("Invalid operating mode: %d", parent->mOperatingMode);
        return BAD_VALUE;
    }

    // Start configuring the streams
    ALOGV("%s: Injection camera %s: Starting stream configuration", __FUNCTION__,
            mInjectedCamId.string());

    parent->mPreparerThread->pause();

    // Do the HAL configuration; will potentially touch stream
    // max_buffers, usage, and priv fields, as well as data_space and format
    // fields for IMPLEMENTATION_DEFINED formats.

    const camera_metadata_t* sessionBuffer = parent->mSessionParams.getAndLock();
    res = mInjectedCamHalInterface->configureInjectedStreams(
            sessionBuffer, &injectionConfig, injectionBufferSizes,
            parent->mDeviceInfo);
    parent->mSessionParams.unlock(sessionBuffer);

    if (res == BAD_VALUE) {
        // HAL rejected this set of streams as unsupported, clean up config
        // attempt and return to unconfigured state
        ALOGE("Set of requested outputs not supported by HAL");
        parent->cancelStreamsConfigurationLocked();
        return BAD_VALUE;
    } else if (res != OK) {
        // Some other kind of error from configure_streams - this is not
        // expected
        ALOGE("Unable to configure streams with HAL: %s (%d)", strerror(-res),
                  res);
        return res;
    }

    for (size_t i = 0; i < parent->mOutputStreams.size(); i++) {
        sp<camera3::Camera3OutputStreamInterface> outputStream =
                parent->mOutputStreams[i];
        mInjectedCamHalInterface->onStreamReConfigured(outputStream->getId());
    }

    // Request thread needs to know to avoid using repeat-last-settings protocol
    // across configure_streams() calls
    parent->mRequestThread->configurationComplete(
            parent->mIsConstrainedHighSpeedConfiguration, parent->mSessionParams,
            parent->mGroupIdPhysicalCameraMap);

    parent->internalUpdateStatusLocked(STATUS_CONFIGURED);

    ALOGV("%s: Injection camera %s: Stream configuration complete", __FUNCTION__,
            mInjectedCamId.string());

    auto rc = parent->mPreparerThread->resume();

    if (rc != OK) {
        ALOGE("%s: Injection camera %s: Preparer thread failed to resume!",
                 __FUNCTION__, mInjectedCamId.string());
        return rc;
    }

    return OK;
}

void Camera3Device::Camera3DeviceInjectionMethods::injectionDisconnectImpl() {
    ATRACE_CALL();
    ALOGI("%s: Injection camera disconnect", __FUNCTION__);

    mBackupHalInterface = nullptr;
    HalInterface* interface = nullptr;
    {
        Mutex::Autolock lock(mInjectionLock);
        if (mInjectedCamHalInterface != nullptr) {
            interface = mInjectedCamHalInterface.get();
            // Call close without internal mutex held, as the HAL close may need
            // to wait on assorted callbacks,etc, to complete before it can
            // return.
        }
    }

    if (interface != nullptr) {
        interface->close();
    }

    {
        Mutex::Autolock lock(mInjectionLock);
        if (mInjectedCamHalInterface != nullptr) {
            mInjectedCamHalInterface->clear();
            mInjectedCamHalInterface = nullptr;
        }
    }
}

status_t Camera3Device::Camera3DeviceInjectionMethods::replaceHalInterface(
        sp<HalInterface> newHalInterface, bool keepBackup) {
    Mutex::Autolock lock(mInjectionLock);
    if (newHalInterface.get() == nullptr) {
        ALOGE("%s: The newHalInterface does not exist, to stop replacing.",
                __FUNCTION__);
        return DEAD_OBJECT;
    }

    sp<Camera3Device> parent = mParent.promote();
    if (parent == nullptr) {
        ALOGE("%s: parent does not exist!", __FUNCTION__);
        return INVALID_OPERATION;
    }

    if (keepBackup && mBackupHalInterface == nullptr) {
        mBackupHalInterface = parent->mInterface;
    } else if (!keepBackup) {
        mBackupHalInterface = nullptr;
    }
    parent->mInterface = newHalInterface;

    return OK;
}

};  // namespace android
