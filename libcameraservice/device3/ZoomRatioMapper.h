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

#ifndef ANDROID_SERVERS_ZOOMRATIOMAPPER_H
#define ANDROID_SERVERS_ZOOMRATIOMAPPER_H

#include <utils/Errors.h>
#include <array>

#include "camera/CameraMetadata.h"
#include "device3/CoordinateMapper.h"

namespace android {

namespace camera3 {

/**
 * Utilities to convert between the new zoomRatio and existing cropRegion
 * metadata tags. Note that this class does conversions in 2 scenarios:
 * - HAL supports zoomRatio and the application uses cropRegion, or
 * - HAL doesn't support zoomRatio, but the application uses zoomRatio
 */
class ZoomRatioMapper : public CoordinateMapper {
  public:
    ZoomRatioMapper() = default;
    ZoomRatioMapper(const CameraMetadata *deviceInfo,
            bool supportNativeZoomRatio, bool usePrecorrectArray);
    ZoomRatioMapper(const ZoomRatioMapper& other) :
            mHalSupportsZoomRatio(other.mHalSupportsZoomRatio),
            mArrayWidth(other.mArrayWidth), mArrayHeight(other.mArrayHeight),
            mIsValid(other.mIsValid) { initRemappedKeys(); }

    void initRemappedKeys() override;

    /**
     * Initialize request template with valid zoomRatio if necessary.
     */
    static status_t initZoomRatioInTemplate(CameraMetadata *request);

    /**
     * Override zoomRatio related tags in the static metadata.
     */
    static status_t overrideZoomRatioTags(
            CameraMetadata* deviceInfo, bool* supportNativeZoomRatio);

    /**
     * Update capture request to handle both cropRegion and zoomRatio.
     */
    status_t updateCaptureRequest(CameraMetadata *request);

    /**
     * Update capture result to handle both cropRegion and zoomRatio.
     */
    status_t updateCaptureResult(CameraMetadata *request, bool requestedZoomRatioIs1);

  public: // Visible for testing. Do not use concurently.
    void scaleCoordinates(int32_t* coordPairs, int coordCount,
            float scaleRatio, bool clamp, int32_t arrayWidth, int32_t arrayHeight);

    bool isValid() { return mIsValid; }
  private:
    // const after construction
    bool mHalSupportsZoomRatio;

    // active array / pre-correction array dimension for default and maximum
    // resolution modes.
    int32_t mArrayWidth, mArrayHeight;
    int32_t mArrayWidthMaximumResolution, mArrayHeightMaximumResolution;

    bool mIsValid = false;

    status_t deriveZoomRatio(const CameraMetadata* metadata, float *zoomRatio, int arrayWidth,
            int arrayHeight);
    void scaleRects(int32_t* rects, int rectCount, float scaleRatio, int32_t arrayWidth,
            int32_t arrayHeight);

    status_t separateZoomFromCropLocked(CameraMetadata* metadata, bool isResult, int arrayWidth,
            int arrayHeight);
    status_t combineZoomAndCropLocked(CameraMetadata* metadata, bool isResult, int arrayWidth,
            int arrayHeight);
    status_t getArrayDimensionsToBeUsed(const CameraMetadata *settings, int32_t *arrayWidth,
            int32_t *arrayHeight);
};

} // namespace camera3

} // namespace android

#endif
