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
#include <cutils/properties.h>

#include "SessionConfigurationUtils.h"
#include "../api2/DepthCompositeStream.h"
#include "../api2/HeicCompositeStream.h"
#include "common/CameraDeviceBase.h"
#include "../CameraService.h"
#include "device3/Camera3Device.h"
#include "device3/Camera3OutputStream.h"

using android::camera3::OutputStreamInfo;
using android::camera3::OutputStreamInfo;
using android::hardware::camera2::ICameraDeviceUser;
using android::hardware::camera::metadata::V3_6::CameraMetadataEnumAndroidSensorPixelMode;

namespace android {
namespace camera3 {

int32_t SessionConfigurationUtils::PERF_CLASS_LEVEL =
        property_get_int32("ro.odm.build.media_performance_class", 0);

bool SessionConfigurationUtils::IS_PERF_CLASS = (PERF_CLASS_LEVEL == SDK_VERSION_S);

camera3::Size SessionConfigurationUtils::getMaxJpegResolution(const CameraMetadata &metadata,
        bool ultraHighResolution) {
    int32_t maxJpegWidth = 0, maxJpegHeight = 0;
    const int STREAM_CONFIGURATION_SIZE = 4;
    const int STREAM_FORMAT_OFFSET = 0;
    const int STREAM_WIDTH_OFFSET = 1;
    const int STREAM_HEIGHT_OFFSET = 2;
    const int STREAM_IS_INPUT_OFFSET = 3;

    int32_t scalerSizesTag = ultraHighResolution ?
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION :
                    ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS;
    camera_metadata_ro_entry_t availableStreamConfigs =
            metadata.find(scalerSizesTag);
    if (availableStreamConfigs.count == 0 ||
            availableStreamConfigs.count % STREAM_CONFIGURATION_SIZE != 0) {
        return camera3::Size(0, 0);
    }

    // Get max jpeg size (area-wise).
    for (size_t i= 0; i < availableStreamConfigs.count; i+= STREAM_CONFIGURATION_SIZE) {
        int32_t format = availableStreamConfigs.data.i32[i + STREAM_FORMAT_OFFSET];
        int32_t width = availableStreamConfigs.data.i32[i + STREAM_WIDTH_OFFSET];
        int32_t height = availableStreamConfigs.data.i32[i + STREAM_HEIGHT_OFFSET];
        int32_t isInput = availableStreamConfigs.data.i32[i + STREAM_IS_INPUT_OFFSET];
        if (isInput == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT
                && format == HAL_PIXEL_FORMAT_BLOB &&
                (width * height > maxJpegWidth * maxJpegHeight)) {
            maxJpegWidth = width;
            maxJpegHeight = height;
        }
    }

    return camera3::Size(maxJpegWidth, maxJpegHeight);
}

size_t SessionConfigurationUtils::getUHRMaxJpegBufferSize(camera3::Size uhrMaxJpegSize,
        camera3::Size defaultMaxJpegSize, size_t defaultMaxJpegBufferSize) {
    return (uhrMaxJpegSize.width * uhrMaxJpegSize.height) /
            (defaultMaxJpegSize.width * defaultMaxJpegSize.height) * defaultMaxJpegBufferSize;
}

void StreamConfiguration::getStreamConfigurations(
        const CameraMetadata &staticInfo, int configuration,
        std::unordered_map<int, std::vector<StreamConfiguration>> *scm) {
    if (scm == nullptr) {
        ALOGE("%s: StreamConfigurationMap nullptr", __FUNCTION__);
        return;
    }
    const int STREAM_FORMAT_OFFSET = 0;
    const int STREAM_WIDTH_OFFSET = 1;
    const int STREAM_HEIGHT_OFFSET = 2;
    const int STREAM_IS_INPUT_OFFSET = 3;

    camera_metadata_ro_entry availableStreamConfigs = staticInfo.find(configuration);
    for (size_t i = 0; i < availableStreamConfigs.count; i += 4) {
        int32_t format = availableStreamConfigs.data.i32[i + STREAM_FORMAT_OFFSET];
        int32_t width = availableStreamConfigs.data.i32[i + STREAM_WIDTH_OFFSET];
        int32_t height = availableStreamConfigs.data.i32[i + STREAM_HEIGHT_OFFSET];
        int32_t isInput = availableStreamConfigs.data.i32[i + STREAM_IS_INPUT_OFFSET];
        StreamConfiguration sc = {format, width, height, isInput};
        (*scm)[format].push_back(sc);
    }
}

void StreamConfiguration::getStreamConfigurations(
        const CameraMetadata &staticInfo, bool maxRes,
        std::unordered_map<int, std::vector<StreamConfiguration>> *scm) {
    int32_t scalerKey =
            SessionConfigurationUtils::getAppropriateModeTag(
                    ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, maxRes);

    int32_t depthKey =
            SessionConfigurationUtils::getAppropriateModeTag(
                    ANDROID_DEPTH_AVAILABLE_DEPTH_STREAM_CONFIGURATIONS, maxRes);

    int32_t dynamicDepthKey =
            SessionConfigurationUtils::getAppropriateModeTag(
                    ANDROID_DEPTH_AVAILABLE_DYNAMIC_DEPTH_STREAM_CONFIGURATIONS);

    int32_t heicKey =
            SessionConfigurationUtils::getAppropriateModeTag(
                    ANDROID_HEIC_AVAILABLE_HEIC_STREAM_CONFIGURATIONS);

    getStreamConfigurations(staticInfo, scalerKey, scm);
    getStreamConfigurations(staticInfo, depthKey, scm);
    getStreamConfigurations(staticInfo, dynamicDepthKey, scm);
    getStreamConfigurations(staticInfo, heicKey, scm);
}

int32_t SessionConfigurationUtils::getAppropriateModeTag(int32_t defaultTag, bool maxResolution) {
    if (!maxResolution) {
        return defaultTag;
    }
    switch (defaultTag) {
        case ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS:
            return ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION;
        case ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS:
            return ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS_MAXIMUM_RESOLUTION;
        case ANDROID_SCALER_AVAILABLE_STALL_DURATIONS:
            return ANDROID_SCALER_AVAILABLE_STALL_DURATIONS_MAXIMUM_RESOLUTION;
        case ANDROID_DEPTH_AVAILABLE_DEPTH_STREAM_CONFIGURATIONS:
            return ANDROID_DEPTH_AVAILABLE_DEPTH_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION;
        case ANDROID_DEPTH_AVAILABLE_DEPTH_MIN_FRAME_DURATIONS:
            return ANDROID_DEPTH_AVAILABLE_DEPTH_MIN_FRAME_DURATIONS_MAXIMUM_RESOLUTION;
        case ANDROID_DEPTH_AVAILABLE_DEPTH_STALL_DURATIONS:
            return ANDROID_DEPTH_AVAILABLE_DEPTH_STALL_DURATIONS_MAXIMUM_RESOLUTION;
        case ANDROID_DEPTH_AVAILABLE_DYNAMIC_DEPTH_STREAM_CONFIGURATIONS:
            return ANDROID_DEPTH_AVAILABLE_DYNAMIC_DEPTH_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION;
        case ANDROID_DEPTH_AVAILABLE_DYNAMIC_DEPTH_MIN_FRAME_DURATIONS:
            return ANDROID_DEPTH_AVAILABLE_DYNAMIC_DEPTH_MIN_FRAME_DURATIONS_MAXIMUM_RESOLUTION;
        case ANDROID_DEPTH_AVAILABLE_DYNAMIC_DEPTH_STALL_DURATIONS:
            return ANDROID_DEPTH_AVAILABLE_DYNAMIC_DEPTH_STALL_DURATIONS_MAXIMUM_RESOLUTION;
        case ANDROID_HEIC_AVAILABLE_HEIC_STREAM_CONFIGURATIONS:
            return ANDROID_HEIC_AVAILABLE_HEIC_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION;
        case ANDROID_HEIC_AVAILABLE_HEIC_MIN_FRAME_DURATIONS:
            return ANDROID_HEIC_AVAILABLE_HEIC_MIN_FRAME_DURATIONS_MAXIMUM_RESOLUTION;
        case ANDROID_HEIC_AVAILABLE_HEIC_STALL_DURATIONS:
            return ANDROID_HEIC_AVAILABLE_HEIC_STALL_DURATIONS_MAXIMUM_RESOLUTION;
        case ANDROID_SENSOR_OPAQUE_RAW_SIZE:
            return ANDROID_SENSOR_OPAQUE_RAW_SIZE_MAXIMUM_RESOLUTION;
        case ANDROID_LENS_INTRINSIC_CALIBRATION:
            return ANDROID_LENS_INTRINSIC_CALIBRATION_MAXIMUM_RESOLUTION;
        case ANDROID_LENS_DISTORTION:
            return ANDROID_LENS_DISTORTION_MAXIMUM_RESOLUTION;
        default:
            ALOGE("%s: Tag %d doesn't have a maximum resolution counterpart", __FUNCTION__,
                    defaultTag);
            return -1;
    }
    return -1;
}

bool SessionConfigurationUtils::getArrayWidthAndHeight(const CameraMetadata *deviceInfo,
        int32_t arrayTag, int32_t *width, int32_t *height) {
    if (width == nullptr || height == nullptr) {
        ALOGE("%s: width / height nullptr", __FUNCTION__);
        return false;
    }
    camera_metadata_ro_entry_t entry;
    entry = deviceInfo->find(arrayTag);
    if (entry.count != 4) return false;
    *width = entry.data.i32[2];
    *height = entry.data.i32[3];
    return true;
}

StreamConfigurationPair
SessionConfigurationUtils::getStreamConfigurationPair(const CameraMetadata &staticInfo) {
    camera3::StreamConfigurationPair streamConfigurationPair;
    camera3::StreamConfiguration::getStreamConfigurations(staticInfo, false,
            &streamConfigurationPair.mDefaultStreamConfigurationMap);
    camera3::StreamConfiguration::getStreamConfigurations(staticInfo, true,
            &streamConfigurationPair.mMaximumResolutionStreamConfigurationMap);
    return streamConfigurationPair;
}

int64_t SessionConfigurationUtils::euclidDistSquare(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    int64_t d0 = x0 - x1;
    int64_t d1 = y0 - y1;
    return d0 * d0 + d1 * d1;
}

bool SessionConfigurationUtils::roundBufferDimensionNearest(int32_t width, int32_t height,
        int32_t format, android_dataspace dataSpace,
        const CameraMetadata& info, bool maxResolution, /*out*/int32_t* outWidth,
        /*out*/int32_t* outHeight) {
    const int32_t depthSizesTag =
            getAppropriateModeTag(ANDROID_DEPTH_AVAILABLE_DEPTH_STREAM_CONFIGURATIONS,
                    maxResolution);
    const int32_t scalerSizesTag =
            getAppropriateModeTag(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, maxResolution);
    const int32_t heicSizesTag =
            getAppropriateModeTag(ANDROID_HEIC_AVAILABLE_HEIC_STREAM_CONFIGURATIONS, maxResolution);

    camera_metadata_ro_entry streamConfigs =
            (dataSpace == HAL_DATASPACE_DEPTH) ? info.find(depthSizesTag) :
            (dataSpace == static_cast<android_dataspace>(HAL_DATASPACE_HEIF)) ?
            info.find(heicSizesTag) :
            info.find(scalerSizesTag);

    int32_t bestWidth = -1;
    int32_t bestHeight = -1;

    // Iterate through listed stream configurations and find the one with the smallest euclidean
    // distance from the given dimensions for the given format.
    for (size_t i = 0; i < streamConfigs.count; i += 4) {
        int32_t fmt = streamConfigs.data.i32[i];
        int32_t w = streamConfigs.data.i32[i + 1];
        int32_t h = streamConfigs.data.i32[i + 2];

        // Ignore input/output type for now
        if (fmt == format) {
            if (w == width && h == height) {
                bestWidth = width;
                bestHeight = height;
                break;
            } else if (w <= ROUNDING_WIDTH_CAP && (bestWidth == -1 ||
                    SessionConfigurationUtils::euclidDistSquare(w, h, width, height) <
                    SessionConfigurationUtils::euclidDistSquare(bestWidth, bestHeight, width,
                            height))) {
                bestWidth = w;
                bestHeight = h;
            }
        }
    }

    if (bestWidth == -1) {
        // Return false if no configurations for this format were listed
        return false;
    }

    // Set the outputs to the closet width/height
    if (outWidth != NULL) {
        *outWidth = bestWidth;
    }
    if (outHeight != NULL) {
        *outHeight = bestHeight;
    }

    // Return true if at least one configuration for this format was listed
    return true;
}

bool SessionConfigurationUtils::isPublicFormat(int32_t format)
{
    switch(format) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_RGB_888:
        case HAL_PIXEL_FORMAT_RGB_565:
        case HAL_PIXEL_FORMAT_BGRA_8888:
        case HAL_PIXEL_FORMAT_YV12:
        case HAL_PIXEL_FORMAT_Y8:
        case HAL_PIXEL_FORMAT_Y16:
        case HAL_PIXEL_FORMAT_RAW16:
        case HAL_PIXEL_FORMAT_RAW10:
        case HAL_PIXEL_FORMAT_RAW12:
        case HAL_PIXEL_FORMAT_RAW_OPAQUE:
        case HAL_PIXEL_FORMAT_BLOB:
        case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
        case HAL_PIXEL_FORMAT_YCbCr_420_888:
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            return true;
        default:
            return false;
    }
}

binder::Status SessionConfigurationUtils::createSurfaceFromGbp(
        OutputStreamInfo& streamInfo, bool isStreamInfoValid,
        sp<Surface>& surface, const sp<IGraphicBufferProducer>& gbp,
        const String8 &logicalCameraId, const CameraMetadata &physicalCameraMetadata,
        const std::vector<int32_t> &sensorPixelModesUsed){
    // bufferProducer must be non-null
    if (gbp == nullptr) {
        String8 msg = String8::format("Camera %s: Surface is NULL", logicalCameraId.string());
        ALOGW("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    }
    // HACK b/10949105
    // Query consumer usage bits to set async operation mode for
    // GLConsumer using controlledByApp parameter.
    bool useAsync = false;
    uint64_t consumerUsage = 0;
    status_t err;
    if ((err = gbp->getConsumerUsage(&consumerUsage)) != OK) {
        String8 msg = String8::format("Camera %s: Failed to query Surface consumer usage: %s (%d)",
                logicalCameraId.string(), strerror(-err), err);
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_INVALID_OPERATION, msg.string());
    }
    if (consumerUsage & GraphicBuffer::USAGE_HW_TEXTURE) {
        ALOGW("%s: Camera %s with consumer usage flag: %" PRIu64 ": Forcing asynchronous mode for"
                "stream", __FUNCTION__, logicalCameraId.string(), consumerUsage);
        useAsync = true;
    }

    uint64_t disallowedFlags = GraphicBuffer::USAGE_HW_VIDEO_ENCODER |
                              GRALLOC_USAGE_RENDERSCRIPT;
    uint64_t allowedFlags = GraphicBuffer::USAGE_SW_READ_MASK |
                           GraphicBuffer::USAGE_HW_TEXTURE |
                           GraphicBuffer::USAGE_HW_COMPOSER;
    bool flexibleConsumer = (consumerUsage & disallowedFlags) == 0 &&
            (consumerUsage & allowedFlags) != 0;

    surface = new Surface(gbp, useAsync);
    ANativeWindow *anw = surface.get();

    int width, height, format;
    android_dataspace dataSpace;
    if ((err = anw->query(anw, NATIVE_WINDOW_WIDTH, &width)) != OK) {
        String8 msg = String8::format("Camera %s: Failed to query Surface width: %s (%d)",
                 logicalCameraId.string(), strerror(-err), err);
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_INVALID_OPERATION, msg.string());
    }
    if ((err = anw->query(anw, NATIVE_WINDOW_HEIGHT, &height)) != OK) {
        String8 msg = String8::format("Camera %s: Failed to query Surface height: %s (%d)",
                logicalCameraId.string(), strerror(-err), err);
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_INVALID_OPERATION, msg.string());
    }
    if ((err = anw->query(anw, NATIVE_WINDOW_FORMAT, &format)) != OK) {
        String8 msg = String8::format("Camera %s: Failed to query Surface format: %s (%d)",
                logicalCameraId.string(), strerror(-err), err);
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_INVALID_OPERATION, msg.string());
    }
    if ((err = anw->query(anw, NATIVE_WINDOW_DEFAULT_DATASPACE,
            reinterpret_cast<int*>(&dataSpace))) != OK) {
        String8 msg = String8::format("Camera %s: Failed to query Surface dataspace: %s (%d)",
                logicalCameraId.string(), strerror(-err), err);
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_INVALID_OPERATION, msg.string());
    }

    // FIXME: remove this override since the default format should be
    //       IMPLEMENTATION_DEFINED. b/9487482 & b/35317944
    if ((format >= HAL_PIXEL_FORMAT_RGBA_8888 && format <= HAL_PIXEL_FORMAT_BGRA_8888) &&
            ((consumerUsage & GRALLOC_USAGE_HW_MASK) &&
             ((consumerUsage & GRALLOC_USAGE_SW_READ_MASK) == 0))) {
        ALOGW("%s: Camera %s: Overriding format %#x to IMPLEMENTATION_DEFINED",
                __FUNCTION__, logicalCameraId.string(), format);
        format = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
    }
    std::unordered_set<int32_t> overriddenSensorPixelModes;
    if (checkAndOverrideSensorPixelModesUsed(sensorPixelModesUsed, format, width, height,
            physicalCameraMetadata, flexibleConsumer, &overriddenSensorPixelModes) != OK) {
        String8 msg = String8::format("Camera %s: sensor pixel modes for stream with "
                "format %#x are not valid",logicalCameraId.string(), format);
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    }
    bool foundInMaxRes = false;
    if (overriddenSensorPixelModes.find(ANDROID_SENSOR_PIXEL_MODE_MAXIMUM_RESOLUTION) !=
            overriddenSensorPixelModes.end()) {
        // we can use the default stream configuration map
        foundInMaxRes = true;
    }
    // Round dimensions to the nearest dimensions available for this format
    if (flexibleConsumer && isPublicFormat(format) &&
            !SessionConfigurationUtils::roundBufferDimensionNearest(width, height,
            format, dataSpace, physicalCameraMetadata, foundInMaxRes, /*out*/&width,
            /*out*/&height)) {
        String8 msg = String8::format("Camera %s: No supported stream configurations with "
                "format %#x defined, failed to create output stream",
                logicalCameraId.string(), format);
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    }

    if (!isStreamInfoValid) {
        streamInfo.width = width;
        streamInfo.height = height;
        streamInfo.format = format;
        streamInfo.dataSpace = dataSpace;
        streamInfo.consumerUsage = consumerUsage;
        streamInfo.sensorPixelModesUsed = overriddenSensorPixelModes;
        return binder::Status::ok();
    }
    if (width != streamInfo.width) {
        String8 msg = String8::format("Camera %s:Surface width doesn't match: %d vs %d",
                logicalCameraId.string(), width, streamInfo.width);
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    }
    if (height != streamInfo.height) {
        String8 msg = String8::format("Camera %s:Surface height doesn't match: %d vs %d",
                 logicalCameraId.string(), height, streamInfo.height);
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    }
    if (format != streamInfo.format) {
        String8 msg = String8::format("Camera %s:Surface format doesn't match: %d vs %d",
                 logicalCameraId.string(), format, streamInfo.format);
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    }
    if (format != HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
        if (dataSpace != streamInfo.dataSpace) {
            String8 msg = String8::format("Camera %s:Surface dataSpace doesn't match: %d vs %d",
                    logicalCameraId.string(), dataSpace, streamInfo.dataSpace);
            ALOGE("%s: %s", __FUNCTION__, msg.string());
            return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
        }
        //At the native side, there isn't a way to check whether 2 surfaces come from the same
        //surface class type. Use usage flag to approximate the comparison.
        if (consumerUsage != streamInfo.consumerUsage) {
            String8 msg = String8::format(
                    "Camera %s:Surface usage flag doesn't match %" PRIu64 " vs %" PRIu64 "",
                    logicalCameraId.string(), consumerUsage, streamInfo.consumerUsage);
            ALOGE("%s: %s", __FUNCTION__, msg.string());
            return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
        }
    }
    return binder::Status::ok();
}

