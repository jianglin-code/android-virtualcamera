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

#define LOG_TAG "Camera3-RotCropMapper"
#define ATRACE_TAG ATRACE_TAG_CAMERA
//#define LOG_NDEBUG 0

#include <algorithm>
#include <cmath>

#include "device3/RotateAndCropMapper.h"

namespace android {

namespace camera3 {

void RotateAndCropMapper::initRemappedKeys() {
    mRemappedKeys.insert(
            kMeteringRegionsToCorrect.begin(),
            kMeteringRegionsToCorrect.end());
    mRemappedKeys.insert(
            kResultPointsToCorrectNoClamp.begin(),
            kResultPointsToCorrectNoClamp.end());

    mRemappedKeys.insert(ANDROID_SCALER_ROTATE_AND_CROP);
    mRemappedKeys.insert(ANDROID_SCALER_CROP_REGION);
}

bool RotateAndCropMapper::isNeeded(const CameraMetadata* deviceInfo) {
    auto entry = deviceInfo->find(ANDROID_SCALER_AVAILABLE_ROTATE_AND_CROP_MODES);
    for (size_t i = 0; i < entry.count; i++) {
        if (entry.data.u8[i] == ANDROID_SCALER_ROTATE_AND_CROP_AUTO) return true;
    }
    return false;
}

RotateAndCropMapper::RotateAndCropMapper(const CameraMetadata* deviceInfo) {
    initRemappedKeys();

    auto entry = deviceInfo->find(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE);
    if (entry.count != 4) return;

    mArrayWidth = entry.data.i32[2];
    mArrayHeight = entry.data.i32[3];
    mArrayAspect = static_cast<float>(mArrayWidth) / mArrayHeight;
    mRotateAspect = 1.f/mArrayAspect;
}

/**
 * Adjust capture request when rotate and crop AUTO is enabled
 */
status_t RotateAndCropMapper::updateCaptureRequest(CameraMetadata *request) {
    auto entry = request->find(ANDROID_SCALER_ROTATE_AND_CROP);
    if (entry.count == 0) return OK;
    uint8_t rotateMode = entry.data.u8[0];
    if (rotateMode == ANDROID_SCALER_ROTATE_AND_CROP_NONE) return OK;

    int32_t cx = 0;
    int32_t cy = 0;
    int32_t cw = mArrayWidth;
    int32_t ch = mArrayHeight;
    entry = request->find(ANDROID_SCALER_CROP_REGION);
    if (entry.count == 4) {
        cx = entry.data.i32[0];
        cy = entry.data.i32[1];
        cw = entry.data.i32[2];
        ch = entry.data.i32[3];
    }

    // User inputs are relative to the rotated-and-cropped view, so convert back
    // to active array coordinates. To be more specific, the application is
    // calculating coordinates based on the crop rectangle and the active array,
    // even though the view the user sees is the cropped-and-rotated one. So we
    // need to adjust the coordinates so that a point that would be on the
    // top-left corner of the crop region is mapped to the top-left corner of
    // the rotated-and-cropped fov within the crop region, and the same for the
    // bottom-right corner.
    //
    // Since the zoom ratio control scales everything uniformly (so an app does
    // not need to adjust anything if it wants to put a metering region on the
    // top-left quadrant of the preview FOV, when changing zoomRatio), it does
    // not need to be factored into this calculation at all.
    //
    //   ->+x                       active array  aw
    //  |+--------------------------------------------------------------------+
    //  v|                                                                    |
    // +y|         a         1       cw        2           b                  |
    //   |          +=========*HHHHHHHHHHHHHHH*===========+                   |
    //   |          I         H      rw       H           I                   |
    //   |          I         H               H           I                   |
    //   |          I         H               H           I                   |
    //ah |       ch I         H rh            H           I crop region       |
    //   |          I         H               H           I                   |
    //   |          I         H               H           I                   |
    //   |          I         H rotate region H           I                   |
    //   |          +=========*HHHHHHHHHHHHHHH*===========+                   |
    //   |         d         4                 3           c                  |
    //   |                                                                    |
    //   +--------------------------------------------------------------------+
    //
    // aw , ah = active array width,height
    // cw , ch = crop region width,height
    // rw , rh = rotated-and-cropped region width,height
    // aw / ah = array aspect = rh / rw = 1 / rotated aspect
    // Coordinate mappings:
    //    ROTATE_AND_CROP_90: point a -> point 2
    //                        point c -> point 4 = +x -> +y, +y -> -x
    //    ROTATE_AND_CROP_180: point a -> point c
    //                         point c -> point a = +x -> -x, +y -> -y
    //    ROTATE_AND_CROP_270: point a -> point 4
    //                         point c -> point 2 = +x -> -y, +y -> +x

    float cropAspect = static_cast<float>(cw) / ch;
    float transformMat[4] = {0, 0,
                             0, 0};
    float xShift = 0;
    float yShift = 0;

    if (rotateMode == ANDROID_SCALER_ROTATE_AND_CROP_180) {
        transformMat[0] = -1;
        transformMat[3] = -1;
        xShift = cw;
        yShift = ch;
    } else {
        float rw = cropAspect > mRotateAspect ?
                   ch * mRotateAspect : // pillarbox, not full width
                   cw;                  // letterbox or 1:1, full width
        float rh = cropAspect >= mRotateAspect ?
                   ch :                 // pillarbox or 1:1, full height
                   cw / mRotateAspect;  // letterbox, not full height
        switch (rotateMode) {
            case ANDROID_SCALER_ROTATE_AND_CROP_90:
                transformMat[1] = -rw / ch; // +y -> -x
                transformMat[2] =  rh / cw; // +x -> +y
                xShift = (cw + rw) / 2; // left edge of crop to right edge of rotated
                yShift = (ch - rh) / 2; // top edge of crop to top edge of rotated
                break;
            case ANDROID_SCALER_ROTATE_AND_CROP_270:
                transformMat[1] =  rw / ch; // +y -> +x
                transformMat[2] = -rh / cw; // +x -> -y
                xShift = (cw - rw) / 2; // left edge of crop to left edge of rotated
                yShift = (ch + rh) / 2; // top edge of crop to bottom edge of rotated
                break;
            default:
                ALOGE("%s: Unexpected rotate mode: %d", __FUNCTION__, rotateMode);
                return BAD_VALUE;
        }
    }

    for (auto regionTag : kMeteringRegionsToCorrect) {
        entry = request->find(regionTag);
        for (size_t i = 0; i < entry.count; i += 5) {
            int32_t weight = entry.data.i32[i + 4];
            if (weight == 0) {
                continue;
            }
            transformPoints(entry.data.i32 + i, 2, transformMat, xShift, yShift, cx, cy);
            swapRectToMinFirst(entry.data.i32 + i);
        }
    }

    return OK;
}

/**
 * Adjust capture result when rotate and crop AUTO is enabled
 */
status_t RotateAndCropMapper::updateCaptureResult(CameraMetadata *result) {
    auto entry = result->find(ANDROID_SCALER_ROTATE_AND_CROP);
    if (entry.count == 0) return OK;
    uint8_t rotateMode = entry.data.u8[0];
    if (rotateMode == ANDROID_SCALER_ROTATE_AND_CROP_NONE) return OK;

    int32_t cx = 0;
    int32_t cy = 0;
    int32_t cw = mArrayWidth;
    int32_t ch = mArrayHeight;
    entry = result->find(ANDROID_SCALER_CROP_REGION);
    if (entry.count == 4) {
        cx = entry.data.i32[0];
        cy = entry.data.i32[1];
        cw = entry.data.i32[2];
        ch = entry.data.i32[3];
    }

    // HAL inputs are relative to the full active array, so convert back to
    // rotated-and-cropped coordinates for apps. To be more specific, the
    // application is calculating coordinates based on the crop rectangle and
    // the active array, even though the view the user sees is the
    // cropped-and-rotated one. So we need to adjust the coordinates so that a
    // point that would be on the top-left corner of the rotate-and-cropped
    // region is mapped to the top-left corner of the crop region, and the same
    // for the bottom-right corner.
    //
    // Since the zoom ratio control scales everything uniformly (so an app does
    // not need to adjust anything if it wants to put a metering region on the
    // top-left quadrant of the preview FOV, when changing zoomRatio), it does
    // not need to be factored into this calculation at all.
    //
    // Also note that round-tripping between original request and final result
    // fields can't be perfect, since the intermediate values have to be
    // integers on a smaller range than the original crop region range. That
    // means that multiple input values map to a single output value in
    // adjusting a request, so when adjusting a result, the original answer may
    // not be obtainable.  Given that aspect ratios are rarely > 16/9, the
    // round-trip values should generally only be off by 1 at most.
    //
    //   ->+x                       active array  aw
    //  |+--------------------------------------------------------------------+
    //  v|                                                                    |
    // +y|         a         1       cw        2           b                  |
    //   |          +=========*HHHHHHHHHHHHHHH*===========+                   |
    //   |          I         H      rw       H           I                   |
    //   |          I         H               H           I                   |
    //   |          I         H               H           I                   |
    //ah |       ch I         H rh            H           I crop region       |
    //   |          I         H               H           I                   |
    //   |          I         H               H           I                   |
    //   |          I         H rotate region H           I                   |
    //   |          +=========*HHHHHHHHHHHHHHH*===========+                   |
    //   |         d         4                 3           c                  |
    //   |                                                                    |
    //   +--------------------------------------------------------------------+
    //
    // aw , ah = active array width,height
    // cw , ch = crop region width,height
    // rw , rh = rotated-and-cropped region width,height
    // aw / ah = array aspect = rh / rw = 1 / rotated aspect
    // Coordinate mappings:
    //    ROTATE_AND_CROP_90: point 2 -> point a
    //                        point 4 -> point c = +x -> -y, +y -> +x
    //    ROTATE_AND_CROP_180: point c -> point a
    //                         point a -> point c = +x -> -x, +y -> -y
    //    ROTATE_AND_CROP_270: point 4 -> point a
    //                         point 2 -> point c = +x -> +y, +y -> -x

    float cropAspect = static_cast<float>(cw) / ch;
    float transformMat[4] = {0, 0,
                             0, 0};
    float xShift = 0;
    float yShift = 0;
    float rx = 0; // top-left corner of rotated region
    float ry = 0;
    if (rotateMode == ANDROID_SCALER_ROTATE_AND_CROP_180) {
        transformMat[0] = -1;
        transformMat[3] = -1;
        xShift = cw;
        yShift = ch;
        rx = cx;
        ry = cy;
    } else {
        float rw = cropAspect > mRotateAspect ?
                   ch * mRotateAspect : // pillarbox, not full width
                   cw;                  // letterbox or 1:1, full width
        float rh = cropAspect >= mRotateAspect ?
                   ch :                 // pillarbox or 1:1, full height
                   cw / mRotateAspect;  // letterbox, not full height
        rx = cx + (cw - rw) / 2;
        ry = cy + (ch - rh) / 2;
        switch (rotateMode) {
            case ANDROID_SCALER_ROTATE_AND_CROP_90:
                transformMat[1] =  ch / rw; // +y -> +x
                transformMat[2] = -cw / rh; // +x -> -y
                xShift = -(cw - rw) / 2; // left edge of rotated to left edge of cropped
                yShift = ry - cy + ch;   // top edge of rotated to bottom edge of cropped
                break;
            case ANDROID_SCALER_ROTATE_AND_CROP_270:
                transformMat[1] = -ch / rw; // +y -> -x
                transformMat[2] =  cw / rh; // +x -> +y
                xShift = (cw + rw) / 2; // left edge of rotated to left edge of cropped
                yShift = (ch - rh) / 2; // top edge of rotated to bottom edge of cropped
                break;
            default:
                ALOGE("%s: Unexpected rotate mode: %d", __FUNCTION__, rotateMode);
                return BAD_VALUE;
        }
    }

    for (auto regionTag : kMeteringRegionsToCorrect) {
        entry = result->find(regionTag);
        for (size_t i = 0; i < entry.count; i += 5) {
            int32_t weight = entry.data.i32[i + 4];
            if (weight == 0) {
                continue;
            }
            transformPoints(entry.data.i32 + i, 2, transformMat, xShift, yShift, rx, ry);
            swapRectToMinFirst(entry.data.i32 + i);
        }
    }

    for (auto pointsTag: kResultPointsToCorrectNoClamp) {
        entry = result->find(pointsTag);
        transformPoints(entry.data.i32, entry.count / 2, transformMat, xShift, yShift, rx, ry);
        if (pointsTag == ANDROID_STATISTICS_FACE_RECTANGLES) {
            for (size_t i = 0; i < entry.count; i += 4) {
                swapRectToMinFirst(entry.data.i32 + i);
            }
        }
    }

    return OK;
}

void RotateAndCropMapper::transformPoints(int32_t* pts, size_t count, float transformMat[4],
        float xShift, float yShift, float ox, float oy) {
    for (size_t i = 0; i < count * 2; i += 2) {
        float x0 = pts[i] - ox;
        float y0 = pts[i + 1] - oy;
        int32_t nx = std::round(transformMat[0] * x0 + transformMat[1] * y0 + xShift + ox);
        int32_t ny = std::round(transformMat[2] * x0 + transformMat[3] * y0 + yShift + oy);

        pts[i] = std::min(std::max(nx, 0), mArrayWidth);
        pts[i + 1] = std::min(std::max(ny, 0), mArrayHeight);
    }
}

void RotateAndCropMapper::swapRectToMinFirst(int32_t* rect) {
    if (rect[0] > rect[2]) {
        auto tmp = rect[0];
        rect[0] = rect[2];
        rect[2] = tmp;
    }
    if (rect[1] > rect[3]) {
        auto tmp = rect[1];
        rect[1] = rect[3];
        rect[3] = tmp;
    }
}

} // namespace camera3

} // namespace android
