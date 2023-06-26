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

#define LOG_NDEBUG 0
#define LOG_TAG "ZoomRatioMapperTest"

#include <gtest/gtest.h>
#include <utils/Errors.h>

#include "../device3/ZoomRatioMapper.h"

using namespace std;
using namespace android;
using namespace android::camera3;

constexpr int kMaxAllowedPixelError = 1;
constexpr float kMaxAllowedRatioError = 0.1;

constexpr int32_t testActiveArraySize[] = {100, 100, 1024, 768};
constexpr int32_t testPreCorrActiveArraySize[] = {90, 90, 1044, 788};

constexpr int32_t testDefaultCropSize[][4] = {
      {0, 0, 1024, 768},   // active array default crop
      {0, 0, 1044, 788},   // preCorrection active array default crop
};

constexpr int32_t test2xCropRegion[][4] = {
      {256, 192, 512, 384}, // active array 2x zoom crop
      {261, 197, 522, 394}, // preCorrection active array default crop
};

constexpr int32_t testLetterBoxSize[][4] = {
      {0, 96, 1024, 576}, // active array 2x zoom crop
      {0, 106, 1024, 576}, // preCorrection active array default crop
};

status_t setupTestMapper(ZoomRatioMapper *m, float maxDigitalZoom,
        const int32_t activeArray[4], const int32_t preCorrectArray[4],
        bool hasZoomRatioRange, float zoomRatioRange[2],
        bool usePreCorrectArray) {
    CameraMetadata deviceInfo;

    deviceInfo.update(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, activeArray, 4);
    deviceInfo.update(ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE, preCorrectArray, 4);
    deviceInfo.update(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, &maxDigitalZoom, 1);
    if (hasZoomRatioRange) {
        deviceInfo.update(ANDROID_CONTROL_ZOOM_RATIO_RANGE, zoomRatioRange, 2);
    }

    bool supportNativeZoomRatio;
    status_t res = ZoomRatioMapper::overrideZoomRatioTags(&deviceInfo, &supportNativeZoomRatio);
    if (res != OK) {
        return res;
    }

    *m = ZoomRatioMapper(&deviceInfo, hasZoomRatioRange, usePreCorrectArray);
    return OK;
}

