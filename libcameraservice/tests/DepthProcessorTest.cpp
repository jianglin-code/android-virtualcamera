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
#define LOG_TAG "DepthProcessorTest"

#include <array>
#include <random>

#include <gtest/gtest.h>

#include "../common/DepthPhotoProcessor.h"
#include "../utils/ExifUtils.h"
#include "NV12Compressor.h"

using namespace android;
using namespace android::camera3;

static const size_t kTestBufferWidth = 640;
static const size_t kTestBufferHeight = 480;
static const size_t kTestBufferNV12Size ((((kTestBufferWidth) * (kTestBufferHeight)) * 3) / 2);
static const size_t kTestBufferDepthSize (kTestBufferWidth * kTestBufferHeight);
static const size_t kSeed = 1234;

void generateColorJpegBuffer(int jpegQuality, ExifOrientation orientationValue, bool includeExif,
        bool switchDimensions, std::vector<uint8_t> *colorJpegBuffer /*out*/) {
    ASSERT_NE(colorJpegBuffer, nullptr);

    std::array<uint8_t, kTestBufferNV12Size> colorSourceBuffer;
    std::default_random_engine gen(kSeed);
    std::uniform_int_distribution<int> uniDist(0, UINT8_MAX - 1);
    for (size_t i = 0; i < colorSourceBuffer.size(); i++) {
        colorSourceBuffer[i] = uniDist(gen);
    }

    size_t width = kTestBufferWidth;
    size_t height = kTestBufferHeight;
    if (switchDimensions) {
        width = kTestBufferHeight;
        height = kTestBufferWidth;
    }

    NV12Compressor jpegCompressor;
    if (includeExif) {
        ASSERT_TRUE(jpegCompressor.compressWithExifOrientation(
                reinterpret_cast<const unsigned char*> (colorSourceBuffer.data()), width, height,
                jpegQuality, orientationValue));
    } else {
        ASSERT_TRUE(jpegCompressor.compress(
                reinterpret_cast<const unsigned char*> (colorSourceBuffer.data()), width, height,
                jpegQuality));
    }

    *colorJpegBuffer = std::move(jpegCompressor.getCompressedData());
    ASSERT_FALSE(colorJpegBuffer->empty());
}

void generateDepth16Buffer(std::array<uint16_t, kTestBufferDepthSize> *depth16Buffer /*out*/) {
    ASSERT_NE(depth16Buffer, nullptr);
    std::default_random_engine gen(kSeed+1);
    std::uniform_int_distribution<int> uniDist(0, UINT16_MAX - 1);
    for (size_t i = 0; i < depth16Buffer->size(); i++) {
        (*depth16Buffer)[i] = uniDist(gen);
    }
}

TEST(DepthProcessorTest, BadInput) {
    int jpegQuality = 95;

    DepthPhotoInputFrame inputFrame;
    // Worst case both depth and confidence maps have the same size as the main color image.
    inputFrame.mMaxJpegSize = inputFrame.mMainJpegSize * 3;

    std::vector<uint8_t> colorJpegBuffer;
    generateColorJpegBuffer(jpegQuality, ExifOrientation::ORIENTATION_UNDEFINED,
            /*includeExif*/ false, /*switchDimensions*/ false, &colorJpegBuffer);

    std::array<uint16_t, kTestBufferDepthSize> depth16Buffer;
    generateDepth16Buffer(&depth16Buffer);

    std::vector<uint8_t> depthPhotoBuffer(inputFrame.mMaxJpegSize);
    size_t actualDepthPhotoSize = 0;

    inputFrame.mMainJpegWidth = kTestBufferWidth;
    inputFrame.mMainJpegHeight = kTestBufferHeight;
    inputFrame.mJpegQuality = jpegQuality;
    ASSERT_NE(processDepthPhotoFrame(inputFrame, depthPhotoBuffer.size(), depthPhotoBuffer.data(),
                &actualDepthPhotoSize), 0);

    inputFrame.mMainJpegBuffer = reinterpret_cast<const char*> (colorJpegBuffer.data());
    inputFrame.mMainJpegSize = colorJpegBuffer.size();
    ASSERT_NE(processDepthPhotoFrame(inputFrame, depthPhotoBuffer.size(), depthPhotoBuffer.data(),
                &actualDepthPhotoSize), 0);

    inputFrame.mDepthMapBuffer = depth16Buffer.data();
    inputFrame.mDepthMapWidth = inputFrame.mDepthMapStride = kTestBufferWidth;
    inputFrame.mDepthMapHeight = kTestBufferHeight;
    ASSERT_NE(processDepthPhotoFrame(inputFrame, depthPhotoBuffer.size(), nullptr,
                &actualDepthPhotoSize), 0);

    ASSERT_NE(processDepthPhotoFrame(inputFrame, depthPhotoBuffer.size(), depthPhotoBuffer.data(),
                nullptr), 0);
}