void SessionConfigurationUtils::mapStreamInfo(const OutputStreamInfo &streamInfo,
            camera3::camera_stream_rotation_t rotation, String8 physicalId,
            int32_t groupId, hardware::camera::device::V3_7::Stream *stream /*out*/) {
    if (stream == nullptr) {
        return;
    }

    stream->v3_4.v3_2.streamType = hardware::camera::device::V3_2::StreamType::OUTPUT;
    stream->v3_4.v3_2.width = streamInfo.width;
    stream->v3_4.v3_2.height = streamInfo.height;
    stream->v3_4.v3_2.format = Camera3Device::mapToPixelFormat(streamInfo.format);
    auto u = streamInfo.consumerUsage;
    camera3::Camera3OutputStream::applyZSLUsageQuirk(streamInfo.format, &u);
    stream->v3_4.v3_2.usage = Camera3Device::mapToConsumerUsage(u);
    stream->v3_4.v3_2.dataSpace = Camera3Device::mapToHidlDataspace(streamInfo.dataSpace);
    stream->v3_4.v3_2.rotation = Camera3Device::mapToStreamRotation(rotation);
    stream->v3_4.v3_2.id = -1; // Invalid stream id
    stream->v3_4.physicalCameraId = std::string(physicalId.string());
    stream->v3_4.bufferSize = 0;
    stream->groupId = groupId;
    stream->sensorPixelModesUsed.resize(streamInfo.sensorPixelModesUsed.size());
    size_t idx = 0;
    for (auto mode : streamInfo.sensorPixelModesUsed) {
        stream->sensorPixelModesUsed[idx++] =
                static_cast<CameraMetadataEnumAndroidSensorPixelMode>(mode);
    }
}

