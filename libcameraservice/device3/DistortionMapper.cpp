/*
 * Copyright (C) 2018 The Android Open Source Project
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

#define LOG_TAG "Camera3-DistMapper"
#define ATRACE_TAG ATRACE_TAG_CAMERA
//#define LOG_NDEBUG 0

#include <algorithm>
#include <cmath>

#include "device3/DistortionMapper.h"
#include "utils/SessionConfigurationUtils.h"

namespace android {

namespace camera3 {


DistortionMapper::DistortionMapper() {
    initRemappedKeys();
}

void DistortionMapper::initRemappedKeys() {
    mRemappedKeys.insert(
            kMeteringRegionsToCorrect.begin(),
            kMeteringRegionsToCorrect.end());
    mRemappedKeys.insert(
            kRectsToCorrect.begin(),
            kRectsToCorrect.end());
    mRemappedKeys.insert(
            kResultPointsToCorrectNoClamp.begin(),
            kResultPointsToCorrectNoClamp.end());
    mRemappedKeys.insert(ANDROID_DISTORTION_CORRECTION_MODE);
}

bool DistortionMapper::isDistortionSupported(const CameraMetadata &deviceInfo) {
    bool isDistortionCorrectionSupported = false;
    camera_metadata_ro_entry_t distortionCorrectionModes =
            deviceInfo.find(ANDROID_DISTORTION_CORRECTION_AVAILABLE_MODES);
    for (size_t i = 0; i < distortionCorrectionModes.count; i++) {
        if (distortionCorrectionModes.data.u8[i] !=
                ANDROID_DISTORTION_CORRECTION_MODE_OFF) {
            isDistortionCorrectionSupported = true;
            break;
        }
    }
    return isDistortionCorrectionSupported;
}

status_t DistortionMapper::setupStaticInfo(const CameraMetadata &deviceInfo) {
    std::lock_guard<std::mutex> lock(mMutex);
    status_t res = setupStaticInfoLocked(deviceInfo, /*maxResolution*/false);
    if (res != OK) {
        return res;
    }

    bool mMaxResolution = SessionConfigurationUtils::isUltraHighResolutionSensor(deviceInfo);
    if (mMaxResolution) {
        res = setupStaticInfoLocked(deviceInfo, /*maxResolution*/true);
    }
    return res;
}

status_t DistortionMapper::setupStaticInfoLocked(const CameraMetadata &deviceInfo,
        bool maxResolution) {
    DistortionMapperInfo *mapperInfo = maxResolution ? &mDistortionMapperInfoMaximumResolution :
            &mDistortionMapperInfo;

    camera_metadata_ro_entry_t array;

    array = deviceInfo.find(
        SessionConfigurationUtils::getAppropriateModeTag(
                ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE, maxResolution));
    if (array.count != 4) return BAD_VALUE;

    float arrayX = static_cast<float>(array.data.i32[0]);
    float arrayY = static_cast<float>(array.data.i32[1]);
    mapperInfo->mArrayWidth = static_cast<float>(array.data.i32[2]);
    mapperInfo->mArrayHeight = static_cast<float>(array.data.i32[3]);

    array = deviceInfo.find(
            SessionConfigurationUtils::getAppropriateModeTag(
                    ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, maxResolution));
    if (array.count != 4) return BAD_VALUE;

    float activeX = static_cast<float>(array.data.i32[0]);
    float activeY = static_cast<float>(array.data.i32[1]);
    mapperInfo->mActiveWidth = static_cast<float>(array.data.i32[2]);
    mapperInfo->mActiveHeight = static_cast<float>(array.data.i32[3]);

    mapperInfo->mArrayDiffX = activeX - arrayX;
    mapperInfo->mArrayDiffY = activeY - arrayY;

    return updateCalibration(deviceInfo, /*isStatic*/ true, maxResolution);
}

static bool doesSettingsHaveMaxResolution(const CameraMetadata *settings) {
    if (settings == nullptr) {
        return false;
    }
    // First we get the sensorPixelMode from the settings metadata.
    camera_metadata_ro_entry sensorPixelModeEntry = settings->find(ANDROID_SENSOR_PIXEL_MODE);
    if (sensorPixelModeEntry.count != 0) {
        return (sensorPixelModeEntry.data.u8[0] == ANDROID_SENSOR_PIXEL_MODE_MAXIMUM_RESOLUTION);
    }
    return false;
}