TEST(DepthProcessorTest, BasicDepthPhotoValidation) {
    int jpegQuality = 95;

    std::vector<uint8_t> colorJpegBuffer;
    generateColorJpegBuffer(jpegQuality, ExifOrientation::ORIENTATION_UNDEFINED,
            /*includeExif*/ false, /*switchDimensions*/ false, &colorJpegBuffer);

    std::array<uint16_t, kTestBufferDepthSize> depth16Buffer;
    generateDepth16Buffer(&depth16Buffer);

    DepthPhotoInputFrame inputFrame;
    inputFrame.mMainJpegBuffer = reinterpret_cast<const char*> (colorJpegBuffer.data());
    inputFrame.mMainJpegSize = colorJpegBuffer.size();
    // Worst case both depth and confidence maps have the same size as the main color image.
    inputFrame.mMaxJpegSize = inputFrame.mMainJpegSize * 3;
    inputFrame.mMainJpegWidth = kTestBufferWidth;
    inputFrame.mMainJpegHeight = kTestBufferHeight;
    inputFrame.mJpegQuality = jpegQuality;
    inputFrame.mDepthMapBuffer = depth16Buffer.data();
    inputFrame.mDepthMapWidth = inputFrame.mDepthMapStride = kTestBufferWidth;
    inputFrame.mDepthMapHeight = kTestBufferHeight;

    std::vector<uint8_t> depthPhotoBuffer(inputFrame.mMaxJpegSize);
    size_t actualDepthPhotoSize = 0;
    ASSERT_EQ(processDepthPhotoFrame(inputFrame, depthPhotoBuffer.size(), depthPhotoBuffer.data(),
                &actualDepthPhotoSize), 0);
    ASSERT_TRUE((actualDepthPhotoSize > 0) && (depthPhotoBuffer.size() >= actualDepthPhotoSize));

    // The final depth photo must consist of three jpeg images:
    //  - the main color image
    //  - the depth map image
    //  - the confidence map image
    size_t mainJpegSize = 0;
    ASSERT_EQ(NV12Compressor::findJpegSize(depthPhotoBuffer.data(), actualDepthPhotoSize,
                &mainJpegSize), OK);
    ASSERT_TRUE((mainJpegSize > 0) && (mainJpegSize < actualDepthPhotoSize));
    size_t depthMapSize = 0;
    ASSERT_EQ(NV12Compressor::findJpegSize(depthPhotoBuffer.data() + mainJpegSize,
                actualDepthPhotoSize - mainJpegSize, &depthMapSize), OK);
    ASSERT_TRUE((depthMapSize > 0) && (depthMapSize < (actualDepthPhotoSize - mainJpegSize)));
}