binder::Status SessionConfigurationUtils::checkPhysicalCameraId(
        const std::vector<std::string> &physicalCameraIds, const String8 &physicalCameraId,
        const String8 &logicalCameraId) {
    if (physicalCameraId.size() == 0) {
        return binder::Status::ok();
    }
    if (std::find(physicalCameraIds.begin(), physicalCameraIds.end(),
        physicalCameraId.string()) == physicalCameraIds.end()) {
        String8 msg = String8::format("Camera %s: Camera doesn't support physicalCameraId %s.",
                logicalCameraId.string(), physicalCameraId.string());
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    }
    return binder::Status::ok();
}

binder::Status SessionConfigurationUtils::checkSurfaceType(size_t numBufferProducers,
        bool deferredConsumer, int surfaceType)  {
    if (numBufferProducers > MAX_SURFACES_PER_STREAM) {
        ALOGE("%s: GraphicBufferProducer count %zu for stream exceeds limit of %d",
                __FUNCTION__, numBufferProducers, MAX_SURFACES_PER_STREAM);
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, "Surface count is too high");
    } else if ((numBufferProducers == 0) && (!deferredConsumer)) {
        ALOGE("%s: Number of consumers cannot be smaller than 1", __FUNCTION__);
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, "No valid consumers.");
    }

    bool validSurfaceType = ((surfaceType == OutputConfiguration::SURFACE_TYPE_SURFACE_VIEW) ||
            (surfaceType == OutputConfiguration::SURFACE_TYPE_SURFACE_TEXTURE));

    if (deferredConsumer && !validSurfaceType) {
        ALOGE("%s: Target surface has invalid surfaceType = %d.", __FUNCTION__, surfaceType);
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, "Target Surface is invalid");
    }

    return binder::Status::ok();
}

