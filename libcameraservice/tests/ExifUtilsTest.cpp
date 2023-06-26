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

//#define LOG_NDEBUG 0
#define LOG_TAG "ExifUtilsTest"

#include <camera/CameraMetadata.h>
#include "../utils/ExifUtils.h"
#include <gtest/gtest.h>

using android::camera3::ExifUtils;
using android::camera3::ExifOrientation;
using android::CameraMetadata;

uint32_t kImageWidth = 1920;
uint32_t kImageHeight = 1440;
ExifOrientation kExifOrientation = ExifOrientation::ORIENTATION_0_DEGREES;

// Test that setFromMetadata works correctly, without errors.
TEST(ExifUtilsTest, SetFromMetadataTest) {
    std::unique_ptr<ExifUtils> utils(ExifUtils::create());
    uint8_t invalidSensorPixelMode = 2;
    uint8_t validSensorPixelMode = ANDROID_SENSOR_PIXEL_MODE_DEFAULT;
    CameraMetadata metadata;
    // Empty staticInfo
    CameraMetadata staticInfo;
    ASSERT_TRUE(utils->initializeEmpty());
    ASSERT_TRUE(
            metadata.update(ANDROID_SENSOR_PIXEL_MODE, &invalidSensorPixelMode, 1) == android::OK);
    ASSERT_FALSE(utils->setFromMetadata(metadata, staticInfo, kImageWidth, kImageHeight));
    ASSERT_TRUE(
            metadata.update(ANDROID_SENSOR_PIXEL_MODE, &validSensorPixelMode, 1) == android::OK);
    ASSERT_TRUE(utils->setFromMetadata(metadata, staticInfo, kImageWidth, kImageHeight));
    ASSERT_TRUE(utils->setImageWidth(kImageWidth));
    ASSERT_TRUE(utils->setImageHeight(kImageHeight));
    ASSERT_TRUE(utils->setOrientationValue(kExifOrientation));
    ASSERT_TRUE(utils->generateApp1());
    const uint8_t* exifBuffer = utils->getApp1Buffer();
    ASSERT_NE(exifBuffer, nullptr);
    size_t exifBufferSize = utils->getApp1Length();
    ASSERT_TRUE(exifBufferSize != 0);
}