bool DistortionMapper::calibrationValid() const {
    std::lock_guard<std::mutex> lock(mMutex);
    bool isValid =  mDistortionMapperInfo.mValidMapping;
    if (mMaxResolution) {
        isValid = isValid && mDistortionMapperInfoMaximumResolution.mValidMapping;
    }
    return isValid;
}

status_t DistortionMapper::correctCaptureRequest(CameraMetadata *request) {
    std::lock_guard<std::mutex> lock(mMutex);
    status_t res;

    bool maxResolution = doesSettingsHaveMaxResolution(request);
    DistortionMapperInfo *mapperInfo = maxResolution ? &mDistortionMapperInfoMaximumResolution :
            &mDistortionMapperInfo;

    if (!mapperInfo->mValidMapping) return OK;

    camera_metadata_entry_t e;
    e = request->find(ANDROID_DISTORTION_CORRECTION_MODE);
    if (e.count != 0 && e.data.u8[0] != ANDROID_DISTORTION_CORRECTION_MODE_OFF) {
        for (auto region : kMeteringRegionsToCorrect) {
            e = request->find(region);
            for (size_t j = 0; j < e.count; j += 5) {
                int32_t weight = e.data.i32[j + 4];
                if (weight == 0) {
                    continue;
                }
                res = mapCorrectedToRaw(e.data.i32 + j, 2, mapperInfo, /*clamp*/true);
                if (res != OK) return res;
            }
        }
        for (auto rect : kRectsToCorrect) {
            e = request->find(rect);
            res = mapCorrectedRectToRaw(e.data.i32, e.count / 4, mapperInfo, /*clamp*/true);
            if (res != OK) return res;
        }
    }
    return OK;
}

status_t DistortionMapper::correctCaptureResult(CameraMetadata *result) {
    std::lock_guard<std::mutex> lock(mMutex);

    bool maxResolution = doesSettingsHaveMaxResolution(result);
    DistortionMapperInfo *mapperInfo = maxResolution ? &mDistortionMapperInfoMaximumResolution :
            &mDistortionMapperInfo;
    status_t res;

    if (!mapperInfo->mValidMapping) return OK;

    res = updateCalibration(*result, /*isStatic*/ false, maxResolution);
    if (res != OK) {
        ALOGE("Failure to update lens calibration information");
        return INVALID_OPERATION;
    }

    camera_metadata_entry_t e;
    e = result->find(ANDROID_DISTORTION_CORRECTION_MODE);
    if (e.count != 0 && e.data.u8[0] != ANDROID_DISTORTION_CORRECTION_MODE_OFF) {
        for (auto region : kMeteringRegionsToCorrect) {
            e = result->find(region);
            for (size_t j = 0; j < e.count; j += 5) {
                int32_t weight = e.data.i32[j + 4];
                if (weight == 0) {
                    continue;
                }
                res = mapRawToCorrected(e.data.i32 + j, 2, mapperInfo, /*clamp*/true);
                if (res != OK) return res;
            }
        }
        for (auto rect : kRectsToCorrect) {
            e = result->find(rect);
            res = mapRawRectToCorrected(e.data.i32, e.count / 4, mapperInfo, /*clamp*/true);
            if (res != OK) return res;
        }
        for (auto pts : kResultPointsToCorrectNoClamp) {
            e = result->find(pts);
            res = mapRawToCorrected(e.data.i32, e.count / 2, mapperInfo, /*clamp*/false);
            if (res != OK) return res;
        }
    }

    return OK;
}

// Utility methods; not guarded by mutex