TEST(ZoomRatioTest, Initialization) {
    CameraMetadata deviceInfo;
    status_t res;
    camera_metadata_entry_t entry;

    deviceInfo.update(ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE,
            testPreCorrActiveArraySize, 4);
    deviceInfo.update(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, testActiveArraySize, 4);

    // Test initialization from devices not supporting zoomRange
    float maxDigitalZoom = 4.0f;
    ZoomRatioMapper mapperNoZoomRange;
    deviceInfo.update(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, &maxDigitalZoom, 1);
    bool supportNativeZoomRatio;
    res = ZoomRatioMapper::overrideZoomRatioTags(&deviceInfo, &supportNativeZoomRatio);
    ASSERT_EQ(res, OK);
    ASSERT_EQ(supportNativeZoomRatio, false);
    mapperNoZoomRange = ZoomRatioMapper(&deviceInfo,
            supportNativeZoomRatio, true/*usePreCorrectArray*/);
    ASSERT_TRUE(mapperNoZoomRange.isValid());
    mapperNoZoomRange = ZoomRatioMapper(&deviceInfo,
            supportNativeZoomRatio, false/*usePreCorrectArray*/);
    ASSERT_TRUE(mapperNoZoomRange.isValid());

    entry = deviceInfo.find(ANDROID_CONTROL_ZOOM_RATIO_RANGE);
    ASSERT_EQ(entry.count, 2U);
    ASSERT_EQ(entry.data.f[0], 1.0);
    ASSERT_EQ(entry.data.f[1], maxDigitalZoom);

    entry = deviceInfo.find(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS);
    ASSERT_GT(entry.count, 0U);
    ASSERT_NE(std::find(entry.data.i32, entry.data.i32 + entry.count,
            ANDROID_CONTROL_ZOOM_RATIO_RANGE), entry.data.i32 + entry.count);

    entry = deviceInfo.find(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS);
    ASSERT_GT(entry.count, 0U);
    ASSERT_NE(std::find(entry.data.i32, entry.data.i32 + entry.count,
            ANDROID_CONTROL_ZOOM_RATIO), entry.data.i32 + entry.count);

    entry = deviceInfo.find(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS);
    ASSERT_GT(entry.count, 0U);
    ASSERT_NE(std::find(entry.data.i32, entry.data.i32 + entry.count,
            ANDROID_CONTROL_ZOOM_RATIO), entry.data.i32 + entry.count);

    // Test initialization from devices supporting zoomRange
    float ratioRange[2] = {0.2f, maxDigitalZoom};
    deviceInfo.update(ANDROID_CONTROL_ZOOM_RATIO_RANGE, ratioRange, 2);
    res = ZoomRatioMapper::overrideZoomRatioTags(&deviceInfo, &supportNativeZoomRatio);
    ASSERT_EQ(res, OK);
    ASSERT_EQ(supportNativeZoomRatio, true);
    ZoomRatioMapper mapperWithZoomRange;
    mapperWithZoomRange = ZoomRatioMapper(&deviceInfo,
            supportNativeZoomRatio, true/*usePreCorrectArray*/);
    ASSERT_TRUE(mapperWithZoomRange.isValid());
    mapperWithZoomRange = ZoomRatioMapper(&deviceInfo,
            supportNativeZoomRatio, false/*usePreCorrectArray*/);
    ASSERT_TRUE(mapperWithZoomRange.isValid());

    entry = deviceInfo.find(ANDROID_CONTROL_ZOOM_RATIO_RANGE);
    ASSERT_EQ(entry.count, 2U);
    ASSERT_EQ(entry.data.f[0], ratioRange[0]);
    ASSERT_EQ(entry.data.f[1], ratioRange[1]);

    // Test default zoom ratio in template
    CameraMetadata requestTemplate;
    res = ZoomRatioMapper::initZoomRatioInTemplate(&requestTemplate);
    ASSERT_EQ(res, OK);
    entry = requestTemplate.find(ANDROID_CONTROL_ZOOM_RATIO);
    ASSERT_EQ(entry.count, 1U);
    ASSERT_EQ(entry.data.f[0], 1.0f);

    float customRatio = 0.5f;
    res = requestTemplate.update(ANDROID_CONTROL_ZOOM_RATIO, &customRatio, 1);
    ASSERT_EQ(res, OK);
    res = ZoomRatioMapper::initZoomRatioInTemplate(&requestTemplate);
    ASSERT_EQ(res, OK);
    entry = requestTemplate.find(ANDROID_CONTROL_ZOOM_RATIO);
    ASSERT_EQ(entry.count, 1U);
    ASSERT_EQ(entry.data.f[0], customRatio);
}