binder::Status SessionConfigurationUtils::checkOperatingMode(int operatingMode,
        const CameraMetadata &staticInfo, const String8 &cameraId) {
    if (operatingMode < 0) {
        String8 msg = String8::format(
            "Camera %s: Invalid operating mode %d requested", cameraId.string(), operatingMode);
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                msg.string());
    }

    bool isConstrainedHighSpeed = (operatingMode == ICameraDeviceUser::CONSTRAINED_HIGH_SPEED_MODE);
    if (isConstrainedHighSpeed) {
        camera_metadata_ro_entry_t entry = staticInfo.find(ANDROID_REQUEST_AVAILABLE_CAPABILITIES);
        bool isConstrainedHighSpeedSupported = false;
        for(size_t i = 0; i < entry.count; ++i) {
            uint8_t capability = entry.data.u8[i];
            if (capability == ANDROID_REQUEST_AVAILABLE_CAPABILITIES_CONSTRAINED_HIGH_SPEED_VIDEO) {
                isConstrainedHighSpeedSupported = true;
                break;
            }
        }
        if (!isConstrainedHighSpeedSupported) {
            String8 msg = String8::format(
                "Camera %s: Try to create a constrained high speed configuration on a device"
                " that doesn't support it.", cameraId.string());
            ALOGE("%s: %s", __FUNCTION__, msg.string());
            return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                    msg.string());
        }
    }

    return binder::Status::ok();
}

