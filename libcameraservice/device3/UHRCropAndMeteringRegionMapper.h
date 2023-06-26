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

#ifndef ANDROID_SERVERS_UHRCROP_REGIONS_MAPPER_H
#define ANDROID_SERVERS_UHRCROP_REGIONS_MAPPER_H

#include <utils/Errors.h>
#include <array>

#include "camera/CameraMetadata.h"

namespace android {

namespace camera3 {

/**
 * Utilities to transform SCALER_CROP_REGION and metering regions for ultra high
 * resolution sensors.
 */
class UHRCropAndMeteringRegionMapper {
 public:
    UHRCropAndMeteringRegionMapper() = default;
    UHRCropAndMeteringRegionMapper(const CameraMetadata &deviceInfo, bool usePreCorrectionArray);

    /**
     * Adjust capture request assuming rotate and crop AUTO is enabled
     */
    status_t updateCaptureRequest(CameraMetadata *request);

 private:

    void fixCropRegionsIfNeeded(CameraMetadata *request);
    void fixMeteringRegionsIfNeeded(CameraMetadata *request);

    int32_t mArrayWidth = 0;
    int32_t mArrayHeight = 0;
    int32_t mArrayWidthMaximumResolution = 0;
    int32_t mArrayHeightMaximumResolution = 0;
    bool mIsValid = false;
}; // class UHRCropAndMeteringRegionMapper

} // namespace camera3

} // namespace android

#endif
