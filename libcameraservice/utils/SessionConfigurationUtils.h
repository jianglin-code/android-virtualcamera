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
#ifndef ANDROID_SERVERS_CAMERA_SESSION_CONFIGURATION_UTILS_H
#define ANDROID_SERVERS_CAMERA_SESSION_CONFIGURATION_UTILS_H

#include <android/hardware/camera2/BnCameraDeviceUser.h>
#include <android/hardware/camera2/ICameraDeviceCallbacks.h>
#include <camera/camera2/OutputConfiguration.h>
#include <camera/camera2/SessionConfiguration.h>
#include <camera/camera2/SubmitInfo.h>
#include <android/hardware/camera/device/3.7/types.h>
#include <android/hardware/camera/device/3.4/ICameraDeviceSession.h>
#include <android/hardware/camera/device/3.7/ICameraDeviceSession.h>

#include <device3/Camera3StreamInterface.h>

#include <set>
#include <stdint.h>

// Convenience methods for constructing binder::Status objects for error returns

#define STATUS_ERROR(errorCode, errorString) \
    binder::Status::fromServiceSpecificError(errorCode, \
            String8::format("%s:%d: %s", __FUNCTION__, __LINE__, errorString))

#define STATUS_ERROR_FMT(errorCode, errorString, ...) \
    binder::Status::fromServiceSpecificError(errorCode, \
            String8::format("%s:%d: " errorString, __FUNCTION__, __LINE__, \
                    __VA_ARGS__))

namespace android {
namespace camera3 {

typedef std::function<CameraMetadata (const String8 &, int targetSdkVersion)> metadataGetter;

class StreamConfiguration {
public:
    int32_t format;
    int32_t width;
    int32_t height;
    int32_t isInput;
    static void getStreamConfigurations(
            const CameraMetadata &static_info, bool maxRes,
            std::unordered_map<int, std::vector<StreamConfiguration>> *scm);
    static void getStreamConfigurations(
            const CameraMetadata &static_info, int configuration,
            std::unordered_map<int, std::vector<StreamConfiguration>> *scm);
};

// Holds the default StreamConfigurationMap and Maximum resolution
// StreamConfigurationMap for a camera device.
struct StreamConfigurationPair {
    std::unordered_map<int, std::vector<camera3::StreamConfiguration>>
            mDefaultStreamConfigurationMap;
    std::unordered_map<int, std::vector<camera3::StreamConfiguration>>
            mMaximumResolutionStreamConfigurationMap;
};

class SessionConfigurationUtils {
public:
    static camera3::Size getMaxJpegResolution(const CameraMetadata &metadata,
            bool ultraHighResolution);

    static size_t getUHRMaxJpegBufferSize(camera3::Size uhrMaxJpegSize,
            camera3::Size defaultMaxJpegSize, size_t defaultMaxJpegBufferSize);

    static int64_t euclidDistSquare(int32_t x0, int32_t y0, int32_t x1, int32_t y1);

    // Find the closest dimensions for a given format in available stream configurations with
    // a width <= ROUNDING_WIDTH_CAP
    static bool roundBufferDimensionNearest(int32_t width, int32_t height, int32_t format,
            android_dataspace dataSpace, const CameraMetadata& info, bool maxResolution,
            /*out*/int32_t* outWidth, /*out*/int32_t* outHeight);

    static bool getArrayWidthAndHeight(const CameraMetadata *deviceInfo, int32_t arrayTag,
            int32_t *width, int32_t *height);

    //check if format is not custom format
    static bool isPublicFormat(int32_t format);

    // Create a Surface from an IGraphicBufferProducer. Returns error if
    // IGraphicBufferProducer's property doesn't match with streamInfo
    static binder::Status createSurfaceFromGbp(
        camera3::OutputStreamInfo& streamInfo, bool isStreamInfoValid,
        sp<Surface>& surface, const sp<IGraphicBufferProducer>& gbp,
        const String8 &logicalCameraId, const CameraMetadata &physicalCameraMetadata,
        const std::vector<int32_t> &sensorPixelModesUsed);

    static void mapStreamInfo(const camera3::OutputStreamInfo &streamInfo,
            camera3::camera_stream_rotation_t rotation, String8 physicalId, int32_t groupId,
            hardware::camera::device::V3_7::Stream *stream /*out*/);

    // Check that the physicalCameraId passed in is spported by the camera
    // device.
    static binder::Status checkPhysicalCameraId(
        const std::vector<std::string> &physicalCameraIds, const String8 &physicalCameraId,
        const String8 &logicalCameraId);

    static binder::Status checkSurfaceType(size_t numBufferProducers,
        bool deferredConsumer, int surfaceType);

    static binder::Status checkOperatingMode(int operatingMode,
        const CameraMetadata &staticInfo, const String8 &cameraId);

    // utility function to convert AIDL SessionConfiguration to HIDL
    // streamConfiguration. Also checks for validity of SessionConfiguration and
    // returns a non-ok binder::Status if the passed in session configuration
    // isn't valid.
    static binder::Status
    convertToHALStreamCombination(const SessionConfiguration& sessionConfiguration,
            const String8 &cameraId, const CameraMetadata &deviceInfo,
            metadataGetter getMetadata, const std::vector<std::string> &physicalCameraIds,
            hardware::camera::device::V3_7::StreamConfiguration &streamConfiguration,
            bool overrideForPerfClass, bool *earlyExit);

    // Utility function to convert a V3_7::StreamConfiguration to
    // V3_4::StreamConfiguration. Return false if the original V3_7 configuration cannot
    // be used by older version HAL.
    static bool convertHALStreamCombinationFromV37ToV34(
            hardware::camera::device::V3_4::StreamConfiguration &streamConfigV34,
            const hardware::camera::device::V3_7::StreamConfiguration &streamConfigV37);

    static StreamConfigurationPair getStreamConfigurationPair(const CameraMetadata &metadata);

    static status_t checkAndOverrideSensorPixelModesUsed(
            const std::vector<int32_t> &sensorPixelModesUsed, int format, int width, int height,
            const CameraMetadata &staticInfo, bool flexibleConsumer,
            std::unordered_set<int32_t> *overriddenSensorPixelModesUsed);

    static bool isUltraHighResolutionSensor(const CameraMetadata &deviceInfo);

    static int32_t getAppropriateModeTag(int32_t defaultTag, bool maxResolution = false);

    static bool targetPerfClassPrimaryCamera(
            const std::set<std::string>& perfClassPrimaryCameraIds, const std::string& cameraId,
            int32_t targetSdkVersion);

    static const int32_t MAX_SURFACES_PER_STREAM = 4;

    static const int32_t ROUNDING_WIDTH_CAP = 1920;

    static const int32_t SDK_VERSION_S = 31;
    static int32_t PERF_CLASS_LEVEL;
    static bool IS_PERF_CLASS;
    static const int32_t PERF_CLASS_JPEG_THRESH_W = 1920;
    static const int32_t PERF_CLASS_JPEG_THRESH_H = 1080;
};

} // camera3
} // android
#endif