void subScaleCoordinatesTest(bool usePreCorrectArray) {
    ZoomRatioMapper mapper;
    float maxDigitalZoom = 4.0f;
    float zoomRatioRange[2];
    ASSERT_EQ(OK, setupTestMapper(&mapper, maxDigitalZoom,
            testActiveArraySize, testPreCorrActiveArraySize,
            false/*hasZoomRatioRange*/, zoomRatioRange,
            usePreCorrectArray));

    size_t index = 0;
    int32_t width = testActiveArraySize[2];
    int32_t height = testActiveArraySize[3];
    if (usePreCorrectArray) {
        index = 1;
        width = testPreCorrActiveArraySize[2];
        height = testPreCorrActiveArraySize[3];
    }

    std::array<int32_t, 16> originalCoords = {
            0, 0, // top-left
            width - 1, 0, // top-right
            0, height - 1, // bottom-left
            width - 1, height - 1, // bottom-right
            (width - 1) / 2, (height - 1) / 2, // center
            (width - 1) / 4, (height - 1) / 4, // top-left after 2x
            (width - 1) / 3, (height - 1) * 2 / 3, // bottom-left after 3x zoom
            (width - 1) * 7 / 8, (height - 1) / 2, // middle-right after 1.33x zoom
    };

    // Verify 1.0x zoom doesn't change the coordinates
    auto coords = originalCoords;
    mapper.scaleCoordinates(coords.data(), coords.size()/2, 1.0f, false /*clamp*/, width, height);
    for (size_t i = 0; i < coords.size(); i++) {
        EXPECT_EQ(coords[i], originalCoords[i]);
    }

    // Verify 2.0x zoom work as expected (no clamping)
    std::array<float, 16> expected2xCoords = {
            - (width - 1) / 2.0f, - (height - 1) / 2.0f,// top-left
            (width - 1) * 3 / 2.0f, - (height - 1) / 2.0f, // top-right
            - (width - 1) / 2.0f, (height - 1) * 3 / 2.0f, // bottom-left
            (width - 1) * 3 / 2.0f, (height - 1) * 3 / 2.0f, // bottom-right
            (width - 1) / 2.0f, (height - 1) / 2.0f, // center
            0, 0, // top-left after 2x
            (width - 1) / 6.0f, (height - 1) * 5.0f / 6.0f, // bottom-left after 3x zoom
            (width - 1) * 5.0f / 4.0f, (height - 1) / 2.0f, // middle-right after 1.33x zoom
    };
    coords = originalCoords;
    mapper.scaleCoordinates(coords.data(), coords.size()/2, 2.0f, false /*clamp*/, width, height);
    for (size_t i = 0; i < coords.size(); i++) {
        EXPECT_LE(std::abs(coords[i] - expected2xCoords[i]), kMaxAllowedPixelError);
    }

    // Verify 2.0x zoom work as expected (with inclusive clamping)
    std::array<float, 16> expected2xCoordsClampedInc = {
            0, 0, // top-left
            width - 1.0f, 0, // top-right
            0, height - 1.0f, // bottom-left
            width - 1.0f, height - 1.0f, // bottom-right
            (width - 1) / 2.0f, (height - 1) / 2.0f, // center
            0, 0, // top-left after 2x
            (width - 1) / 6.0f, (height - 1) * 5.0f / 6.0f , // bottom-left after 3x zoom
            width - 1.0f,  (height - 1) / 2.0f, // middle-right after 1.33x zoom
    };
    coords = originalCoords;
    mapper.scaleCoordinates(coords.data(), coords.size()/2, 2.0f, true /*clamp*/, width, height);
    for (size_t i = 0; i < coords.size(); i++) {
        EXPECT_LE(std::abs(coords[i] - expected2xCoordsClampedInc[i]), kMaxAllowedPixelError);
    }

    // Verify 2.0x zoom work as expected (with exclusive clamping)
    std::array<float, 16> expected2xCoordsClampedExc = {
            0, 0, // top-left
            width - 1.0f, 0, // top-right
            0, height - 1.0f, // bottom-left
            width - 1.0f, height - 1.0f, // bottom-right
            width / 2.0f, height / 2.0f, // center
            0, 0, // top-left after 2x
            (width - 1) / 6.0f, (height - 1) * 5.0f / 6.0f , // bottom-left after 3x zoom
            width - 1.0f,  height / 2.0f, // middle-right after 1.33x zoom
    };
    coords = originalCoords;
    mapper.scaleCoordinates(coords.data(), coords.size()/2, 2.0f, true /*clamp*/, width, height);
    for (size_t i = 0; i < coords.size(); i++) {
        EXPECT_LE(std::abs(coords[i] - expected2xCoordsClampedExc[i]), kMaxAllowedPixelError);
    }

    // Verify 0.33x zoom work as expected
    std::array<float, 16> expectedZoomOutCoords = {
            (width - 1) / 3.0f, (height - 1) / 3.0f, // top-left
            (width - 1) * 2 / 3.0f, (height - 1) / 3.0f, // top-right
            (width - 1) / 3.0f, (height - 1) * 2 / 3.0f, // bottom-left
            (width - 1) * 2 / 3.0f, (height - 1) * 2 / 3.0f, // bottom-right
            (width - 1) / 2.0f, (height - 1) / 2.0f, // center
            (width - 1) * 5 / 12.0f, (height - 1) * 5 / 12.0f, // top-left after 2x
            (width - 1) * 4 / 9.0f, (height - 1) * 5 / 9.0f, // bottom-left after 3x zoom-in
            (width - 1) * 5 / 8.0f, (height - 1) / 2.0f, // middle-right after 1.33x zoom-in
    };
    coords = originalCoords;
    mapper.scaleCoordinates(coords.data(), coords.size()/2, 1.0f/3, false /*clamp*/, width, height);
    for (size_t i = 0; i < coords.size(); i++) {
        EXPECT_LE(std::abs(coords[i] - expectedZoomOutCoords[i]), kMaxAllowedPixelError);
    }
}

