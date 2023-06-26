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

#include <system/camera_metadata_tags.h>

#include "device3/CoordinateMapper.h"

namespace android {

namespace camera3 {

/**
 * Metadata keys to correct when adjusting coordinates for distortion correction
 * or for crop and rotate
 */

// Both capture request and result
constexpr std::array<uint32_t, 3> CoordinateMapper::kMeteringRegionsToCorrect = {
    ANDROID_CONTROL_AF_REGIONS,
    ANDROID_CONTROL_AE_REGIONS,
    ANDROID_CONTROL_AWB_REGIONS
};

// Both capture request and result, not applicable to crop and rotate
constexpr std::array<uint32_t, 1> CoordinateMapper::kRectsToCorrect = {
    ANDROID_SCALER_CROP_REGION,
};

// Only for capture result
constexpr std::array<uint32_t, 2> CoordinateMapper::kResultPointsToCorrectNoClamp = {
    ANDROID_STATISTICS_FACE_RECTANGLES, // Says rectangles, is really points
    ANDROID_STATISTICS_FACE_LANDMARKS,
};

} // namespace camera3

} // namespace android
