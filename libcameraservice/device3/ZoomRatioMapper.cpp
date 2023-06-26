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

#define LOG_TAG "Camera3-ZoomRatioMapper"
//#define LOG_NDEBUG 0

#include <algorithm>

#include "device3/ZoomRatioMapper.h"
#include "utils/SessionConfigurationUtils.h"

namespace android {

namespace camera3 {

void ZoomRatioMapper::initRemappedKeys() {
    mRemappedKeys.insert(
            kMeteringRegionsToCorrect.begin(),
            kMeteringRegionsToCorrect.end());
    mRemappedKeys.insert(
            kRectsToCorrect.begin(),
            kRectsToCorrect.end());
    mRemappedKeys.insert(
            kResultPointsToCorrectNoClamp.begin(),
            kResultPointsToCorrectNoClamp.end());

    mRemappedKeys.insert(ANDROID_CONTROL_ZOOM_RATIO);
}

status_t ZoomRatioMapper::initZoomRatioInTemplate(CameraMetadata *request) {
    camera_metadata_entry_t entry;
    entry = request->find(ANDROID_CONTROL_ZOOM_RATIO);
    float defaultZoomRatio = 1.0f;
    if (entry.count == 0) {
        return request->update(ANDROID_CONTROL_ZOOM_RATIO, &defaultZoomRatio, 1);
    }
    return OK;
}

status_t ZoomRatioMapper::overrideZoomRatioTags(
        CameraMetadata* deviceInfo, bool* supportNativeZoomRatio) {
    if (deviceInfo == nullptr || supportNativeZoomRatio == nullptr) {
        return BAD_VALUE;
    }

    camera_metadata_entry_t entry;
    entry = deviceInfo->find(ANDROID_CONTROL_ZOOM_RATIO_RANGE);
    if (entry.count != 2 && entry.count != 0) return BAD_VALUE;

    // Hal has zoom ratio support
    if (entry.count == 2) {
        *supportNativeZoomRatio = true;
        return OK;
    }

    // Hal has no zoom ratio support
    *supportNativeZoomRatio = false;

    entry = deviceInfo->find(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM);
    if (entry.count != 1) {
        ALOGI("%s: Camera device doesn't support SCALER_AVAILABLE_MAX_DIGITAL_ZOOM key!",
                __FUNCTION__);
        return OK;
    }

    float zoomRange[] = {1.0f, entry.data.f[0]};
    status_t res = deviceInfo->update(ANDROID_CONTROL_ZOOM_RATIO_RANGE, zoomRange, 2);
    if (res != OK) {
        ALOGE("%s: Failed to update CONTROL_ZOOM_RATIO_RANGE key: %s (%d)",
                __FUNCTION__, strerror(-res), res);
        return res;
    }

    std::vector<int32_t> requestKeys;
    entry = deviceInfo->find(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS);
    if (entry.count > 0) {
        requestKeys.insert(requestKeys.end(), entry.data.i32, entry.data.i32 + entry.count);
    }
    requestKeys.push_back(ANDROID_CONTROL_ZOOM_RATIO);
    res = deviceInfo->update(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS,
            requestKeys.data(), requestKeys.size());
    if (res != OK) {
        ALOGE("%s: Failed to update REQUEST_AVAILABLE_REQUEST_KEYS: %s (%d)",
                __FUNCTION__, strerror(-res), res);
        return res;
    }

    std::vector<int32_t> resultKeys;
    entry = deviceInfo->find(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS);
    if (entry.count > 0) {
        resultKeys.insert(resultKeys.end(), entry.data.i32, entry.data.i32 + entry.count);
    }
    resultKeys.push_back(ANDROID_CONTROL_ZOOM_RATIO);
    res = deviceInfo->update(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
            resultKeys.data(), resultKeys.size());
    if (res != OK) {
        ALOGE("%s: Failed to update REQUEST_AVAILABLE_RESULT_KEYS: %s (%d)",
                __FUNCTION__, strerror(-res), res);
        return res;
    }

    std::vector<int32_t> charKeys;
    entry = deviceInfo->find(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS);
    if (entry.count > 0) {
        charKeys.insert(charKeys.end(), entry.data.i32, entry.data.i32 + entry.count);
    }
    charKeys.push_back(ANDROID_CONTROL_ZOOM_RATIO_RANGE);
    res = deviceInfo->update(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
            charKeys.data(), charKeys.size());
    if (res != OK) {
        ALOGE("%s: Failed to update REQUEST_AVAILABLE_CHARACTERISTICS_KEYS: %s (%d)",
                __FUNCTION__, strerror(-res), res);
        return res;
    }

    return OK;
}

ZoomRatioMapper::ZoomRatioMapper(const CameraMetadata* deviceInfo,
        bool supportNativeZoomRatio, bool usePrecorrectArray) {
    initRemappedKeys();

    int32_t arrayW = 0;
    int32_t arrayH = 0;
    int32_t arrayMaximumResolutionW = 0;
    int32_t arrayMaximumResolutionH = 0;
    int32_t activeW = 0;
    int32_t activeH = 0;
    int32_t activeMaximumResolutionW = 0;
    int32_t activeMaximumResolutionH = 0;

    if (!SessionConfigurationUtils::getArrayWidthAndHeight(deviceInfo,
            ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE, &arrayW, &arrayH)) {
        ALOGE("%s: Couldn't get pre correction active array size", __FUNCTION__);
        return;
    }
     if (!SessionConfigurationUtils::getArrayWidthAndHeight(deviceInfo,
            ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, &activeW, &activeH)) {
        ALOGE("%s: Couldn't get active array size", __FUNCTION__);
        return;
    }

    bool isUltraHighResolutionSensor =
            camera3::SessionConfigurationUtils::isUltraHighResolutionSensor(*deviceInfo);
    if (isUltraHighResolutionSensor) {
        if (!SessionConfigurationUtils::getArrayWidthAndHeight(deviceInfo,
                ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE_MAXIMUM_RESOLUTION,
                &arrayMaximumResolutionW, &arrayMaximumResolutionH)) {
            ALOGE("%s: Couldn't get maximum resolution pre correction active array size",
                    __FUNCTION__);
            return;
        }
         if (!SessionConfigurationUtils::getArrayWidthAndHeight(deviceInfo,
                ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE_MAXIMUM_RESOLUTION,
                &activeMaximumResolutionW, &activeMaximumResolutionH)) {
            ALOGE("%s: Couldn't get maximum resolution pre correction active array size",
                    __FUNCTION__);
            return;
        }
    }

    if (usePrecorrectArray) {
        mArrayWidth = arrayW;
        mArrayHeight = arrayH;
        mArrayWidthMaximumResolution = arrayMaximumResolutionW;
        mArrayHeightMaximumResolution = arrayMaximumResolutionH;
    } else {
        mArrayWidth = activeW;
        mArrayHeight = activeH;
        mArrayWidthMaximumResolution = activeMaximumResolutionW;
        mArrayHeightMaximumResolution = activeMaximumResolutionH;
    }
    mHalSupportsZoomRatio = supportNativeZoomRatio;

    ALOGV("%s: array size: %d x %d, full res array size: %d x %d,  mHalSupportsZoomRatio %d",
            __FUNCTION__, mArrayWidth, mArrayHeight, mArrayWidthMaximumResolution,
            mArrayHeightMaximumResolution, mHalSupportsZoomRatio);
    mIsValid = true;
}

status_t ZoomRatioMapper::getArrayDimensionsToBeUsed(const CameraMetadata *settings,
        int32_t *arrayWidth, int32_t *arrayHeight) {
    if (settings == nullptr || arrayWidth == nullptr || arrayHeight == nullptr) {
        return BAD_VALUE;
    }
    // First we get the sensorPixelMode from the settings metadata.
    int32_t sensorPixelMode = ANDROID_SENSOR_PIXEL_MODE_DEFAULT;
    camera_metadata_ro_entry sensorPixelModeEntry = settings->find(ANDROID_SENSOR_PIXEL_MODE);
    if (sensorPixelModeEntry.count != 0) {
        sensorPixelMode = sensorPixelModeEntry.data.u8[0];
        if (sensorPixelMode != ANDROID_SENSOR_PIXEL_MODE_DEFAULT &&
            sensorPixelMode != ANDROID_SENSOR_PIXEL_MODE_MAXIMUM_RESOLUTION) {
            ALOGE("%s: Request sensor pixel mode is not one of the valid values %d",
                      __FUNCTION__, sensorPixelMode);
            return BAD_VALUE;
        }
    }
    if (sensorPixelMode == ANDROID_SENSOR_PIXEL_MODE_DEFAULT) {
        *arrayWidth = mArrayWidth;
        *arrayHeight = mArrayHeight;
    } else {
        *arrayWidth = mArrayWidthMaximumResolution;
        *arrayHeight = mArrayHeightMaximumResolution;
    }
    return OK;
}

status_t ZoomRatioMapper::updateCaptureRequest(CameraMetadata* request) {
    if (!mIsValid) return INVALID_OPERATION;

    status_t res = OK;
    bool zoomRatioIs1 = true;
    camera_metadata_entry_t entry;
    int arrayHeight, arrayWidth = 0;
    res = getArrayDimensionsToBeUsed(request, &arrayWidth, &arrayHeight);
    if (res != OK) {
        return res;
    }
    entry = request->find(ANDROID_CONTROL_ZOOM_RATIO);
    if (entry.count == 1 && entry.data.f[0] != 1.0f) {
        zoomRatioIs1 = false;

        // If cropRegion is windowboxing, override it with activeArray
        camera_metadata_entry_t cropRegionEntry = request->find(ANDROID_SCALER_CROP_REGION);
        if (cropRegionEntry.count == 4) {
            int cropWidth = cropRegionEntry.data.i32[2];
            int cropHeight = cropRegionEntry.data.i32[3];
            if (cropWidth < arrayWidth && cropHeight < arrayHeight) {
                cropRegionEntry.data.i32[0] = 0;
                cropRegionEntry.data.i32[1] = 0;
                cropRegionEntry.data.i32[2] = arrayWidth;
                cropRegionEntry.data.i32[3] = arrayHeight;
            }
        }
    }

    if (mHalSupportsZoomRatio && zoomRatioIs1) {
        res = separateZoomFromCropLocked(request, false/*isResult*/, arrayWidth, arrayHeight);
    } else if (!mHalSupportsZoomRatio && !zoomRatioIs1) {
        res = combineZoomAndCropLocked(request, false/*isResult*/, arrayWidth, arrayHeight);
    }

    // If CONTROL_ZOOM_RATIO is in request, but HAL doesn't support
    // CONTROL_ZOOM_RATIO, remove it from the request.
    if (!mHalSupportsZoomRatio && entry.count == 1) {
        request->erase(ANDROID_CONTROL_ZOOM_RATIO);
    }

    return res;
}

status_t ZoomRatioMapper::updateCaptureResult(CameraMetadata* result, bool requestedZoomRatioIs1) {
    if (!mIsValid) return INVALID_OPERATION;

    status_t res = OK;

    int arrayHeight, arrayWidth = 0;
    res = getArrayDimensionsToBeUsed(result, &arrayWidth, &arrayHeight);
    if (res != OK) {
        return res;
    }
    if (mHalSupportsZoomRatio && requestedZoomRatioIs1) {
        res = combineZoomAndCropLocked(result, true/*isResult*/, arrayWidth, arrayHeight);
    } else if (!mHalSupportsZoomRatio && !requestedZoomRatioIs1) {
        res = separateZoomFromCropLocked(result, true/*isResult*/, arrayWidth, arrayHeight);
    } else {
        camera_metadata_entry_t entry = result->find(ANDROID_CONTROL_ZOOM_RATIO);
        if (entry.count == 0) {
            float zoomRatio1x = 1.0f;
            result->update(ANDROID_CONTROL_ZOOM_RATIO, &zoomRatio1x, 1);
        }
    }

    return res;
}

status_t ZoomRatioMapper::deriveZoomRatio(const CameraMetadata* metadata, float *zoomRatioRet,
        int arrayWidth, int arrayHeight) {
    if (metadata == nullptr || zoomRatioRet == nullptr) {
        return BAD_VALUE;
    }
    float zoomRatio = 1.0;

    camera_metadata_ro_entry_t entry;
    entry = metadata->find(ANDROID_SCALER_CROP_REGION);
    if (entry.count != 4) {
        *zoomRatioRet = 1;
        return OK;
    }
    // Center of the preCorrection/active size
    float arrayCenterX = arrayWidth / 2.0;
    float arrayCenterY = arrayHeight / 2.0;

    // Re-map crop region to coordinate system centered to (arrayCenterX,
    // arrayCenterY).
    float cropRegionLeft = arrayCenterX - entry.data.i32[0] ;
    float cropRegionTop = arrayCenterY - entry.data.i32[1];
    float cropRegionRight = entry.data.i32[0] + entry.data.i32[2] - arrayCenterX;
    float cropRegionBottom = entry.data.i32[1] + entry.data.i32[3] - arrayCenterY;

    // Calculate the scaling factor for left, top, bottom, right
    float zoomRatioLeft = std::max(arrayWidth / (2 * cropRegionLeft), 1.0f);
    float zoomRatioTop = std::max(arrayHeight / (2 * cropRegionTop), 1.0f);
    float zoomRatioRight = std::max(arrayWidth / (2 * cropRegionRight), 1.0f);
    float zoomRatioBottom = std::max(arrayHeight / (2 * cropRegionBottom), 1.0f);

    // Use minimum scaling factor to handle letterboxing or pillarboxing
    zoomRatio = std::min(std::min(zoomRatioLeft, zoomRatioRight),
            std::min(zoomRatioTop, zoomRatioBottom));

    ALOGV("%s: derived zoomRatio is %f", __FUNCTION__, zoomRatio);
    *zoomRatioRet = zoomRatio;
    return OK;
}

status_t ZoomRatioMapper::separateZoomFromCropLocked(CameraMetadata* metadata, bool isResult,
        int arrayWidth, int arrayHeight) {
    float zoomRatio = 1.0;
    status_t res = deriveZoomRatio(metadata, &zoomRatio, arrayWidth, arrayHeight);

    if (res != OK) {
        ALOGE("%s: Failed to derive zoom ratio: %s(%d)",
                __FUNCTION__, strerror(-res), res);
        return res;
    }

    // Update zoomRatio metadata tag
    res = metadata->update(ANDROID_CONTROL_ZOOM_RATIO, &zoomRatio, 1);
    if (res != OK) {
        ALOGE("%s: Failed to update ANDROID_CONTROL_ZOOM_RATIO: %s(%d)",
                __FUNCTION__, strerror(-res), res);
        return res;
    }

    // Scale regions using zoomRatio
    camera_metadata_entry_t entry;
    for (auto region : kMeteringRegionsToCorrect) {
        entry = metadata->find(region);
        for (size_t j = 0; j < entry.count; j += 5) {
            int32_t weight = entry.data.i32[j + 4];
            if (weight == 0) {
                continue;
            }
            // Top left (inclusive)
            scaleCoordinates(entry.data.i32 + j, 1, zoomRatio, true /*clamp*/, arrayWidth,
                    arrayHeight);
            // Bottom right (exclusive): Use adjacent inclusive pixel to
            // calculate.
            entry.data.i32[j+2] -= 1;
            entry.data.i32[j+3] -= 1;
            scaleCoordinates(entry.data.i32 + j + 2, 1, zoomRatio, true /*clamp*/, arrayWidth,
                    arrayHeight);
            entry.data.i32[j+2] += 1;
            entry.data.i32[j+3] += 1;
        }
    }

    for (auto rect : kRectsToCorrect) {
        entry = metadata->find(rect);
        scaleRects(entry.data.i32, entry.count / 4, zoomRatio, arrayWidth, arrayHeight);
    }

    if (isResult) {
        for (auto pts : kResultPointsToCorrectNoClamp) {
            entry = metadata->find(pts);
            scaleCoordinates(entry.data.i32, entry.count / 2, zoomRatio, false /*clamp*/,
                    arrayWidth, arrayHeight);
        }
    }

    return OK;
}

status_t ZoomRatioMapper::combineZoomAndCropLocked(CameraMetadata* metadata, bool isResult,
        int arrayWidth, int arrayHeight) {
    float zoomRatio = 1.0f;
    camera_metadata_entry_t entry;
    entry = metadata->find(ANDROID_CONTROL_ZOOM_RATIO);
    if (entry.count == 1) {
        zoomRatio = entry.data.f[0];
    }

    // Unscale regions with zoomRatio
    for (auto region : kMeteringRegionsToCorrect) {
        entry = metadata->find(region);
        for (size_t j = 0; j < entry.count; j += 5) {
            int32_t weight = entry.data.i32[j + 4];
            if (weight == 0) {
                continue;
            }
            // Top-left (inclusive)
            scaleCoordinates(entry.data.i32 + j, 1, 1.0 / zoomRatio, true /*clamp*/, arrayWidth,
                    arrayHeight);
            // Bottom-right (exclusive): Use adjacent inclusive pixel to
            // calculate.
            entry.data.i32[j+2] -= 1;
            entry.data.i32[j+3] -= 1;
            scaleCoordinates(entry.data.i32 + j + 2, 1, 1.0 / zoomRatio, true /*clamp*/, arrayWidth,
                    arrayHeight);
            entry.data.i32[j+2] += 1;
            entry.data.i32[j+3] += 1;
        }
    }
    for (auto rect : kRectsToCorrect) {
        entry = metadata->find(rect);
        scaleRects(entry.data.i32, entry.count / 4, 1.0 / zoomRatio, arrayWidth, arrayHeight);
    }
    if (isResult) {
        for (auto pts : kResultPointsToCorrectNoClamp) {
            entry = metadata->find(pts);
            scaleCoordinates(entry.data.i32, entry.count / 2, 1.0 / zoomRatio, false /*clamp*/,
                    arrayWidth, arrayHeight);
        }
    }

    zoomRatio = 1.0;
    status_t res = metadata->update(ANDROID_CONTROL_ZOOM_RATIO, &zoomRatio, 1);
    if (res != OK) {
        return res;
    }

    return OK;
}

void ZoomRatioMapper::scaleCoordinates(int32_t* coordPairs, int coordCount,
        float scaleRatio, bool clamp, int32_t arrayWidth, int32_t arrayHeight) {
    // A pixel's coordinate is represented by the position of its top-left corner.
    // To avoid the rounding error, we use the coordinate for the center of the
    // pixel instead:
    // 1. First shift the coordinate system half pixel both horizontally and
    // vertically, so that [x, y] is the center of the pixel, not the top-left corner.
    // 2. Do zoom operation to scale the coordinate relative to the center of
    // the active array (shifted by 0.5 pixel as well).
    // 3. Shift the coordinate system back by directly using the pixel center
    // coordinate.
    for (int i = 0; i < coordCount * 2; i += 2) {
        float x = coordPairs[i];
        float y = coordPairs[i + 1];
        float xCentered = x - (arrayWidth - 2) / 2;
        float yCentered = y - (arrayHeight - 2) / 2;
        float scaledX = xCentered * scaleRatio;
        float scaledY = yCentered * scaleRatio;
        scaledX += (arrayWidth - 2) / 2;
        scaledY += (arrayHeight - 2) / 2;
        coordPairs[i] = static_cast<int32_t>(std::round(scaledX));
        coordPairs[i+1] = static_cast<int32_t>(std::round(scaledY));
        // Clamp to within activeArray/preCorrectionActiveArray
        if (clamp) {
            int32_t right = arrayWidth - 1;
            int32_t bottom = arrayHeight - 1;
            coordPairs[i] =
                    std::min(right, std::max(0, coordPairs[i]));
            coordPairs[i+1] =
                    std::min(bottom, std::max(0, coordPairs[i+1]));
        }
        ALOGV("%s: coordinates: %d, %d", __FUNCTION__, coordPairs[i], coordPairs[i+1]);
    }
}

void ZoomRatioMapper::scaleRects(int32_t* rects, int rectCount,
        float scaleRatio, int32_t arrayWidth, int32_t arrayHeight) {
    for (int i = 0; i < rectCount * 4; i += 4) {
        // Map from (l, t, width, height) to (l, t, l+width-1, t+height-1),
        // where both top-left and bottom-right are inclusive.
        int32_t coords[4] = {
            rects[i],
            rects[i + 1],
            rects[i] + rects[i + 2] - 1,
            rects[i + 1] + rects[i + 3] - 1
        };

        // top-left
        scaleCoordinates(coords, 1, scaleRatio, true /*clamp*/, arrayWidth, arrayHeight);
        // bottom-right
        scaleCoordinates(coords+2, 1, scaleRatio, true /*clamp*/, arrayWidth, arrayHeight);

        // Map back to (l, t, width, height)
        rects[i] = coords[0];
        rects[i + 1] = coords[1];
        rects[i + 2] = coords[2] - coords[0] + 1;
        rects[i + 3] = coords[3] - coords[1] + 1;
    }
}

} // namespace camera3

} // namespace android