TEST(ZoomRatioTest, scaleCoordinatesTest) {
    subScaleCoordinatesTest(false/*usePreCorrectArray*/);
    subScaleCoordinatesTest(true/*usePreCorrectArray*/);
}

void subCropOverMaxDigitalZoomTest(bool usePreCorrectArray) {
    status_t res;
    ZoomRatioMapper mapper;
    float noZoomRatioRange[2];
    res = setupTestMapper(&mapper, 4.0/*maxDigitalZoom*/,
            testActiveArraySize, testPreCorrActiveArraySize,
            false/*hasZoomRatioRange*/, noZoomRatioRange,
            usePreCorrectArray);
    ASSERT_EQ(res, OK);

    CameraMetadata metadata;
    camera_metadata_entry_t entry;

    size_t index = usePreCorrectArray ? 1 : 0;
    metadata.update(ANDROID_SCALER_CROP_REGION, testDefaultCropSize[index], 4);
    res = mapper.updateCaptureRequest(&metadata);
    ASSERT_EQ(res, OK);
    entry = metadata.find(ANDROID_SCALER_CROP_REGION);
    ASSERT_EQ(entry.count, 4U);
    for (int i = 0; i < 4; i ++) {
        EXPECT_EQ(entry.data.i32[i], testDefaultCropSize[index][i]);
    }

    metadata.update(ANDROID_SCALER_CROP_REGION, test2xCropRegion[index], 4);
    res = mapper.updateCaptureResult(&metadata, true/*requestedZoomRatioIs1*/);
    ASSERT_EQ(res, OK);
    entry = metadata.find(ANDROID_SCALER_CROP_REGION);
    ASSERT_EQ(entry.count, 4U);
    for (int i = 0; i < 4; i ++) {
        EXPECT_EQ(entry.data.i32[i], test2xCropRegion[index][i]);
    }
    entry = metadata.find(ANDROID_CONTROL_ZOOM_RATIO);
    ASSERT_TRUE(entry.count == 0 || (entry.count == 1 && entry.data.f[0] == 1.0f));
}

TEST(ZoomRatioTest, CropOverMaxDigitalZoomTest) {
    subCropOverMaxDigitalZoomTest(false/*usePreCorrectArray*/);
    subCropOverMaxDigitalZoomTest(true/*usePreCorrectArray*/);
}

