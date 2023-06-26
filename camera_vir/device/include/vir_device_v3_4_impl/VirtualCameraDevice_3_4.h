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

#ifndef ANDROID_HARDWARE_CAMERA_DEVICE_V3_4_VIRCAMERADEVICE_H
#define ANDROID_HARDWARE_CAMERA_DEVICE_V3_4_VIRCAMERADEVICE_H

#include "utils/Mutex.h"
#include "CameraMetadata.h"

#include <android/hardware/camera/device/3.2/ICameraDevice.h>
#include <hidl/Status.h>
#include <hidl/MQDescriptor.h>
#include "VirtualCameraDeviceSession_3.4.h"

#include <vector>

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace V3_4 {
namespace virtuals {
namespace implementation {

using namespace ::android::hardware::camera::device;
using ::android::hardware::camera::device::V3_2::ICameraDevice;
using ::android::hardware::camera::device::V3_2::ICameraDeviceCallback;
using ::android::hardware::camera::common::V1_0::CameraResourceCost;
using ::android::hardware::camera::common::V1_0::TorchMode;
using ::android::hardware::camera::common::V1_0::Status;
using ::android::hardware::camera::virtuals::common::VirtualCameraConfig;
using ::android::hardware::camera::virtuals::common::Size;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::hidl_vec;
using ::android::hardware::hidl_string;
using ::android::sp;

/*
 * The camera device HAL implementation is opened lazily (via the open call)
 */
struct VirtualCameraDevice : public virtual RefBase {

    // Called by external camera provider HAL.
    // Provider HAL must ensure the uniqueness of CameraDevice object per cameraId, or there could
    // be multiple CameraDevice trying to access the same physical camera.  Also, provider will have
    // to keep track of all CameraDevice objects in order to notify CameraDevice when the underlying
    // camera is detached.
    VirtualCameraDevice(const std::string& cameraId, const VirtualCameraConfig& cfg);
    virtual ~VirtualCameraDevice();

    // Retrieve the HIDL interface, split into its own class to avoid inheritance issues when
    // dealing with minor version revs and simultaneous implementation and interface inheritance
    virtual sp<ICameraDevice> getInterface() {
        return new TrampolineDeviceInterface_3_4(this);
    }

    // Caller must use this method to check if CameraDevice ctor failed
    bool isInitFailed();
    bool isInitFailedLocked();

    /* Methods from ::android::hardware::camera::device::V3_2::ICameraDevice follow. */
    // The following method can be called without opening the actual camera device
    Return<void> getResourceCost(ICameraDevice::getResourceCost_cb _hidl_cb);

    Return<void> getCameraCharacteristics(
            ICameraDevice::getCameraCharacteristics_cb _hidl_cb);

    Return<Status> setTorchMode(TorchMode);

    // Open the device HAL and also return a default capture session
    Return<void> open(const sp<ICameraDeviceCallback>&, ICameraDevice::open_cb);

    // Forward the dump call to the opened session, or do nothing
    Return<void> dumpState(const ::android::hardware::hidl_handle&);
    /* End of Methods from ::android::hardware::camera::device::V3_2::ICameraDevice */

protected:
    // Overridden by child implementations for returning different versions of
    // VirtualCameraDeviceSession
    virtual sp<VirtualCameraDeviceSession> createSession(
            const sp<ICameraDeviceCallback>&,
            const VirtualCameraConfig& cfg,
            const std::vector<SupportedV4L2Format>& sortedFormats,
            const CroppingType& croppingType,
            const common::V1_0::helper::CameraMetadata& chars,
            const std::string& cameraId,
            unique_fd v4l2Fd);

    // Init supported w/h/format/fps in mSupportedFormats. Caller still owns fd
    void initSupportedFormatsLocked(int fd);

    // Calls into virtual member function. Do not use it in constructor
    status_t initCameraCharacteristics();
    // Init available capabilities keys
    virtual status_t initAvailableCapabilities(
            ::android::hardware::camera::common::V1_0::helper::CameraMetadata*);
    // Init non-device dependent keys
    virtual status_t initDefaultCharsKeys(
            ::android::hardware::camera::common::V1_0::helper::CameraMetadata*);
    // Init camera control chars keys. Caller still owns fd
    status_t initCameraControlsCharsKeys(int fd,
            ::android::hardware::camera::common::V1_0::helper::CameraMetadata*);
    // Init camera output configuration related keys.  Caller still owns fd
    status_t initOutputCharsKeys(int fd,
            ::android::hardware::camera::common::V1_0::helper::CameraMetadata*);

    // Helper function for initOutputCharskeys
    template <size_t SIZE>
    status_t initOutputCharskeysByFormat(
            ::android::hardware::camera::common::V1_0::helper::CameraMetadata*,
            uint32_t fourcc, const std::array<int, SIZE>& formats,
            int scaler_stream_config_tag,
            int stream_configuration, int min_frame_duration, int stall_duration);

    bool calculateMinFps(::android::hardware::camera::common::V1_0::helper::CameraMetadata*);

    static void getFrameRateList(int fd, double fpsUpperBound, SupportedV4L2Format* format);

    static void updateFpsBounds(int fd, CroppingType cropType,
            const std::vector<VirtualCameraConfig::FpsLimitation>& fpsLimits,
            SupportedV4L2Format format,
            std::vector<SupportedV4L2Format>& outFmts);

    // Get candidate supported formats list of input cropping type.
    std::vector<SupportedV4L2Format> getCandidateSupportedFormatsLocked(
            int fd, CroppingType cropType,
            const std::vector<VirtualCameraConfig::FpsLimitation>& fpsLimits,
            const std::vector<VirtualCameraConfig::FpsLimitation>& depthFpsLimits,
            const Size& minStreamSize,
            bool depthEnabled);
    // Trim supported format list by the cropping type. Also sort output formats by width/height
    static void trimSupportedFormats(CroppingType cropType,
            /*inout*/std::vector<SupportedV4L2Format>* pFmts);

    Mutex mLock;
    bool mInitialized = false;
    bool mInitFailed = false;
    std::string mCameraId;
    std::string mDevicePath;
    const VirtualCameraConfig& mCfg;
    std::vector<SupportedV4L2Format> mSupportedFormats;
    CroppingType mCroppingType;

    wp<VirtualCameraDeviceSession> mSession = nullptr;

    std::string mSns_name;
    std::string mDev_name;
    bool mMainDevice = false;
    bool mSubDevice = false;
    ::android::hardware::camera::common::V1_0::helper::CameraMetadata mCameraCharacteristics;

    const std::vector<int32_t> AVAILABLE_CHARACTERISTICS_KEYS_3_4 = {
        ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
        ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
        ANDROID_CONTROL_AE_AVAILABLE_MODES,
        ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
        ANDROID_CONTROL_AE_COMPENSATION_RANGE,
        ANDROID_CONTROL_AE_COMPENSATION_STEP,
        ANDROID_CONTROL_AE_LOCK_AVAILABLE,
        ANDROID_CONTROL_AF_AVAILABLE_MODES,
        ANDROID_CONTROL_AVAILABLE_EFFECTS,
        ANDROID_CONTROL_AVAILABLE_MODES,
        ANDROID_CONTROL_AVAILABLE_SCENE_MODES,
        ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
        ANDROID_CONTROL_AWB_AVAILABLE_MODES,
        ANDROID_CONTROL_AWB_LOCK_AVAILABLE,
        ANDROID_CONTROL_MAX_REGIONS,
        ANDROID_FLASH_INFO_AVAILABLE,
        ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,
        ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
        ANDROID_LENS_FACING,
        ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
        ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION,
        ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
        ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS,
        ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS,
        ANDROID_REQUEST_PARTIAL_RESULT_COUNT,
        ANDROID_REQUEST_PIPELINE_MAX_DEPTH,
        ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
        ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
        ANDROID_SCALER_CROPPING_TYPE,
        ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
        ANDROID_SENSOR_INFO_MAX_FRAME_DURATION,
        ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
        ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE,
        ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE,
        ANDROID_SENSOR_ORIENTATION,
        ANDROID_SHADING_AVAILABLE_MODES,
        ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
        ANDROID_STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES,
        ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,
        ANDROID_STATISTICS_INFO_MAX_FACE_COUNT,
        ANDROID_SYNC_MAX_LATENCY};

private:

    struct TrampolineDeviceInterface_3_4 : public ICameraDevice {
        TrampolineDeviceInterface_3_4(sp<VirtualCameraDevice> parent) :
            mParent(parent) {}

        virtual Return<void> getResourceCost(V3_2::ICameraDevice::getResourceCost_cb _hidl_cb)
                override {
            return mParent->getResourceCost(_hidl_cb);
        }

        virtual Return<void> getCameraCharacteristics(
                V3_2::ICameraDevice::getCameraCharacteristics_cb _hidl_cb) override {
            return mParent->getCameraCharacteristics(_hidl_cb);
        }

        virtual Return<Status> setTorchMode(TorchMode mode) override {
            return Status::OK;
            //return mParent->setTorchMode(mode);
        }

        virtual Return<void> open(const sp<V3_2::ICameraDeviceCallback>& callback,
                V3_2::ICameraDevice::open_cb _hidl_cb) override {
            sp<ICameraDeviceSession> deviceSession = nullptr;
            _hidl_cb(Status::OK, deviceSession);
            return hardware::Void();
            //return mParent->open(callback, _hidl_cb);
        }

        virtual Return<void> dumpState(const hidl_handle& fd) override {
            return hardware::Void();
            //return mParent->dumpState(fd);
        }

    private:
        sp<VirtualCameraDevice> mParent;
    };

};

}  // namespace implementation
}  // namespace virtuals
}  // namespace V3_4
}  // namespace device
}  // namespace camera
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_CAMERA_DEVICE_V3_4_VIRCAMERADEVICE_H