status_t DistortionMapper::updateCalibration(const CameraMetadata &result, bool isStatic,
        bool maxResolution) {
    camera_metadata_ro_entry_t calib, distortion;
    DistortionMapperInfo *mapperInfo =
            maxResolution ? &mDistortionMapperInfoMaximumResolution : &mDistortionMapperInfo;
    // We only need maximum resolution version of LENS_INTRINSIC_CALIBRATION and
    // LENS_DISTORTION since CaptureResults would still use the same key
    // regardless of sensor pixel mode.
    int calibrationKey =
        SessionConfigurationUtils::getAppropriateModeTag(ANDROID_LENS_INTRINSIC_CALIBRATION,
                maxResolution && isStatic);
    int distortionKey =
        SessionConfigurationUtils::getAppropriateModeTag(ANDROID_LENS_DISTORTION,
                maxResolution && isStatic);

    calib = result.find(calibrationKey);
    distortion = result.find(distortionKey);

    if (calib.count != 5) return BAD_VALUE;
    if (distortion.count != 5) return BAD_VALUE;

    // Skip redoing work if no change to calibration fields
    if (mapperInfo->mValidMapping &&
            mapperInfo->mFx == calib.data.f[0] &&
            mapperInfo->mFy == calib.data.f[1] &&
            mapperInfo->mCx == calib.data.f[2] &&
            mapperInfo->mCy == calib.data.f[3] &&
            mapperInfo->mS == calib.data.f[4]) {
        bool noChange = true;
        for (size_t i = 0; i < distortion.count; i++) {
            if (mapperInfo->mK[i] != distortion.data.f[i]) {
                noChange = false;
                break;
            }
        }
        if (noChange) return OK;
    }

    mapperInfo->mFx = calib.data.f[0];
    mapperInfo->mFy = calib.data.f[1];
    mapperInfo->mCx = calib.data.f[2];
    mapperInfo->mCy = calib.data.f[3];
    mapperInfo->mS = calib.data.f[4];

    mapperInfo->mInvFx = 1 / mapperInfo->mFx;
    mapperInfo->mInvFy = 1 / mapperInfo->mFy;

    for (size_t i = 0; i < distortion.count; i++) {
        mapperInfo->mK[i] = distortion.data.f[i];
    }

    mapperInfo->mValidMapping = true;
    // Need to recalculate grid
    mapperInfo->mValidGrids = false;

    return OK;
}

status_t DistortionMapper::mapRawToCorrected(int32_t *coordPairs, int coordCount,
        DistortionMapperInfo *mapperInfo, bool clamp, bool simple) {
    if (!mapperInfo->mValidMapping) return INVALID_OPERATION;

    if (simple) return mapRawToCorrectedSimple(coordPairs, coordCount, mapperInfo, clamp);

    if (!mapperInfo->mValidGrids) {
        status_t res = buildGrids(mapperInfo);
        if (res != OK) return res;
    }

    for (int i = 0; i < coordCount * 2; i += 2) {
        const GridQuad *quad = findEnclosingQuad(coordPairs + i, mapperInfo->mDistortedGrid);
        if (quad == nullptr) {
            ALOGE("Raw to corrected mapping failure: No quad found for (%d, %d)",
                    *(coordPairs + i), *(coordPairs + i + 1));
            return INVALID_OPERATION;
        }
        ALOGV("src xy: %d, %d, enclosing quad: (%f, %f), (%f, %f), (%f, %f), (%f, %f)",
                coordPairs[i], coordPairs[i+1],
                quad->coords[0], quad->coords[1],
                quad->coords[2], quad->coords[3],
                quad->coords[4], quad->coords[5],
                quad->coords[6], quad->coords[7]);

        const GridQuad *corrQuad = quad->src;
        if (corrQuad == nullptr) {
            ALOGE("Raw to corrected mapping failure: No src quad found");
            return INVALID_OPERATION;
        }
        ALOGV("              corr quad: (%f, %f), (%f, %f), (%f, %f), (%f, %f)",
                corrQuad->coords[0], corrQuad->coords[1],
                corrQuad->coords[2], corrQuad->coords[3],
                corrQuad->coords[4], corrQuad->coords[5],
                corrQuad->coords[6], corrQuad->coords[7]);

        float u = calculateUorV(coordPairs + i, *quad, /*calculateU*/ true);
        float v = calculateUorV(coordPairs + i, *quad, /*calculateU*/ false);

        ALOGV("uv: %f, %f", u, v);

        // Interpolate along top edge of corrected quad (which are axis-aligned) for x
        float corrX = corrQuad->coords[0] + u * (corrQuad->coords[2] - corrQuad->coords[0]);
        // Interpolate along left edge of corrected quad (which are axis-aligned) for y
        float corrY = corrQuad->coords[1] + v * (corrQuad->coords[7] - corrQuad->coords[1]);

        // Clamp to within active array
        if (clamp) {
            corrX = std::min(mapperInfo->mActiveWidth - 1, std::max(0.f, corrX));
            corrY = std::min(mapperInfo->mActiveHeight - 1, std::max(0.f, corrY));
        }

        coordPairs[i] = static_cast<int32_t>(std::round(corrX));
        coordPairs[i + 1] = static_cast<int32_t>(std::round(corrY));
    }

    return OK;
}