void subCropOverZoomRangeTest(bool usePreCorrectArray) {
    status_t res;
    ZoomRatioMapper mapper;
    float zoomRatioRange[2] = {0.5f, 4.0f};
    res = setupTestMapper(&mapper, 4.0/*maxDigitalZoom*/,
            testActiveArraySize, testPreCorrActiveArraySize,
            true/*hasZoomRatioRange*/, zoomRatioRange,
            usePreCorrectArray);
    ASSERT_EQ(res, OK);

    CameraMetadata metadata;
    camera_metadata_entry_t entry;

    size_t index = usePreCorrectArray ? 1 : 0;

    // 2x zoom crop region, zoomRatio is 1.0f
    metadata.update(ANDROID_SCALER_CROP_REGION, test2xCropRegion[index], 4);
    res = mapper.updateCaptureRequest(&metadata);
    ASSERT_EQ(res, OK);
    entry = metadata.find(ANDROID_SCALER_CROP_REGION);
    ASSERT_EQ(entry.count, 4U);
    for (int i = 0; i < 4; i++) {
        EXPECT_LE(std::abs(entry.data.i32[i] - testDefaultCropSize[index][i]),
                kMaxAllowedPixelError);
    }
    entry = metadata.find(ANDROID_CONTROL_ZOOM_RATIO);
    EXPECT_NEAR(entry.data.f[0], 2.0f, kMaxAllowedRatioError);

    res = mapper.updateCaptureResult(&metadata, true/*requestedZoomRatioIs1*/);
    ASSERT_EQ(res, OK);
    entry = metadata.find(ANDROID_CONTROL_ZOOM_RATIO);
    EXPECT_NEAR(entry.data.f[0], 1.0f, kMaxAllowedRatioError);
    entry = metadata.find(ANDROID_SCALER_CROP_REGION);
    ASSERT_EQ(entry.count, 4U);
    for (int i = 0; i < 4; i++) {
        EXPECT_LE(std::abs(entry.data.i32[i] - test2xCropRegion[index][i]), kMaxAllowedPixelError);
    }

    // Letter boxing crop region, zoomRatio is 1.0
    float zoomRatio = 1.0f;
    metadata.update(ANDROID_CONTROL_ZOOM_RATIO, &zoomRatio, 1);
    metadata.update(ANDROID_SCALER_CROP_REGION, testLetterBoxSize[index], 4);
    res = mapper.updateCaptureRequest(&metadata);
    ASSERT_EQ(res, OK);
    entry = metadata.find(ANDROID_SCALER_CROP_REGION);
    ASSERT_EQ(entry.count, 4U);
    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(entry.data.i32[i], testLetterBoxSize[index][i]);
    }
    entry = metadata.find(ANDROID_CONTROL_ZOOM_RATIO);
    EXPECT_NEAR(entry.data.f[0], 1.0f, kMaxAllowedRatioError);

    res = mapper.updateCaptureResult(&metadata, true/*requestedZoomRatioIs1*/);
    ASSERT_EQ(res, OK);
    entry = metadata.find(ANDROID_SCALER_CROP_REGION);
    ASSERT_EQ(entry.count, 4U);
    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(entry.data.i32[i], testLetterBoxSize[index][i]);
    }
    entry = metadata.find(ANDROID_CONTROL_ZOOM_RATIO);
    EXPECT_NEAR(entry.data.f[0], 1.0f, kMaxAllowedRatioError);
}

TEST(ZoomRatioTest, CropOverZoomRangeTest) {
    subCropOverZoomRangeTest(false/*usePreCorrectArray*/);
    subCropOverZoomRangeTest(true/*usePreCorrectArray*/);
}

