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

#include <hidl/AidlCameraServiceListener.h>
#include <hidl/Convert.h>

namespace android {
namespace frameworks {
namespace cameraservice {
namespace service {
namespace V2_0 {
namespace implementation {

using hardware::cameraservice::utils::conversion::convertToHidlCameraDeviceStatus;
typedef frameworks::cameraservice::service::V2_1::ICameraServiceListener HCameraServiceListener2_1;

binder::Status H2BCameraServiceListener::onStatusChanged(
    int32_t status, const ::android::String16& cameraId) {
  HCameraDeviceStatus hCameraDeviceStatus = convertToHidlCameraDeviceStatus(status);
  CameraStatusAndId cameraStatusAndId;
  cameraStatusAndId.deviceStatus = hCameraDeviceStatus;
  cameraStatusAndId.cameraId = String8(cameraId).string();
  auto ret = mBase->onStatusChanged(cameraStatusAndId);
  if (!ret.isOk()) {
      ALOGE("%s OnStatusChanged callback failed due to %s",__FUNCTION__,
            ret.description().c_str());
  }
  return binder::Status::ok();
}

binder::Status H2BCameraServiceListener::onPhysicalCameraStatusChanged(
    int32_t status, const ::android::String16& cameraId,
    const ::android::String16& physicalCameraId) {
  auto cast2_1 = HCameraServiceListener2_1::castFrom(mBase);
  sp<HCameraServiceListener2_1> interface2_1 = nullptr;
  if (cast2_1.isOk()) {
    interface2_1 = cast2_1;
    if (interface2_1 != nullptr) {
      HCameraDeviceStatus hCameraDeviceStatus = convertToHidlCameraDeviceStatus(status);
      V2_1::PhysicalCameraStatusAndId cameraStatusAndId;
      cameraStatusAndId.deviceStatus = hCameraDeviceStatus;
      cameraStatusAndId.cameraId = String8(cameraId).string();
      cameraStatusAndId.physicalCameraId = String8(physicalCameraId).string();
      auto ret = interface2_1->onPhysicalCameraStatusChanged(cameraStatusAndId);
      if (!ret.isOk()) {
        ALOGE("%s OnPhysicalCameraStatusChanged callback failed due to %s",__FUNCTION__,
            ret.description().c_str());
      }
    }
  }
  return binder::Status::ok();
}

::android::binder::Status H2BCameraServiceListener::onTorchStatusChanged(
    int32_t, const ::android::String16&) {
  // We don't implement onTorchStatusChanged
  return binder::Status::ok();
}

} // implementation
} // V2_0
} // common
} // cameraservice
} // frameworks
} // android