binder::Status
SessionConfigurationUtils::convertToHALStreamCombination(
        const SessionConfiguration& sessionConfiguration,
        const String8 &logicalCameraId, const CameraMetadata &deviceInfo,
        metadataGetter getMetadata, const std::vector<std::string> &physicalCameraIds,
        hardware::camera::device::V3_7::StreamConfiguration &streamConfiguration,
        bool overrideForPerfClass, bool *earlyExit) {

    auto operatingMode = sessionConfiguration.getOperatingMode();
    binder::Status res = checkOperatingMode(operatingMode, deviceInfo, logicalCameraId);
    if (!res.isOk()) {
        return res;
    }

    if (earlyExit == nullptr) {
        String8 msg("earlyExit nullptr");
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
    }
    *earlyExit = false;
    auto ret = Camera3Device::mapToStreamConfigurationMode(
            static_cast<camera_stream_configuration_mode_t> (operatingMode),
            /*out*/ &streamConfiguration.operationMode);
    if (ret != OK) {
        String8 msg = String8::format(
            "Camera %s: Failed mapping operating mode %d requested: %s (%d)",
            logicalCameraId.string(), operatingMode, strerror(-ret), ret);
        ALOGE("%s: %s", __FUNCTION__, msg.string());
        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                msg.string());
    }

    bool isInputValid = (sessionConfiguration.getInputWidth() > 0) &&
            (sessionConfiguration.getInputHeight() > 0) &&
            (sessionConfiguration.getInputFormat() > 0);
    auto outputConfigs = sessionConfiguration.getOutputConfigurations();
    size_t streamCount = outputConfigs.size();
    streamCount = isInputValid ? streamCount + 1 : streamCount;
    streamConfiguration.streams.resize(streamCount);
    size_t streamIdx = 0;
    if (isInputValid) {
        hardware::hidl_vec<CameraMetadataEnumAndroidSensorPixelMode> defaultSensorPixelModes;
        defaultSensorPixelModes.resize(1);
        defaultSensorPixelModes[0] =
                static_cast<CameraMetadataEnumAndroidSensorPixelMode>(
                        ANDROID_SENSOR_PIXEL_MODE_DEFAULT);
        streamConfiguration.streams[streamIdx++] = {{{/*streamId*/0,
                hardware::camera::device::V3_2::StreamType::INPUT,
                static_cast<uint32_t> (sessionConfiguration.getInputWidth()),
                static_cast<uint32_t> (sessionConfiguration.getInputHeight()),
                Camera3Device::mapToPixelFormat(sessionConfiguration.getInputFormat()),
                /*usage*/ 0, HAL_DATASPACE_UNKNOWN,
                hardware::camera::device::V3_2::StreamRotation::ROTATION_0},
                /*physicalId*/ nullptr, /*bufferSize*/0}, /*groupId*/-1, defaultSensorPixelModes};
        streamConfiguration.multiResolutionInputImage =
                sessionConfiguration.inputIsMultiResolution();
    }

    for (const auto &it : outputConfigs) {
        const std::vector<sp<IGraphicBufferProducer>>& bufferProducers =
            it.getGraphicBufferProducers();
        bool deferredConsumer = it.isDeferred();
        String8 physicalCameraId = String8(it.getPhysicalCameraId());

        std::vector<int32_t> sensorPixelModesUsed = it.getSensorPixelModesUsed();
        const CameraMetadata &physicalDeviceInfo = getMetadata(physicalCameraId,
                overrideForPerfClass);
        const CameraMetadata &metadataChosen =
                physicalCameraId.size() > 0 ? physicalDeviceInfo : deviceInfo;

        size_t numBufferProducers = bufferProducers.size();
        bool isStreamInfoValid = false;
        int32_t groupId = it.isMultiResolution() ? it.getSurfaceSetID() : -1;
        OutputStreamInfo streamInfo;

        res = checkSurfaceType(numBufferProducers, deferredConsumer, it.getSurfaceType());
        if (!res.isOk()) {
            return res;
        }
        res = checkPhysicalCameraId(physicalCameraIds, physicalCameraId,
                logicalCameraId);
        if (!res.isOk()) {
            return res;
        }

        if (deferredConsumer) {
            streamInfo.width = it.getWidth();
            streamInfo.height = it.getHeight();
            streamInfo.format = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
            streamInfo.dataSpace = android_dataspace_t::HAL_DATASPACE_UNKNOWN;
            auto surfaceType = it.getSurfaceType();
            streamInfo.consumerUsage = GraphicBuffer::USAGE_HW_TEXTURE;
            if (surfaceType == OutputConfiguration::SURFACE_TYPE_SURFACE_VIEW) {
                streamInfo.consumerUsage |= GraphicBuffer::USAGE_HW_COMPOSER;
            }
            if (checkAndOverrideSensorPixelModesUsed(sensorPixelModesUsed,
                    streamInfo.format, streamInfo.width,
                    streamInfo.height, metadataChosen, false /*flexibleConsumer*/,
                    &streamInfo.sensorPixelModesUsed) != OK) {
                        ALOGE("%s: Deferred surface sensor pixel modes not valid",
                                __FUNCTION__);
                        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT,
                                "Deferred surface sensor pixel modes not valid");
            }
            mapStreamInfo(streamInfo, camera3::CAMERA_STREAM_ROTATION_0, physicalCameraId, groupId,
                    &streamConfiguration.streams[streamIdx++]);
            isStreamInfoValid = true;

            if (numBufferProducers == 0) {
                continue;
            }
        }

        for (auto& bufferProducer : bufferProducers) {
            sp<Surface> surface;
            res = createSurfaceFromGbp(streamInfo, isStreamInfoValid, surface, bufferProducer,
                    logicalCameraId, metadataChosen, sensorPixelModesUsed);

            if (!res.isOk())
                return res;

            if (!isStreamInfoValid) {
                bool isDepthCompositeStream =
                        camera3::DepthCompositeStream::isDepthCompositeStream(surface);
                bool isHeicCompositeStream =
                        camera3::HeicCompositeStream::isHeicCompositeStream(surface);
                if (isDepthCompositeStream || isHeicCompositeStream) {
                    // We need to take in to account that composite streams can have
                    // additional internal camera streams.
                    std::vector<OutputStreamInfo> compositeStreams;
                    if (isDepthCompositeStream) {
                      // TODO: Take care of composite streams.
                        ret = camera3::DepthCompositeStream::getCompositeStreamInfo(streamInfo,
                                deviceInfo, &compositeStreams);
                    } else {
                        ret = camera3::HeicCompositeStream::getCompositeStreamInfo(streamInfo,
                            deviceInfo, &compositeStreams);
                    }
                    if (ret != OK) {
                        String8 msg = String8::format(
                                "Camera %s: Failed adding composite streams: %s (%d)",
                                logicalCameraId.string(), strerror(-ret), ret);
                        ALOGE("%s: %s", __FUNCTION__, msg.string());
                        return STATUS_ERROR(CameraService::ERROR_ILLEGAL_ARGUMENT, msg.string());
                    }

                    if (compositeStreams.size() == 0) {
                        // No internal streams means composite stream not
                        // supported.
                        *earlyExit = true;
                        return binder::Status::ok();
                    } else if (compositeStreams.size() > 1) {
                        streamCount += compositeStreams.size() - 1;
                        streamConfiguration.streams.resize(streamCount);
                    }

                    for (const auto& compositeStream : compositeStreams) {
                        mapStreamInfo(compositeStream,
                                static_cast<camera_stream_rotation_t> (it.getRotation()),
                                physicalCameraId, groupId,
                                &streamConfiguration.streams[streamIdx++]);
                    }
                } else {
                    mapStreamInfo(streamInfo,
                            static_cast<camera_stream_rotation_t> (it.getRotation()),
                            physicalCameraId, groupId, &streamConfiguration.streams[streamIdx++]);
                }
                isStreamInfoValid = true;
            }
        }
    }
    return binder::Status::ok();
}