TEST(DepthProcessorTest, TestDepthPhotoExifOrientation) {
    int jpegQuality = 95;

    ExifOrientation exifOrientations[] = { ExifOrientation::ORIENTATION_UNDEFINED,
            ExifOrientation::ORIENTATION_0_DEGREES, ExifOrientation::ORIENTATION_90_DEGREES,
            ExifOrientation::ORIENTATION_180_DEGREES, ExifOrientation::ORIENTATION_270_DEGREES };
    for (auto exifOrientation : exifOrientations) {
        std::vector<uint8_t> colorJpegBuffer;
        generateColorJpegBuffer(jpegQuality, exifOrientation, /*includeExif*/ true,
                /*switchDimensions*/ false, &colorJpegBuffer);
        if (exifOrientation != ExifOrientation::ORIENTATION_UNDEFINED) {
            auto jpegExifOrientation = ExifOrientation::ORIENTATION_UNDEFINED;
            ASSERT_EQ(NV12Compressor::getExifOrientation(colorJpegBuffer.data(),
                    colorJpegBuffer.size(), &jpegExifOrientation), OK);
            ASSERT_EQ(exifOrientation, jpegExifOrientation);
        }

        std::array<uint16_t, kTestBufferDepthSize> depth16Buffer;
        generateDepth16Buffer(&depth16Buffer);

        DepthPhotoInputFrame inputFrame;
        inputFrame.mMainJpegBuffer = reinterpret_cast<const char*> (colorJpegBuffer.data());
        inputFrame.mMainJpegSize = colorJpegBuffer.size();
        // Worst case both depth and confidence maps have the same size as the main color image.
        inputFrame.mMaxJpegSize = inputFrame.mMainJpegSize * 3;
        inputFrame.mMainJpegWidth = kTestBufferWidth;
        inputFrame.mMainJpegHeight = kTestBufferHeight;
        inputFrame.mJpegQuality = jpegQuality;
        inputFrame.mDepthMapBuffer = depth16Buffer.data();
        inputFrame.mDepthMapWidth = inputFrame.mDepthMapStride = kTestBufferWidth;
        inputFrame.mDepthMapHeight = kTestBufferHeight;

        std::vector<uint8_t> depthPhotoBuffer(inputFrame.mMaxJpegSize);
        size_t actualDepthPhotoSize = 0;
        ASSERT_EQ(processDepthPhotoFrame(inputFrame, depthPhotoBuffer.size(),
                    depthPhotoBuffer.data(), &actualDepthPhotoSize), 0);
        ASSERT_TRUE((actualDepthPhotoSize > 0) &&
                (depthPhotoBuffer.size() >= actualDepthPhotoSize));

        size_t mainJpegSize = 0;
        ASSERT_EQ(NV12Compressor::findJpegSize(depthPhotoBuffer.data(), actualDepthPhotoSize,
                &mainJpegSize), OK);
        ASSERT_TRUE((mainJpegSize > 0) && (mainJpegSize < actualDepthPhotoSize));
        size_t depthMapSize = 0;
        ASSERT_EQ(NV12Compressor::findJpegSize(depthPhotoBuffer.data() + mainJpegSize,
                actualDepthPhotoSize - mainJpegSize, &depthMapSize), OK);
        ASSERT_TRUE((depthMapSize > 0) && (depthMapSize < (actualDepthPhotoSize - mainJpegSize)));
        size_t confidenceMapSize = actualDepthPhotoSize - (mainJpegSize + depthMapSize);

        //Depth and confidence images must have the same EXIF orientation as the source
        auto depthJpegExifOrientation = ExifOrientation::ORIENTATION_UNDEFINED;
        ASSERT_EQ(NV12Compressor::getExifOrientation(depthPhotoBuffer.data() + mainJpegSize,
                depthMapSize, &depthJpegExifOrientation), OK);
        if (exifOrientation == ORIENTATION_UNDEFINED) {
            // In case of undefined or missing EXIF orientation, always expect 0 degrees in the
            // depth map.
            ASSERT_EQ(depthJpegExifOrientation, ExifOrientation::ORIENTATION_0_DEGREES);
        } else {
            ASSERT_EQ(depthJpegExifOrientation, exifOrientation);
        }

        auto confidenceJpegExifOrientation = ExifOrientation::ORIENTATION_UNDEFINED;
        ASSERT_EQ(NV12Compressor::getExifOrientation(
                depthPhotoBuffer.data() + mainJpegSize + depthMapSize,
                confidenceMapSize, &confidenceJpegExifOrientation), OK);
        if (exifOrientation == ORIENTATION_UNDEFINED) {
            // In case of undefined or missing EXIF orientation, always expect 0 degrees in the
            // confidence map.
            ASSERT_EQ(confidenceJpegExifOrientation, ExifOrientation::ORIENTATION_0_DEGREES);
        } else {
            ASSERT_EQ(confidenceJpegExifOrientation, exifOrientation);
        }
    }
}

