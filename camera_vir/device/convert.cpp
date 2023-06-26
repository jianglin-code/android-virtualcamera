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

#define LOG_TAG "android.hardware.camera.device@3.4-convert-impl"
#include <log/log.h>

#include <cstring>
#include "include/convert.h"

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace V3_4 {
namespace implementation {

using ::android::hardware::graphics::common::V1_0::Dataspace;
using ::android::hardware::graphics::common::V1_0::PixelFormat;
using ::android::hardware::camera::device::V3_2::BufferUsageFlags;

void convertToHidl(const Camera3Stream* src, HalStream* dst) {
    V3_3::implementation::convertToHidl(src, &dst->v3_3);
    dst->physicalCameraId = src->physical_camera_id;
}

void convertToHidl(const camera3_stream_configuration_t& src, HalStreamConfiguration* dst) {
    dst->streams.resize(src.num_streams);
    for (uint32_t i = 0; i < src.num_streams; i++) {
        convertToHidl(static_cast<Camera3Stream*>(src.streams[i]), &dst->streams[i]);
    }
    return;
}

void convertFromHidl(const Stream &src, Camera3Stream* dst) {
    V3_2::implementation::convertFromHidl(src.v3_2, dst);
    // Initialize physical_camera_id
    dst->physical_camera_id = nullptr;
    return;
}

}  // namespace implementation
}  // namespace V3_4
}  // namespace device
}  // namespace camera
}  // namespace hardware
}  // namespace android