status_t DistortionMapper::mapRawToCorrectedSimple(int32_t *coordPairs, int coordCount,
       const DistortionMapperInfo *mapperInfo, bool clamp) const {
    if (!mapperInfo->mValidMapping) return INVALID_OPERATION;

    float scaleX = mapperInfo->mActiveWidth / mapperInfo->mArrayWidth;
    float scaleY = mapperInfo->mActiveHeight / mapperInfo->mArrayHeight;
    for (int i = 0; i < coordCount * 2; i += 2) {
        float x = coordPairs[i];
        float y = coordPairs[i + 1];
        float corrX = x * scaleX;
        float corrY = y * scaleY;
        if (clamp) {
            corrX = std::min(mapperInfo->mActiveWidth - 1, std::max(0.f, corrX));
            corrY = std::min(mapperInfo->mActiveHeight - 1, std::max(0.f, corrY));
        }
        coordPairs[i] = static_cast<int32_t>(std::round(corrX));
        coordPairs[i + 1] = static_cast<int32_t>(std::round(corrY));
    }

    return OK;
}

status_t DistortionMapper::mapRawRectToCorrected(int32_t *rects, int rectCount,
       DistortionMapperInfo *mapperInfo, bool clamp, bool simple) {
    if (!mapperInfo->mValidMapping) return INVALID_OPERATION;
    for (int i = 0; i < rectCount * 4; i += 4) {
        // Map from (l, t, width, height) to (l, t, r, b)
        int32_t coords[4] = {
            rects[i],
            rects[i + 1],
            rects[i] + rects[i + 2] - 1,
            rects[i + 1] + rects[i + 3] - 1
        };

        mapRawToCorrected(coords, 2, mapperInfo, clamp, simple);

        // Map back to (l, t, width, height)
        rects[i] = coords[0];
        rects[i + 1] = coords[1];
        rects[i + 2] = coords[2] - coords[0] + 1;
        rects[i + 3] = coords[3] - coords[1] + 1;
    }

    return OK;
}

status_t DistortionMapper::mapCorrectedToRaw(int32_t *coordPairs, int coordCount,
       const DistortionMapperInfo *mapperInfo, bool clamp, bool simple) const {
    return mapCorrectedToRawImpl(coordPairs, coordCount, mapperInfo, clamp, simple);
}

template<typename T>
status_t DistortionMapper::mapCorrectedToRawImpl(T *coordPairs, int coordCount,
       const DistortionMapperInfo *mapperInfo, bool clamp, bool simple) const {
    if (!mapperInfo->mValidMapping) return INVALID_OPERATION;

    if (simple) return mapCorrectedToRawImplSimple(coordPairs, coordCount, mapperInfo, clamp);

    float activeCx = mapperInfo->mCx - mapperInfo->mArrayDiffX;
    float activeCy = mapperInfo->mCy - mapperInfo->mArrayDiffY;
    for (int i = 0; i < coordCount * 2; i += 2) {
        // Move to normalized space from active array space
        float ywi = (coordPairs[i + 1] - activeCy) * mapperInfo->mInvFy;
        float xwi = (coordPairs[i] - activeCx - mapperInfo->mS * ywi) * mapperInfo->mInvFx;
        // Apply distortion model to calculate raw image coordinates
        const std::array<float, 5> &kK = mapperInfo->mK;
        float rSq = xwi * xwi + ywi * ywi;
        float Fr = 1.f + (kK[0] * rSq) + (kK[1] * rSq * rSq) + (kK[2] * rSq * rSq * rSq);
        float xc = xwi * Fr + (kK[3] * 2 * xwi * ywi) + kK[4] * (rSq + 2 * xwi * xwi);
        float yc = ywi * Fr + (kK[4] * 2 * xwi * ywi) + kK[3] * (rSq + 2 * ywi * ywi);
        // Move back to image space
        float xr = mapperInfo->mFx * xc + mapperInfo->mS * yc + mapperInfo->mCx;
        float yr = mapperInfo->mFy * yc + mapperInfo->mCy;
        // Clamp to within pre-correction active array
        if (clamp) {
            xr = std::min(mapperInfo->mArrayWidth - 1, std::max(0.f, xr));
            yr = std::min(mapperInfo->mArrayHeight - 1, std::max(0.f, yr));
        }

        coordPairs[i] = static_cast<T>(std::round(xr));
        coordPairs[i + 1] = static_cast<T>(std::round(yr));
    }
    return OK;
}

