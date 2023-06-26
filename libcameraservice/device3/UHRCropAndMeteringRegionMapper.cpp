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

#define LOG_TAG "Camera3-UHRCropAndMeteringRegionMapper"
#define ATRACE_TAG ATRACE_TAG_CAMERA
//#define LOG_NDEBUG 0

#include <algorithm>
#include <cmath>

#include "device3/UHRCropAndMeteringRegionMapper.h"
#include "utils/SessionConfigurationUtils.h"

namespace android {

namespace camera3 {
// For Capture request
// metering region -> {fwk private key for metering region set, true}
static std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> kMeteringRegionsToCorrect = {
    {ANDROID_CONTROL_AF_REGIONS,
        {ANDROID_CONTROL_AF_REGIONS_SET, ANDROID_CONTROL_AF_REGIONS_SET_TRUE}},
    {ANDROID_CONTROL_AE_REGIONS,
        {ANDROID_CONTROL_AE_REGIONS_SET, ANDROID_CONTROL_AE_REGIONS_SET_TRUE}},
    {ANDROID_CONTROL_AWB_REGIONS,
        {ANDROID_CONTROL_AWB_REGIONS_SET,  ANDROID_CONTROL_AWB_REGIONS_SET_TRUE}}
};

UHRCropAndMeteringRegionMapper::UHRCropAndMeteringRegionMapper(const CameraMetadata &deviceInfo,
        bool usePreCorrectedArray) {

    if (usePreCorrectedArray) {
        if (!SessionConfigurationUtils::getArrayWidthAndHeight(&deviceInfo,
                ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE, &mArrayWidth,
                &mArrayHeight)) {
            ALOGE("%s: Couldn't get pre correction active array size", __FUNCTION__);
            return;
        }
        if (!SessionConfigurationUtils::getArrayWidthAndHeight(&deviceInfo,
                ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE_MAXIMUM_RESOLUTION,
                &mArrayWidthMaximumResolution, &mArrayHeightMaximumResolution)) {
            ALOGE("%s: Couldn't get maximum resolution pre correction active array size",
                    __FUNCTION__);
            return;
        }
    } else {
        if (!SessionConfigurationUtils::getArrayWidthAndHeight(&deviceInfo,
                ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, &mArrayWidth,
                &mArrayHeight)) {
            ALOGE("%s: Couldn't get active array size", __FUNCTION__);
            return;
        }
        if (!SessionConfigurationUtils::getArrayWidthAndHeight(&deviceInfo,
                ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE_MAXIMUM_RESOLUTION,
                &mArrayWidthMaximumResolution, &mArrayHeightMaximumResolution)) {
            ALOGE("%s: Couldn't get maximum resolution active array size", __FUNCTION__);
            return;
        }

    }

    mIsValid = true;

    ALOGV("%s: array size: %d x %d, full res array size: %d x %d,",
            __FUNCTION__, mArrayWidth, mArrayHeight, mArrayWidthMaximumResolution,
            mArrayHeightMaximumResolution);
}

void UHRCropAndMeteringRegionMapper::fixMeteringRegionsIfNeeded(CameraMetadata *request) {
    if (request == nullptr) {
      ALOGE("%s request is nullptr, can't fix crop region", __FUNCTION__);
      return;
    }
    for (const auto &entry : kMeteringRegionsToCorrect) {
        // Check if the metering region Set key is set to TRUE, we don't
        // need to correct the metering regions.
        camera_metadata_entry meteringRegionsSetEntry =
                request->find(entry.second.first);
        if (meteringRegionsSetEntry.count == 1 &&
                meteringRegionsSetEntry.data.u8[0] == entry.second.second) {
            // metering region set by client, doesn't need to be fixed.
            continue;
        }
        camera_metadata_entry meteringRegionEntry = request->find(entry.first);
        if (meteringRegionEntry.count % 5 != 0) {
            ALOGE("%s: Metering region entry for tag %d does not have a valid number of entries, "
                    "skipping", __FUNCTION__, (int)entry.first);
            continue;
        }
        for (size_t j = 0; j < meteringRegionEntry.count; j += 5) {
            int32_t *meteringRegionStart = meteringRegionEntry.data.i32 + j;
            meteringRegionStart[0] = 0;
            meteringRegionStart[1] = 0;
            meteringRegionStart[2] = mArrayWidthMaximumResolution;
            meteringRegionStart[3] = mArrayHeightMaximumResolution;
        }
    }
}

void UHRCropAndMeteringRegionMapper::fixCropRegionsIfNeeded(CameraMetadata *request) {
    if (request == nullptr) {
      ALOGE("%s request is nullptr, can't fix crop region", __FUNCTION__);
      return;
    }
    // Check if the scalerCropRegionSet key is set to TRUE, we don't
    // need to correct the crop region.
    camera_metadata_entry cropRegionSetEntry =
            request->find(ANDROID_SCALER_CROP_REGION_SET);
    if (cropRegionSetEntry.count == 1 &&
        cropRegionSetEntry.data.u8[0] == ANDROID_SCALER_CROP_REGION_SET_TRUE) {
        // crop regions set by client, doesn't need to be fixed.
        return;
    }
    camera_metadata_entry_t cropRegionEntry = request->find(ANDROID_SCALER_CROP_REGION);
    if (cropRegionEntry.count == 4) {
        cropRegionEntry.data.i32[0] = 0;
        cropRegionEntry.data.i32[1] = 0;
        cropRegionEntry.data.i32[2] = mArrayWidthMaximumResolution;
        cropRegionEntry.data.i32[3] = mArrayHeightMaximumResolution;
    }
}

status_t UHRCropAndMeteringRegionMapper::updateCaptureRequest(CameraMetadata* request) {
    if (request == nullptr) {
        ALOGE("%s Invalid request, request is nullptr", __FUNCTION__);
        return BAD_VALUE;
    }
    if (!mIsValid) {
        ALOGE("%s UHRCropAndMeteringRegionMapper didn't initialize correctly", __FUNCTION__);
        return INVALID_OPERATION;
    }

    camera_metadata_entry sensorPixelModeEntry = request->find(ANDROID_SENSOR_PIXEL_MODE);

    // Check if this is max resolution capture, if not, we don't need to do
    // anything.
    if (sensorPixelModeEntry.count != 0) {
        int32_t sensorPixelMode = sensorPixelModeEntry.data.u8[0];
        if (sensorPixelMode != ANDROID_SENSOR_PIXEL_MODE_MAXIMUM_RESOLUTION) {
            // Correction not needed for default mode requests.
           return OK;
        }
    } else {
        // sensor pixel mode not set -> default sensor pixel mode request, which
        // doesn't need correction.
        return OK;
    }

    fixCropRegionsIfNeeded(request);
    fixMeteringRegionsIfNeeded(request);
    return OK;
}

} // namespace camera3

} // namespace android
