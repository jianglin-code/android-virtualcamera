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

#ifndef ANDROID_SERVERS_CAMERA_PHOTOGRAPHY_CAMERAOFFLINESESSIONCLIENT_H
#define ANDROID_SERVERS_CAMERA_PHOTOGRAPHY_CAMERAOFFLINESESSIONCLIENT_H

#include <android/hardware/camera2/BnCameraOfflineSession.h>
#include <android/hardware/camera2/ICameraDeviceCallbacks.h>
#include "common/FrameProcessorBase.h"
#include "common/CameraDeviceBase.h"
#include "CameraService.h"
#include "CompositeStream.h"

namespace android {

using android::hardware::camera2::ICameraDeviceCallbacks;
using camera3::CompositeStream;

// Client for offline session. Note that offline session client does not affect camera service's
// client arbitration logic. It is camera HAL's decision to decide whether a normal camera
// client is conflicting with existing offline client(s).
// The other distinctive difference between offline clients and normal clients is that normal
// clients are created through ICameraService binder calls, while the offline session client
// is created through ICameraDeviceUser::switchToOffline call.
class CameraOfflineSessionClient :
        public CameraService::BasicClient,
        public hardware::camera2::BnCameraOfflineSession,
        public camera2::FrameProcessorBase::FilteredListener,
        public NotificationListener
{
public:
    CameraOfflineSessionClient(
            const sp<CameraService>& cameraService,
            sp<CameraOfflineSessionBase> session,
            const KeyedVector<sp<IBinder>, sp<CompositeStream>>& offlineCompositeStreamMap,
            const sp<ICameraDeviceCallbacks>& remoteCallback,
            const String16& clientPackageName,
            const std::optional<String16>& clientFeatureId,
            const String8& cameraIdStr, int cameraFacing, int sensorOrientation,
            int clientPid, uid_t clientUid, int servicePid) :
            CameraService::BasicClient(
                    cameraService,
                    IInterface::asBinder(remoteCallback),
                    clientPackageName, clientFeatureId,
                    cameraIdStr, cameraFacing, sensorOrientation, clientPid, clientUid, servicePid),
            mRemoteCallback(remoteCallback), mOfflineSession(session),
            mCompositeStreamMap(offlineCompositeStreamMap) {}

    virtual ~CameraOfflineSessionClient() {}

    sp<IBinder> asBinderWrapper() override {
        return IInterface::asBinder(this);
    }

    binder::Status disconnect() override;

    status_t dump(int /*fd*/, const Vector<String16>& /*args*/) override;

    status_t dumpClient(int /*fd*/, const Vector<String16>& /*args*/) override;

    status_t initialize(sp<CameraProviderManager> /*manager*/,
            const String8& /*monitorTags*/) override;

    status_t setRotateAndCropOverride(uint8_t rotateAndCrop) override;

    bool supportsCameraMute() override;
    status_t setCameraMute(bool enabled) override;

    // permissions management
    status_t startCameraOps() override;
    status_t finishCameraOps() override;

    // FilteredResultListener API
    void onResultAvailable(const CaptureResult& result) override;

    // NotificationListener API
    void notifyError(int32_t errorCode, const CaptureResultExtras& resultExtras) override;
    void notifyShutter(const CaptureResultExtras& resultExtras, nsecs_t timestamp) override;
    status_t notifyActive() override;
    void notifyIdle(int64_t requestCount, int64_t resultErrorCount, bool deviceError,
            const std::vector<hardware::CameraStreamStats>& streamStats) override;
    void notifyAutoFocus(uint8_t newState, int triggerId) override;
    void notifyAutoExposure(uint8_t newState, int triggerId) override;
    void notifyAutoWhitebalance(uint8_t newState, int triggerId) override;
    void notifyPrepared(int streamId) override;
    void notifyRequestQueueEmpty() override;
    void notifyRepeatingRequestError(long lastFrameNumber) override;

private:
    mutable Mutex mBinderSerializationLock;

    sp<hardware::camera2::ICameraDeviceCallbacks> mRemoteCallback;

    sp<CameraOfflineSessionBase> mOfflineSession;

    sp<camera2::FrameProcessorBase> mFrameProcessor;

    // Offline composite stream map, output surface -> composite stream
    KeyedVector<sp<IBinder>, sp<CompositeStream>> mCompositeStreamMap;
};

} // namespace android

#endif // ANDROID_SERVERS_CAMERA_PHOTOGRAPHY_CAMERAOFFLINESESSIONCLIENT_H