TEST(DepthProcessorTest, TestDephtPhotoPhysicalRotation) {
    int jpegQuality = 95;

    // In case of physical rotation, the EXIF orientation must always be 0.
    auto exifOrientation = ExifOrientation::ORIENTATION_0_DEGREES;
    DepthPhotoOrientation depthOrientations[] = {
            DepthPhotoOrientation::DEPTH_ORIENTATION_0_DEGREES,
            DepthPhotoOrientation::DEPTH_ORIENTATION_90_DEGREES,
            DepthPhotoOrientation::DEPTH_ORIENTATION_180_DEGREES,
            DepthPhotoOrientation::DEPTH_ORIENTATION_270_DEGREES };
    for (auto depthOrientation : depthOrientations) {
        std::vector<uint8_t> colorJpegBuffer;
        bool switchDimensions = false;
        size_t expectedWidth = kTestBufferWidth;
        size_t expectedHeight = kTestBufferHeight;
        if ((depthOrientation == DepthPhotoOrientation::DEPTH_ORIENTATION_90_DEGREES) ||
                (depthOrientation == DepthPhotoOrientation::DEPTH_ORIENTATION_270_DEGREES)) {
            switchDimensions = true;
            expectedWidth = kTestBufferHeight;
            expectedHeight = kTestBufferWidth;
        }
        generateColorJpegBuffer(jpegQuality, exifOrientation, /*includeExif*/ true,
                switchDimensions, &colorJpegBuffer);
        auto jpegExifOrientation = ExifOrientation::ORIENTATION_UNDEFINED;
        ASSERT_EQ(NV12Compressor::getExifOrientation(colorJpegBuffer.data(), colorJpegBuffer.size(),
                &jpegExifOrientation), OK);
        ASSERT_EQ(exifOrientation, jpegExifOrientation);

        std::array<uint16_t, kTestBufferDepthSize> depth16Buffer;
        generateDepth16Buffer(&depth16Buffer);

        DepthPhotoInputFrame inputFrame;
        inputFrame.mMainJpegBuffer = reinterpret_cast<const char*> (colorJpegBuffer.data());
        inputFrame.mMainJpegSize = colorJpegBuffer.size();
        // Worst case both depth and confidence maps have the same size as the main color image.
        inputFrame.mMaxJpegSize = inputFrame.mMainJpegSize * 3;
        inputFrame.mMainJpegWidth = kTestBufferWidth;
        inputFrame.mMainJpegHeight = kTestBufferHeight;
        inputFrame.mJpegQuality = jpegQuality;
        inputFrame.mDepthMapBuffer = depth16Buffer.data();
        inputFrame.mDepthMapWidth = inputFrame.mDepthMapStride = kTestBufferWidth;
        inputFrame.mDepthMapHeight = kTestBufferHeight;
        inputFrame.mOrientation = depthOrientation;

        std::vector<uint8_t> depthPhotoBuffer(inputFrame.mMaxJpegSize);
        size_t actualDepthPhotoSize = 0;
        ASSERT_EQ(processDepthPhotoFrame(inputFrame, depthPhotoBuffer.size(),
                    depthPhotoBuffer.data(), &actualDepthPhotoSize), 0);
        ASSERT_TRUE((actualDepthPhotoSize > 0) &&
                (depthPhotoBuffer.size() >= actualDepthPhotoSize));

        size_t mainJpegSize = 0;
        ASSERT_EQ(NV12Compressor::findJpegSize(depthPhotoBuffer.data(), actualDepthPhotoSize,
                &mainJpegSize), OK);
        ASSERT_TRUE((mainJpegSize > 0) && (mainJpegSize < actualDepthPhotoSize));
        size_t depthMapSize = 0;
        ASSERT_EQ(NV12Compressor::findJpegSize(depthPhotoBuffer.data() + mainJpegSize,
                actualDepthPhotoSize - mainJpegSize, &depthMapSize), OK);
        ASSERT_TRUE((depthMapSize > 0) && (depthMapSize < (actualDepthPhotoSize - mainJpegSize)));
        size_t confidenceMapSize = actualDepthPhotoSize - (mainJpegSize + depthMapSize);

        //Depth and confidence images must have the same EXIF orientation as the source
        auto depthJpegExifOrientation = ExifOrientation::ORIENTATION_UNDEFINED;
        ASSERT_EQ(NV12Compressor::getExifOrientation(depthPhotoBuffer.data() + mainJpegSize,
                depthMapSize, &depthJpegExifOrientation), OK);
        ASSERT_EQ(depthJpegExifOrientation, exifOrientation);
        size_t depthMapWidth, depthMapHeight;
        ASSERT_EQ(NV12Compressor::getJpegImageDimensions(depthPhotoBuffer.data() + mainJpegSize,
                depthMapSize, &depthMapWidth, &depthMapHeight), OK);
        ASSERT_EQ(depthMapWidth, expectedWidth);
        ASSERT_EQ(depthMapHeight, expectedHeight);

        auto confidenceJpegExifOrientation = ExifOrientation::ORIENTATION_UNDEFINED;
        ASSERT_EQ(NV12Compressor::getExifOrientation(
                depthPhotoBuffer.data() + mainJpegSize + depthMapSize, confidenceMapSize,
                &confidenceJpegExifOrientation), OK);
        ASSERT_EQ(confidenceJpegExifOrientation, exifOrientation);
        size_t confidenceMapWidth, confidenceMapHeight;
        ASSERT_EQ(NV12Compressor::getJpegImageDimensions(
                depthPhotoBuffer.data() + mainJpegSize + depthMapSize, confidenceMapSize,
                &confidenceMapWidth, &confidenceMapHeight), OK);
        ASSERT_EQ(confidenceMapWidth, expectedWidth);
        ASSERT_EQ(confidenceMapHeight, expectedHeight);
    }
}
