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

#define LOG_NDEBUG 0
#define LOG_TAG "RotateAndCropMapperTest"

#include <functional>
#include <random>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "../device3/RotateAndCropMapper.h"

namespace rotateAndCropMapperTest {

using namespace android;
using namespace android::camera3;

using ::testing::ElementsAreArray;
using ::testing::Each;
using ::testing::AllOf;
using ::testing::Ge;
using ::testing::Le;

#define EXPECT_EQUAL_WITHIN_N(vec, array, N, msg)   \
{ \
    for (size_t i = 0; i < vec.size(); i++) { \
        EXPECT_THAT(vec[i] - array[i], AllOf(Ge(-N), Le(N))) << msg " failed at index:" << i; \
    } \
}

int32_t testActiveArray[] = {100, 100, 4000, 3000};

std::vector<uint8_t> basicModes = {
    ANDROID_SCALER_ROTATE_AND_CROP_NONE,
    ANDROID_SCALER_ROTATE_AND_CROP_90,
    ANDROID_SCALER_ROTATE_AND_CROP_AUTO
};

CameraMetadata setupDeviceInfo(int32_t activeArray[4], std::vector<uint8_t> availableCropModes ) {
    CameraMetadata deviceInfo;

    deviceInfo.update(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
            activeArray, 4);

    deviceInfo.update(ANDROID_SCALER_AVAILABLE_ROTATE_AND_CROP_MODES,
            availableCropModes.data(), availableCropModes.size());

    return deviceInfo;
}

TEST(RotationMapperTest, Initialization) {
    CameraMetadata deviceInfo = setupDeviceInfo(testActiveArray,
            {ANDROID_SCALER_ROTATE_AND_CROP_NONE});

    ASSERT_FALSE(RotateAndCropMapper::isNeeded(&deviceInfo));

    deviceInfo.update(ANDROID_SCALER_AVAILABLE_ROTATE_AND_CROP_MODES,
            basicModes.data(), 3);

    ASSERT_TRUE(RotateAndCropMapper::isNeeded(&deviceInfo));
}

TEST(RotationMapperTest, IdentityTransform) {
    status_t res;

    CameraMetadata deviceInfo = setupDeviceInfo(testActiveArray,
            basicModes);

    RotateAndCropMapper mapper(&deviceInfo);

    CameraMetadata request;
    uint8_t mode = ANDROID_SCALER_ROTATE_AND_CROP_NONE;
    auto full_crop = std::vector<int32_t>{0,0, testActiveArray[2], testActiveArray[3]};
    auto full_region = std::vector<int32_t>{0,0, testActiveArray[2], testActiveArray[3], 1};
    request.update(ANDROID_SCALER_ROTATE_AND_CROP,
            &mode, 1);
    request.update(ANDROID_SCALER_CROP_REGION,
            full_crop.data(), full_crop.size());
    request.update(ANDROID_CONTROL_AE_REGIONS,
            full_region.data(), full_region.size());

    // Map to HAL

    res = mapper.updateCaptureRequest(&request);
    ASSERT_TRUE(res == OK);

    auto e = request.find(ANDROID_CONTROL_AE_REGIONS);
    EXPECT_THAT(full_region, ElementsAreArray(e.data.i32, e.count));

    e = request.find(ANDROID_SCALER_CROP_REGION);
    EXPECT_THAT(full_crop, ElementsAreArray(e.data.i32, e.count));

    // Add fields in HAL

    CameraMetadata result(request);

    auto face = std::vector<int32_t> {300,300,500,500};
    result.update(ANDROID_STATISTICS_FACE_RECTANGLES,
            face.data(), face.size());

    // Map to app

    res = mapper.updateCaptureResult(&result);
    ASSERT_TRUE(res == OK);

    e = result.find(ANDROID_CONTROL_AE_REGIONS);
    EXPECT_THAT(full_region, ElementsAreArray(e.data.i32, e.count));

    e = result.find(ANDROID_SCALER_CROP_REGION);
    EXPECT_THAT(full_crop, ElementsAreArray(e.data.i32, e.count));

    e = result.find(ANDROID_STATISTICS_FACE_RECTANGLES);
    EXPECT_THAT(face, ElementsAreArray(e.data.i32, e.count));
}

TEST(RotationMapperTest, Transform90) {
    status_t res;

    CameraMetadata deviceInfo = setupDeviceInfo(testActiveArray,
            basicModes);

    RotateAndCropMapper mapper(&deviceInfo);

    CameraMetadata request;
    uint8_t mode = ANDROID_SCALER_ROTATE_AND_CROP_90;
    auto full_crop = std::vector<int32_t> {0,0, testActiveArray[2], testActiveArray[3]};
    auto full_region = std::vector<int32_t> {0,0, testActiveArray[2], testActiveArray[3], 1};
    request.update(ANDROID_SCALER_ROTATE_AND_CROP,
            &mode, 1);
    request.update(ANDROID_SCALER_CROP_REGION,
            full_crop.data(), full_crop.size());
    request.update(ANDROID_CONTROL_AE_REGIONS,
            full_region.data(), full_region.size());

    // Map to HAL

    res = mapper.updateCaptureRequest(&request);
    ASSERT_TRUE(res == OK);

    auto e = request.find(ANDROID_CONTROL_AE_REGIONS);
    float aspectRatio = static_cast<float>(full_crop[2]) / full_crop[3];
    int32_t rw = full_crop[3] / aspectRatio;
    int32_t rh = full_crop[3];
    auto rotated_region = std::vector<int32_t> {
        full_crop[0] + (full_crop[2] - rw) / 2, full_crop[1],
        full_crop[0] + (full_crop[2] + rw) / 2, full_crop[1] + full_crop[3],
        1
    };
    EXPECT_THAT(rotated_region, ElementsAreArray(e.data.i32, e.count))
            << "Rotated AE region isn't right";

    e = request.find(ANDROID_SCALER_CROP_REGION);
    EXPECT_THAT(full_crop, ElementsAreArray(e.data.i32, e.count))
            << "Rotated crop region isn't right";

    // Add fields in HAL

    CameraMetadata result(request);

    auto face = std::vector<int32_t> {
        rotated_region[0] + rw / 4, rotated_region[1] + rh / 4,
        rotated_region[2] - rw / 4, rotated_region[3] - rh / 4};
    result.update(ANDROID_STATISTICS_FACE_RECTANGLES,
            face.data(), face.size());

    auto landmarks = std::vector<int32_t> {
        rotated_region[0], rotated_region[1],
        rotated_region[2], rotated_region[3],
        rotated_region[0] + rw / 4, rotated_region[1] + rh / 4,
        rotated_region[0] + rw / 2, rotated_region[1] + rh / 2,
        rotated_region[2] - rw / 4, rotated_region[3] - rh / 4
    };
    result.update(ANDROID_STATISTICS_FACE_LANDMARKS,
            landmarks.data(), landmarks.size());

    // Map to app

    res = mapper.updateCaptureResult(&result);
    ASSERT_TRUE(res == OK);

    // Round-trip results can't be exact since we've gone from a large int range -> small int range
    // and back, leading to quantization. For 4/3 aspect ratio, no more than +-1 error expected
    e = result.find(ANDROID_CONTROL_AE_REGIONS);
    EXPECT_EQUAL_WITHIN_N(full_region, e.data.i32, 1, "Round-tripped AE region isn't right");

    e = result.find(ANDROID_SCALER_CROP_REGION);
    EXPECT_EQUAL_WITHIN_N(full_crop, e.data.i32, 1, "Round-tripped crop region isn't right");

    auto full_face = std::vector<int32_t> {
        full_crop[0] + full_crop[2]/4, full_crop[1] + full_crop[3]/4,
        full_crop[0] + 3*full_crop[2]/4, full_crop[1] + 3*full_crop[3]/4
    };
    e = result.find(ANDROID_STATISTICS_FACE_RECTANGLES);
    EXPECT_EQUAL_WITHIN_N(full_face, e.data.i32, 1, "App-side face rectangle isn't right");

    auto full_landmarks = std::vector<int32_t> {
        full_crop[0], full_crop[1] + full_crop[3],
        full_crop[0] + full_crop[2], full_crop[1],
        full_crop[0] + full_crop[2]/4, full_crop[1] + 3*full_crop[3]/4,
        full_crop[0] + full_crop[2]/2, full_crop[1] + full_crop[3]/2,
        full_crop[0] + 3*full_crop[2]/4, full_crop[1] + full_crop[3]/4
    };
    e = result.find(ANDROID_STATISTICS_FACE_LANDMARKS);
    EXPECT_EQUAL_WITHIN_N(full_landmarks, e.data.i32, 1, "App-side face landmarks aren't right");
}

TEST(RotationMapperTest, Transform270) {
    status_t res;

    CameraMetadata deviceInfo = setupDeviceInfo(testActiveArray,
            basicModes);

    RotateAndCropMapper mapper(&deviceInfo);

    CameraMetadata request;
    uint8_t mode = ANDROID_SCALER_ROTATE_AND_CROP_270;
    auto full_crop = std::vector<int32_t> {0,0, testActiveArray[2], testActiveArray[3]};
    auto full_region = std::vector<int32_t> {0,0, testActiveArray[2], testActiveArray[3], 1};
    request.update(ANDROID_SCALER_ROTATE_AND_CROP,
            &mode, 1);
    request.update(ANDROID_SCALER_CROP_REGION,
            full_crop.data(), full_crop.size());
    request.update(ANDROID_CONTROL_AE_REGIONS,
            full_region.data(), full_region.size());

    // Map to HAL

    res = mapper.updateCaptureRequest(&request);
    ASSERT_TRUE(res == OK);

    auto e = request.find(ANDROID_CONTROL_AE_REGIONS);
    float aspectRatio = static_cast<float>(full_crop[2]) / full_crop[3];
    int32_t rw = full_crop[3] / aspectRatio;
    int32_t rh = full_crop[3];
    auto rotated_region = std::vector<int32_t> {
        full_crop[0] + (full_crop[2] - rw) / 2, full_crop[1],
        full_crop[0] + (full_crop[2] + rw) / 2, full_crop[1] + full_crop[3],
        1
    };
    EXPECT_THAT(rotated_region, ElementsAreArray(e.data.i32, e.count))
            << "Rotated AE region isn't right";

    e = request.find(ANDROID_SCALER_CROP_REGION);
    EXPECT_THAT(full_crop, ElementsAreArray(e.data.i32, e.count))
            << "Rotated crop region isn't right";

    // Add fields in HAL

    CameraMetadata result(request);

    auto face = std::vector<int32_t> {
        rotated_region[0] + rw / 4, rotated_region[1] + rh / 4,
        rotated_region[2] - rw / 4, rotated_region[3] - rh / 4};
    result.update(ANDROID_STATISTICS_FACE_RECTANGLES,
            face.data(), face.size());

    auto landmarks = std::vector<int32_t> {
        rotated_region[0], rotated_region[1],
        rotated_region[2], rotated_region[3],
        rotated_region[0] + rw / 4, rotated_region[1] + rh / 4,
        rotated_region[0] + rw / 2, rotated_region[1] + rh / 2,
        rotated_region[2] - rw / 4, rotated_region[3] - rh / 4
    };
    result.update(ANDROID_STATISTICS_FACE_LANDMARKS,
            landmarks.data(), landmarks.size());

    // Map to app

    res = mapper.updateCaptureResult(&result);
    ASSERT_TRUE(res == OK);

    // Round-trip results can't be exact since we've gone from a large int range -> small int range
    // and back, leading to quantization. For 4/3 aspect ratio, no more than +-1 error expected

    e = result.find(ANDROID_CONTROL_AE_REGIONS);
    EXPECT_EQUAL_WITHIN_N(full_region, e.data.i32, 1, "Round-tripped AE region isn't right");

    e = result.find(ANDROID_SCALER_CROP_REGION);
    EXPECT_EQUAL_WITHIN_N(full_crop, e.data.i32, 1, "Round-tripped crop region isn't right");

    auto full_face = std::vector<int32_t> {
        full_crop[0] + full_crop[2]/4, full_crop[1] + full_crop[3]/4,
        full_crop[0] + 3*full_crop[2]/4, full_crop[1] + 3*full_crop[3]/4
    };
    e = result.find(ANDROID_STATISTICS_FACE_RECTANGLES);
    EXPECT_EQUAL_WITHIN_N(full_face, e.data.i32, 1, "App-side face rectangle isn't right");

    auto full_landmarks = std::vector<int32_t> {
        full_crop[0] + full_crop[2], full_crop[1],
        full_crop[0], full_crop[1] + full_crop[3],
        full_crop[0] + 3*full_crop[2]/4, full_crop[1] + full_crop[3]/4,
        full_crop[0] + full_crop[2]/2, full_crop[1] + full_crop[3]/2,
        full_crop[0] + full_crop[2]/4, full_crop[1] + 3*full_crop[3]/4
    };
    e = result.find(ANDROID_STATISTICS_FACE_LANDMARKS);
    EXPECT_EQUAL_WITHIN_N(full_landmarks, e.data.i32, 1, "App-side face landmarks aren't right");
}

TEST(RotationMapperTest, Transform180) {
    status_t res;

    CameraMetadata deviceInfo = setupDeviceInfo(testActiveArray,
            basicModes);

    RotateAndCropMapper mapper(&deviceInfo);

    CameraMetadata request;
    uint8_t mode = ANDROID_SCALER_ROTATE_AND_CROP_180;
    auto full_crop = std::vector<int32_t> {0,0, testActiveArray[2], testActiveArray[3]};
    auto full_region = std::vector<int32_t> {0,0, testActiveArray[2], testActiveArray[3], 1};
    request.update(ANDROID_SCALER_ROTATE_AND_CROP,
            &mode, 1);
    request.update(ANDROID_SCALER_CROP_REGION,
            full_crop.data(), full_crop.size());
    request.update(ANDROID_CONTROL_AE_REGIONS,
            full_region.data(), full_region.size());

    // Map to HAL

    res = mapper.updateCaptureRequest(&request);
    ASSERT_TRUE(res == OK);

    auto e = request.find(ANDROID_CONTROL_AE_REGIONS);
    auto rotated_region = full_region;
    EXPECT_THAT(rotated_region, ElementsAreArray(e.data.i32, e.count))
            << "Rotated AE region isn't right";

    e = request.find(ANDROID_SCALER_CROP_REGION);
    EXPECT_THAT(full_crop, ElementsAreArray(e.data.i32, e.count))
            << "Rotated crop region isn't right";

    // Add fields in HAL

    CameraMetadata result(request);

    float rw = full_region[2] - full_region[0];
    float rh = full_region[3] - full_region[1];
    auto face = std::vector<int32_t> {
        rotated_region[0] + (int)(rw / 4), rotated_region[1] + (int)(rh / 4),
        rotated_region[2] - (int)(rw / 4), rotated_region[3] - (int)(rh / 4)
    };
    result.update(ANDROID_STATISTICS_FACE_RECTANGLES,
            face.data(), face.size());

    auto landmarks = std::vector<int32_t> {
        rotated_region[0], rotated_region[1],
        rotated_region[2], rotated_region[3],
        rotated_region[0] + (int)(rw / 4), rotated_region[1] + (int)(rh / 4),
        rotated_region[0] + (int)(rw / 2), rotated_region[1] + (int)(rh / 2),
        rotated_region[2] - (int)(rw / 4), rotated_region[3] - (int)(rh / 4)
    };
    result.update(ANDROID_STATISTICS_FACE_LANDMARKS,
            landmarks.data(), landmarks.size());

    // Map to app

    res = mapper.updateCaptureResult(&result);
    ASSERT_TRUE(res == OK);

    e = result.find(ANDROID_CONTROL_AE_REGIONS);
    EXPECT_THAT(full_region, ElementsAreArray(e.data.i32, e.count))
            << "Round-tripped AE region isn't right";

    e = result.find(ANDROID_SCALER_CROP_REGION);
    EXPECT_THAT(full_crop, ElementsAreArray(e.data.i32, e.count))
            << "Round-tripped crop region isn't right";

    auto full_face = std::vector<int32_t> {
        full_crop[0] + full_crop[2]/4, full_crop[1] + full_crop[3]/4,
        full_crop[0] + 3*full_crop[2]/4, full_crop[1] + 3*full_crop[3]/4
    };
    e = result.find(ANDROID_STATISTICS_FACE_RECTANGLES);
    EXPECT_EQUAL_WITHIN_N(full_face, e.data.i32, 1, "App-side face rectangle isn't right");

    auto full_landmarks = std::vector<int32_t> {
        full_crop[0] + full_crop[2], full_crop[1] + full_crop[3],
        full_crop[0], full_crop[1],
        full_crop[0] + 3*full_crop[2]/4, full_crop[1] + 3*full_crop[3]/4,
        full_crop[0] + full_crop[2]/2, full_crop[1] + full_crop[3]/2,
        full_crop[0] + full_crop[2]/4, full_crop[1] + full_crop[3]/4
    };
    e = result.find(ANDROID_STATISTICS_FACE_LANDMARKS);
    EXPECT_EQUAL_WITHIN_N(full_landmarks, e.data.i32, 1, "App-side face landmarks aren't right");
}


} // namespace rotateAndCropMapperTest
