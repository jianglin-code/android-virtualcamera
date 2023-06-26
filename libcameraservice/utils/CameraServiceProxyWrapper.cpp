/*
 * Copyright (C) 2020 The Android Open Source Project
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

#define LOG_TAG "CameraServiceProxyWrapper"
#define ATRACE_TAG ATRACE_TAG_CAMERA
//#define LOG_NDEBUG 0

#include <inttypes.h>
#include <utils/Log.h>
#include <binder/IServiceManager.h>

#include "CameraServiceProxyWrapper.h"

namespace android {

using hardware::ICameraServiceProxy;
using hardware::CameraSessionStats;

Mutex CameraServiceProxyWrapper::sProxyMutex;
sp<hardware::ICameraServiceProxy> CameraServiceProxyWrapper::sCameraServiceProxy;

Mutex CameraServiceProxyWrapper::mLock;
std::map<String8, std::shared_ptr<CameraServiceProxyWrapper::CameraSessionStatsWrapper>>
        CameraServiceProxyWrapper::mSessionStatsMap;

/**
 * CameraSessionStatsWrapper functions
 */

void CameraServiceProxyWrapper::CameraSessionStatsWrapper::onOpen() {
    Mutex::Autolock l(mLock);

    updateProxyDeviceState(mSessionStats);
}

void CameraServiceProxyWrapper::CameraSessionStatsWrapper::onClose(int32_t latencyMs) {
    Mutex::Autolock l(mLock);

    mSessionStats.mNewCameraState = CameraSessionStats::CAMERA_STATE_CLOSED;
    mSessionStats.mLatencyMs = latencyMs;
    updateProxyDeviceState(mSessionStats);
}

void CameraServiceProxyWrapper::CameraSessionStatsWrapper::onStreamConfigured(
        int operatingMode, bool internalReconfig, int32_t latencyMs) {
    Mutex::Autolock l(mLock);

    if (internalReconfig) {
        mSessionStats.mInternalReconfigure++;
    } else {
        mSessionStats.mLatencyMs = latencyMs;
        mSessionStats.mSessionType = operatingMode;
    }
}

void CameraServiceProxyWrapper::CameraSessionStatsWrapper::onActive() {
    Mutex::Autolock l(mLock);

    mSessionStats.mNewCameraState = CameraSessionStats::CAMERA_STATE_ACTIVE;
    updateProxyDeviceState(mSessionStats);

    // Reset mCreationDuration to -1 to distinguish between 1st session
    // after configuration, and all other sessions after configuration.
    mSessionStats.mLatencyMs = -1;
}

void CameraServiceProxyWrapper::CameraSessionStatsWrapper::onIdle(
        int64_t requestCount, int64_t resultErrorCount, bool deviceError,
        const std::vector<hardware::CameraStreamStats>& streamStats) {
    Mutex::Autolock l(mLock);

    mSessionStats.mNewCameraState = CameraSessionStats::CAMERA_STATE_IDLE;
    mSessionStats.mRequestCount = requestCount;
    mSessionStats.mResultErrorCount = resultErrorCount;
    mSessionStats.mDeviceError = deviceError;
    mSessionStats.mStreamStats = streamStats;
    updateProxyDeviceState(mSessionStats);

    mSessionStats.mInternalReconfigure = 0;
    mSessionStats.mStreamStats.clear();
}

/**
 * CameraServiceProxyWrapper functions
 */

sp<ICameraServiceProxy> CameraServiceProxyWrapper::getCameraServiceProxy() {
#ifndef __BRILLO__
    Mutex::Autolock al(sProxyMutex);
    if (sCameraServiceProxy == nullptr) {
        sp<IServiceManager> sm = defaultServiceManager();
        // Use checkService because cameraserver normally starts before the
        // system server and the proxy service. So the long timeout that getService
        // has before giving up is inappropriate.
        sp<IBinder> binder = sm->checkService(String16("media.camera.proxy"));
        if (binder != nullptr) {
            sCameraServiceProxy = interface_cast<ICameraServiceProxy>(binder);
        }
    }
#endif
    return sCameraServiceProxy;
}

void CameraServiceProxyWrapper::pingCameraServiceProxy() {
    sp<ICameraServiceProxy> proxyBinder = getCameraServiceProxy();
    if (proxyBinder == nullptr) return;
    proxyBinder->pingForUserUpdate();
}

int CameraServiceProxyWrapper::getRotateAndCropOverride(String16 packageName, int lensFacing,
        int userId) {
    sp<ICameraServiceProxy> proxyBinder = getCameraServiceProxy();
    if (proxyBinder == nullptr) return true;
    int ret = 0;
    auto status = proxyBinder->getRotateAndCropOverride(packageName, lensFacing, userId, &ret);
    if (!status.isOk()) {
        ALOGE("%s: Failed during top activity orientation query: %s", __FUNCTION__,
                status.exceptionMessage().c_str());
    }

    return ret;
}

void CameraServiceProxyWrapper::updateProxyDeviceState(const CameraSessionStats& sessionStats) {
    sp<ICameraServiceProxy> proxyBinder = getCameraServiceProxy();
    if (proxyBinder == nullptr) return;
    proxyBinder->notifyCameraState(sessionStats);
}