template<typename T>
status_t DistortionMapper::mapCorrectedToRawImplSimple(T *coordPairs, int coordCount,
       const DistortionMapperInfo *mapperInfo, bool clamp) const {
    if (!mapperInfo->mValidMapping) return INVALID_OPERATION;

    float scaleX = mapperInfo->mArrayWidth / mapperInfo->mActiveWidth;
    float scaleY = mapperInfo->mArrayHeight / mapperInfo->mActiveHeight;
    for (int i = 0; i < coordCount * 2; i += 2) {
        float x = coordPairs[i];
        float y = coordPairs[i + 1];
        float rawX = x * scaleX;
        float rawY = y * scaleY;
        if (clamp) {
            rawX = std::min(mapperInfo->mArrayWidth - 1, std::max(0.f, rawX));
            rawY = std::min(mapperInfo->mArrayHeight - 1, std::max(0.f, rawY));
        }
        coordPairs[i] = static_cast<T>(std::round(rawX));
        coordPairs[i + 1] = static_cast<T>(std::round(rawY));
    }

    return OK;
}

status_t DistortionMapper::mapCorrectedRectToRaw(int32_t *rects, int rectCount,
       const DistortionMapperInfo *mapperInfo, bool clamp, bool simple) const {
    if (!mapperInfo->mValidMapping) return INVALID_OPERATION;

    for (int i = 0; i < rectCount * 4; i += 4) {
        // Map from (l, t, width, height) to (l, t, r, b)
        int32_t coords[4] = {
            rects[i],
            rects[i + 1],
            rects[i] + rects[i + 2] - 1,
            rects[i + 1] + rects[i + 3] - 1
        };

        mapCorrectedToRaw(coords, 2, mapperInfo, clamp, simple);

        // Map back to (l, t, width, height)
        rects[i] = coords[0];
        rects[i + 1] = coords[1];
        rects[i + 2] = coords[2] - coords[0] + 1;
        rects[i + 3] = coords[3] - coords[1] + 1;
    }

    return OK;
}

status_t DistortionMapper::buildGrids(DistortionMapperInfo *mapperInfo) {
    if (mapperInfo->mCorrectedGrid.size() != kGridSize * kGridSize) {
        mapperInfo->mCorrectedGrid.resize(kGridSize * kGridSize);
        mapperInfo->mDistortedGrid.resize(kGridSize * kGridSize);
    }

    float gridMargin = mapperInfo->mArrayWidth * kGridMargin;
    float gridSpacingX = (mapperInfo->mArrayWidth + 2 * gridMargin) / kGridSize;
    float gridSpacingY = (mapperInfo->mArrayHeight + 2 * gridMargin) / kGridSize;

    size_t index = 0;
    float x = -gridMargin;
    for (size_t i = 0; i < kGridSize; i++, x += gridSpacingX) {
        float y = -gridMargin;
        for (size_t j = 0; j < kGridSize; j++, y += gridSpacingY, index++) {
            mapperInfo->mCorrectedGrid[index].src = nullptr;
            mapperInfo->mCorrectedGrid[index].coords = {
                x, y,
                x + gridSpacingX, y,
                x + gridSpacingX, y + gridSpacingY,
                x, y + gridSpacingY
            };
            mapperInfo->mDistortedGrid[index].src = &(mapperInfo->mCorrectedGrid[index]);
            mapperInfo->mDistortedGrid[index].coords = mapperInfo->mCorrectedGrid[index].coords;
            status_t res = mapCorrectedToRawImpl(mapperInfo->mDistortedGrid[index].coords.data(), 4,
                    mapperInfo, /*clamp*/false, /*simple*/false);
            if (res != OK) return res;
        }
    }

    mapperInfo->mValidGrids = true;
    return OK;
}