void subZoomOverMaxDigitalZoomTest(bool usePreCorrectArray) {
    status_t res;
    ZoomRatioMapper mapper;
    float noZoomRatioRange[2];
    res = setupTestMapper(&mapper, 4.0/*maxDigitalZoom*/,
            testActiveArraySize, testPreCorrActiveArraySize,
            false/*hasZoomRatioRange*/, noZoomRatioRange,
            usePreCorrectArray);
    ASSERT_EQ(res, OK);

    CameraMetadata metadata;
    float zoomRatio = 3.0f;
    camera_metadata_entry_t entry;

    size_t index = usePreCorrectArray ? 1 : 0;

    // Full active array crop, zoomRatio is 3.0f
    metadata.update(ANDROID_SCALER_CROP_REGION, testDefaultCropSize[index], 4);
    metadata.update(ANDROID_CONTROL_ZOOM_RATIO, &zoomRatio, 1);
    res = mapper.updateCaptureRequest(&metadata);
    ASSERT_EQ(res, OK);
    entry = metadata.find(ANDROID_SCALER_CROP_REGION);
    ASSERT_EQ(entry.count, 4U);
    std::array<float, 4> expectedCrop = {
        testDefaultCropSize[index][2] / 3.0f, /*x*/
        testDefaultCropSize[index][3] / 3.0f, /*y*/
        testDefaultCropSize[index][2] / 3.0f, /*width*/
        testDefaultCropSize[index][3] / 3.0f, /*height*/
    };
    for (int i = 0; i < 4; i++) {
        EXPECT_LE(std::abs(entry.data.i32[i] - expectedCrop[i]), kMaxAllowedPixelError);
    }

    entry = metadata.find(ANDROID_CONTROL_ZOOM_RATIO);
    if (entry.count == 1) {
        EXPECT_NEAR(entry.data.f[0], 1.0f, kMaxAllowedRatioError);
    }
}

TEST(ZoomRatioTest, ZoomOverMaxDigitalZoomTest) {
    subZoomOverMaxDigitalZoomTest(false/*usePreCorrectArray*/);
    subZoomOverMaxDigitalZoomTest(true/*usePreCorrectArray*/);
}

void subZoomOverZoomRangeTest(bool usePreCorrectArray) {
    status_t res;
    ZoomRatioMapper mapper;
    float zoomRatioRange[2] = {1.0f, 4.0f};
    res = setupTestMapper(&mapper, 4.0/*maxDigitalZoom*/,
            testActiveArraySize, testPreCorrActiveArraySize,
            true/*hasZoomRatioRange*/, zoomRatioRange,
            usePreCorrectArray);
    ASSERT_EQ(res, OK);

    CameraMetadata metadata;
    float zoomRatio = 3.0f;
    camera_metadata_entry_t entry;
    size_t index = usePreCorrectArray ? 1 : 0;

    // Full active array crop, zoomRatio is 3.0f
    metadata.update(ANDROID_SCALER_CROP_REGION, testDefaultCropSize[index], 4);
    metadata.update(ANDROID_CONTROL_ZOOM_RATIO, &zoomRatio, 1);
    res = mapper.updateCaptureRequest(&metadata);
    ASSERT_EQ(res, OK);
    entry = metadata.find(ANDROID_SCALER_CROP_REGION);
    ASSERT_EQ(entry.count, 4U);
    for (int i = 0; i < 4; i ++) {
        EXPECT_EQ(entry.data.i32[i], testDefaultCropSize[index][i]);
    }
    entry = metadata.find(ANDROID_CONTROL_ZOOM_RATIO);
    ASSERT_EQ(entry.data.f[0], zoomRatio);

    res = mapper.updateCaptureResult(&metadata, false/*requestedZoomRatioIs1*/);
    ASSERT_EQ(res, OK);
    entry = metadata.find(ANDROID_SCALER_CROP_REGION);
    ASSERT_EQ(entry.count, 4U);
    for (int i = 0; i < 4; i ++) {
        EXPECT_EQ(entry.data.i32[i], testDefaultCropSize[index][i]);
    }
    entry = metadata.find(ANDROID_CONTROL_ZOOM_RATIO);
    ASSERT_EQ(entry.data.f[0], zoomRatio);
}

TEST(ZoomRatioTest, ZoomOverZoomRangeTest) {
    subZoomOverZoomRangeTest(false/*usePreCorrectArray*/);
    subZoomOverZoomRangeTest(true/*usePreCorrectArray*/);
}