static bool inStreamConfigurationMap(int format, int width, int height,
        const std::unordered_map<int, std::vector<camera3::StreamConfiguration>> &sm) {
    auto scs = sm.find(format);
    if (scs == sm.end()) {
        return false;
    }
    for (auto &sc : scs->second) {
        if (sc.width == width && sc.height == height && sc.isInput == 0) {
            return true;
        }
    }
    return false;
}

static std::unordered_set<int32_t> convertToSet(const std::vector<int32_t> &sensorPixelModesUsed) {
    return std::unordered_set<int32_t>(sensorPixelModesUsed.begin(), sensorPixelModesUsed.end());
}

status_t SessionConfigurationUtils::checkAndOverrideSensorPixelModesUsed(
        const std::vector<int32_t> &sensorPixelModesUsed, int format, int width, int height,
        const CameraMetadata &staticInfo, bool flexibleConsumer,
        std::unordered_set<int32_t> *overriddenSensorPixelModesUsed) {

    const std::unordered_set<int32_t> &sensorPixelModesUsedSet =
            convertToSet(sensorPixelModesUsed);
    if (!isUltraHighResolutionSensor(staticInfo)) {
        if (sensorPixelModesUsedSet.find(ANDROID_SENSOR_PIXEL_MODE_MAXIMUM_RESOLUTION) !=
                sensorPixelModesUsedSet.end()) {
            // invalid value for non ultra high res sensors
            return BAD_VALUE;
        }
        overriddenSensorPixelModesUsed->clear();
        overriddenSensorPixelModesUsed->insert(ANDROID_SENSOR_PIXEL_MODE_DEFAULT);
        return OK;
    }

    StreamConfigurationPair streamConfigurationPair = getStreamConfigurationPair(staticInfo);

    bool isInDefaultStreamConfigurationMap =
            inStreamConfigurationMap(format, width, height,
                    streamConfigurationPair.mDefaultStreamConfigurationMap);

    bool isInMaximumResolutionStreamConfigurationMap =
            inStreamConfigurationMap(format, width, height,
                    streamConfigurationPair.mMaximumResolutionStreamConfigurationMap);

    // Case 1: The client has not changed the sensor mode defaults. In this case, we check if the
    // size + format of the OutputConfiguration is found exclusively in 1.
    // If yes, add that sensorPixelMode to overriddenSensorPixelModes.
    // If no, add 'DEFAULT' to sensorPixelMode. This maintains backwards
    // compatibility.
    if (sensorPixelModesUsedSet.size() == 0) {
        // Ambiguous case, default to only 'DEFAULT' mode.
        if (isInDefaultStreamConfigurationMap && isInMaximumResolutionStreamConfigurationMap) {
            overriddenSensorPixelModesUsed->insert(ANDROID_SENSOR_PIXEL_MODE_DEFAULT);
            return OK;
        }
        // We don't allow flexible consumer for max resolution mode.
        if (isInMaximumResolutionStreamConfigurationMap) {
            overriddenSensorPixelModesUsed->insert(ANDROID_SENSOR_PIXEL_MODE_MAXIMUM_RESOLUTION);
            return OK;
        }
        if (isInDefaultStreamConfigurationMap || (flexibleConsumer && width < ROUNDING_WIDTH_CAP)) {
            overriddenSensorPixelModesUsed->insert(ANDROID_SENSOR_PIXEL_MODE_DEFAULT);
            return OK;
        }
        return BAD_VALUE;
    }

    // Case2: The app has set sensorPixelModesUsed, we need to verify that they
    // are valid / err out.
    if (sensorPixelModesUsedSet.find(ANDROID_SENSOR_PIXEL_MODE_DEFAULT) !=
            sensorPixelModesUsedSet.end() && !isInDefaultStreamConfigurationMap) {
        return BAD_VALUE;
    }

   if (sensorPixelModesUsedSet.find(ANDROID_SENSOR_PIXEL_MODE_MAXIMUM_RESOLUTION) !=
            sensorPixelModesUsedSet.end() && !isInMaximumResolutionStreamConfigurationMap) {
        return BAD_VALUE;
    }
    *overriddenSensorPixelModesUsed = sensorPixelModesUsedSet;
    return OK;
}