const DistortionMapper::GridQuad* DistortionMapper::findEnclosingQuad(
        const int32_t pt[2], const std::vector<GridQuad>& grid) {
    const float x = pt[0];
    const float y = pt[1];

    for (const GridQuad& quad : grid) {
        const float &x1 = quad.coords[0];
        const float &y1 = quad.coords[1];
        const float &x2 = quad.coords[2];
        const float &y2 = quad.coords[3];
        const float &x3 = quad.coords[4];
        const float &y3 = quad.coords[5];
        const float &x4 = quad.coords[6];
        const float &y4 = quad.coords[7];

        // Point-in-quad test:

        // Quad has corners P1-P4; if P is within the quad, then it is on the same side of all the
        // edges (or on top of one of the edges or corners), traversed in a consistent direction.
        // This means that the cross product of edge En = Pn->P(n+1 mod 4) and line Ep = Pn->P must
        // have the same sign (or be zero) for all edges.
        // For clockwise traversal, the sign should be negative or zero for Ep x En, indicating that
        // En is to the left of Ep, or overlapping.
        float s1 = (x - x1) * (y2 - y1) - (y - y1) * (x2 - x1);
        if (s1 > 0) continue;
        float s2 = (x - x2) * (y3 - y2) - (y - y2) * (x3 - x2);
        if (s2 > 0) continue;
        float s3 = (x - x3) * (y4 - y3) - (y - y3) * (x4 - x3);
        if (s3 > 0) continue;
        float s4 = (x - x4) * (y1 - y4) - (y - y4) * (x1 - x4);
        if (s4 > 0) continue;

        return &quad;
    }
    return nullptr;
}

float DistortionMapper::calculateUorV(const int32_t pt[2], const GridQuad& quad, bool calculateU) {
    const float x = pt[0];
    const float y = pt[1];
    const float &x1 = quad.coords[0];
    const float &y1 = quad.coords[1];
    const float &x2 = calculateU ? quad.coords[2] : quad.coords[6];
    const float &y2 = calculateU ? quad.coords[3] : quad.coords[7];
    const float &x3 = quad.coords[4];
    const float &y3 = quad.coords[5];
    const float &x4 = calculateU ? quad.coords[6] : quad.coords[2];
    const float &y4 = calculateU ? quad.coords[7] : quad.coords[3];

    float a = (x1 - x2) * (y1 - y2 + y3 - y4) - (y1 - y2) * (x1 - x2 + x3 - x4);
    float b = (x - x1) * (y1 - y2 + y3 - y4) + (x1 - x2) * (y4 - y1) -
              (y - y1) * (x1 - x2 + x3 - x4) - (y1 - y2) * (x4 - x1);
    float c = (x - x1) * (y4 - y1) - (y - y1) * (x4 - x1);

    if (a == 0) {
        // One solution may happen if edges are parallel
        float u0 = -c / b;
        ALOGV("u0: %.9g, b: %f, c: %f", u0, b, c);
        return u0;
    }

    float det = b * b - 4 * a * c;
    if (det < 0) {
        // Validation check - should not happen if pt is within the quad
        ALOGE("Bad determinant! a: %f, b: %f, c: %f, det: %f", a,b,c,det);
        return -1;
    }

    // Select more numerically stable solution
    float sqdet = b > 0 ? -std::sqrt(det) : std::sqrt(det);

    float u1 = (-b + sqdet) / (2 * a);
    ALOGV("u1: %.9g", u1);
    if (0 - kFloatFuzz < u1 && u1 < 1 + kFloatFuzz) return u1;

    float u2 = c / (a * u1);
    ALOGV("u2: %.9g", u2);
    if (0 - kFloatFuzz < u2 && u2 < 1 + kFloatFuzz) return u2;

    // Last resort, return the smaller-magnitude solution
    return fabs(u1) < fabs(u2) ? u1 : u2;
}

} // namespace camera3

} // namespace android
