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

#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

#include "device3/DistortionMapper.h"
#include <camera/CameraMetadata.h>

using namespace android;
using namespace android::camera3;
using DistortionMapperInfo = android::camera3::DistortionMapper::DistortionMapperInfo;

int32_t testActiveArray[] = {100, 100, 1000, 750};
float testICal[] = { 1000.f, 1000.f, 500.f, 500.f, 0.f };
float identityDistortion[] = { 0.f, 0.f, 0.f, 0.f, 0.f};

void setupTestMapper(DistortionMapper *m,
        float distortion[5], float intrinsics[5],
        int32_t activeArray[4], int32_t preCorrectionActiveArray[4]) {
    CameraMetadata deviceInfo;

    deviceInfo.update(ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE,
            preCorrectionActiveArray, 4);

    deviceInfo.update(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
            activeArray, 4);

    deviceInfo.update(ANDROID_LENS_INTRINSIC_CALIBRATION,
            intrinsics, 5);

    deviceInfo.update(ANDROID_LENS_DISTORTION,
            distortion, 5);

    m->setupStaticInfo(deviceInfo);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    FuzzedDataProvider fdp(data, size);

    DistortionMapper m;
    setupTestMapper(&m, identityDistortion, testICal,
        /*activeArray*/ testActiveArray,
        /*preCorrectionActiveArray*/ testActiveArray);

    bool clamp = fdp.ConsumeBool();
    bool simple = fdp.ConsumeBool();
    std::vector<int32_t> input;
    for (int index = 0; fdp.remaining_bytes() > 0; index++) {
        input.push_back(fdp.ConsumeIntegral<int32_t>());
    }
    DistortionMapperInfo *mapperInfo = m.getMapperInfo();
    // The size argument counts how many coordinate pairs there are, so
    // it is expected to be 1/2 the size of the input.
    m.mapCorrectedToRaw(input.data(), input.size()/2,  mapperInfo, clamp, simple);

    return 0;
}
