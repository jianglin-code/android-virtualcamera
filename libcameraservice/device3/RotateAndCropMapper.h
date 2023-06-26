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

#ifndef ANDROID_SERVERS_ROTATEANDCROPMAPPER_H
#define ANDROID_SERVERS_ROTATEANDCROPMAPPER_H

#include <utils/Errors.h>
#include <array>
#include <mutex>

#include "camera/CameraMetadata.h"
#include "device3/CoordinateMapper.h"

namespace android {

namespace camera3 {

/**
 * Utilities to transform between unrotated and rotated-and-cropped coordinate systems
 * for cameras that support SCALER_ROTATE_AND_CROP controls in AUTO mode.
 */
class RotateAndCropMapper : public CoordinateMapper {
  public:
    static bool isNeeded(const CameraMetadata* deviceInfo);

    RotateAndCropMapper(const CameraMetadata* deviceInfo);

    void initRemappedKeys() override;

    /**
     * Adjust capture request assuming rotate and crop AUTO is enabled
     */
    status_t updateCaptureRequest(CameraMetadata *request);

    /**
     * Adjust capture result assuming rotate and crop AUTO is enabled
     */
    status_t updateCaptureResult(CameraMetadata *result);

  private:
    // Transform count's worth of x,y points passed in with 2x2 matrix + translate with transform
    // origin (cx,cy)
    void transformPoints(int32_t* pts, size_t count, float transformMat[4],
            float xShift, float yShift, float cx, float cy);
    // Take two corners of a rect as (x1,y1,x2,y2) and swap x and y components
    // if needed so that x1 < x2, y1 < y2.
    void swapRectToMinFirst(int32_t* rect);

    int32_t mArrayWidth, mArrayHeight;
    float mArrayAspect, mRotateAspect;
}; // class RotateAndCroMapper

} // namespace camera3

} // namespace android

#endif