void CameraServiceProxyWrapper::logStreamConfigured(const String8& id,
        int operatingMode, bool internalConfig, int32_t latencyMs) {
    std::shared_ptr<CameraSessionStatsWrapper> sessionStats;
    {
        Mutex::Autolock l(mLock);
        sessionStats = mSessionStatsMap[id];
        if (sessionStats == nullptr) {
            ALOGE("%s: SessionStatsMap should contain camera %s",
                    __FUNCTION__, id.c_str());
            return;
        }
    }

    ALOGV("%s: id %s, operatingMode %d, internalConfig %d, latencyMs %d",
            __FUNCTION__, id.c_str(), operatingMode, internalConfig, latencyMs);
    sessionStats->onStreamConfigured(operatingMode, internalConfig, latencyMs);
}

void CameraServiceProxyWrapper::logActive(const String8& id) {
    std::shared_ptr<CameraSessionStatsWrapper> sessionStats;
    {
        Mutex::Autolock l(mLock);
        sessionStats = mSessionStatsMap[id];
        if (sessionStats == nullptr) {
            ALOGE("%s: SessionStatsMap should contain camera %s when logActive is called",
                    __FUNCTION__, id.c_str());
            return;
        }
    }

    ALOGV("%s: id %s", __FUNCTION__, id.c_str());
    sessionStats->onActive();
}

void CameraServiceProxyWrapper::logIdle(const String8& id,
        int64_t requestCount, int64_t resultErrorCount, bool deviceError,
        const std::vector<hardware::CameraStreamStats>& streamStats) {
    std::shared_ptr<CameraSessionStatsWrapper> sessionStats;
    {
        Mutex::Autolock l(mLock);
        sessionStats = mSessionStatsMap[id];
    }

    if (sessionStats == nullptr) {
        ALOGE("%s: SessionStatsMap should contain camera %s when logIdle is called",
                __FUNCTION__, id.c_str());
        return;
    }

    ALOGV("%s: id %s, requestCount %" PRId64 ", resultErrorCount %" PRId64 ", deviceError %d",
            __FUNCTION__, id.c_str(), requestCount, resultErrorCount, deviceError);
    for (size_t i = 0; i < streamStats.size(); i++) {
        ALOGV("%s: streamStats[%zu]: w %d h %d, requestedCount %" PRId64 ", dropCount %"
                PRId64 ", startTimeMs %d" ,
                __FUNCTION__, i, streamStats[i].mWidth, streamStats[i].mHeight,
                streamStats[i].mRequestCount, streamStats[i].mErrorCount,
                streamStats[i].mStartLatencyMs);
    }

    sessionStats->onIdle(requestCount, resultErrorCount, deviceError, streamStats);
}

void CameraServiceProxyWrapper::logOpen(const String8& id, int facing,
            const String16& clientPackageName, int effectiveApiLevel, bool isNdk,
            int32_t latencyMs) {
    std::shared_ptr<CameraSessionStatsWrapper> sessionStats;
    {
        Mutex::Autolock l(mLock);
        if (mSessionStatsMap.count(id) > 0) {
            ALOGE("%s: SessionStatsMap shouldn't contain camera %s",
                    __FUNCTION__, id.c_str());
            return;
        }

        int apiLevel = CameraSessionStats::CAMERA_API_LEVEL_1;
        if (effectiveApiLevel == 2) {
            apiLevel = CameraSessionStats::CAMERA_API_LEVEL_2;
        }

        sessionStats = std::make_shared<CameraSessionStatsWrapper>(String16(id), facing,
                CameraSessionStats::CAMERA_STATE_OPEN, clientPackageName,
                apiLevel, isNdk, latencyMs);
        mSessionStatsMap.emplace(id, sessionStats);
        ALOGV("%s: Adding id %s", __FUNCTION__, id.c_str());
    }

    ALOGV("%s: id %s, facing %d, effectiveApiLevel %d, isNdk %d, latencyMs %d",
            __FUNCTION__, id.c_str(), facing, effectiveApiLevel, isNdk, latencyMs);
    sessionStats->onOpen();
}

void CameraServiceProxyWrapper::logClose(const String8& id, int32_t latencyMs) {
    std::shared_ptr<CameraSessionStatsWrapper> sessionStats;
    {
        Mutex::Autolock l(mLock);
        if (mSessionStatsMap.count(id) == 0) {
            ALOGE("%s: SessionStatsMap should contain camera %s before it's closed",
                    __FUNCTION__, id.c_str());
            return;
        }

        sessionStats = mSessionStatsMap[id];
        if (sessionStats == nullptr) {
            ALOGE("%s: SessionStatsMap should contain camera %s",
                    __FUNCTION__, id.c_str());
            return;
        }
        mSessionStatsMap.erase(id);
        ALOGV("%s: Erasing id %s", __FUNCTION__, id.c_str());
    }

    ALOGV("%s: id %s, latencyMs %d", __FUNCTION__, id.c_str(), latencyMs);
    sessionStats->onClose(latencyMs);
}

}; // namespace android