bool SessionConfigurationUtils::isUltraHighResolutionSensor(const CameraMetadata &deviceInfo) {
    camera_metadata_ro_entry_t entryCap;
    entryCap = deviceInfo.find(ANDROID_REQUEST_AVAILABLE_CAPABILITIES);
    // Go through the capabilities and check if it has
    // ANDROID_REQUEST_AVAILABLE_CAPABILITIES_ULTRA_HIGH_RESOLUTION_SENSOR
    for (size_t i = 0; i < entryCap.count; ++i) {
        uint8_t capability = entryCap.data.u8[i];
        if (capability == ANDROID_REQUEST_AVAILABLE_CAPABILITIES_ULTRA_HIGH_RESOLUTION_SENSOR) {
            return true;
        }
    }
    return false;
}

bool SessionConfigurationUtils::convertHALStreamCombinationFromV37ToV34(
        hardware::camera::device::V3_4::StreamConfiguration &streamConfigV34,
        const hardware::camera::device::V3_7::StreamConfiguration &streamConfigV37) {
    if (streamConfigV37.multiResolutionInputImage) {
        // ICameraDevice older than 3.7 doesn't support multi-resolution input image.
        return false;
    }

    streamConfigV34.streams.resize(streamConfigV37.streams.size());
    for (size_t i = 0; i < streamConfigV37.streams.size(); i++) {
        if (streamConfigV37.streams[i].groupId != -1) {
            // ICameraDevice older than 3.7 doesn't support multi-resolution output
            // image
            return false;
        }
        streamConfigV34.streams[i] = streamConfigV37.streams[i].v3_4;
    }
    streamConfigV34.operationMode = streamConfigV37.operationMode;
    streamConfigV34.sessionParams = streamConfigV37.sessionParams;

    return true;
}

bool SessionConfigurationUtils::targetPerfClassPrimaryCamera(
        const std::set<std::string>& perfClassPrimaryCameraIds, const std::string& cameraId,
        int targetSdkVersion) {
    bool isPerfClassPrimaryCamera =
            perfClassPrimaryCameraIds.find(cameraId) != perfClassPrimaryCameraIds.end();
    return targetSdkVersion >= SDK_VERSION_S && isPerfClassPrimaryCamera;
}

} // namespace camera3
} // namespace android
