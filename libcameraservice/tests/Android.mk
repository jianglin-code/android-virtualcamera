# Copyright 2013 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= $(call all-cpp-files-under, .)

LOCAL_SHARED_LIBRARIES := \
    libbase \
    libcutils \
    libcameraservice \
    libhidlbase \
    liblog \
    libcamera_client \
    libcamera_metadata \
    libui \
    libutils \
    libjpeg \
    libexif \
    android.hardware.camera.common@1.0 \
    android.hardware.camera.provider@2.4 \
    android.hardware.camera.provider@2.5 \
    android.hardware.camera.provider@2.6 \
    android.hardware.camera.provider@2.7 \
    android.hardware.camera.device@1.0 \
    android.hardware.camera.device@3.2 \
    android.hardware.camera.device@3.4 \
    android.hardware.camera.device@3.7 \
    android.hidl.token@1.0-utils

LOCAL_STATIC_LIBRARIES := \
    libgmock

LOCAL_C_INCLUDES += \
    system/media/private/camera/include \
    external/dynamic_depth/includes \
    external/dynamic_depth/internal \

LOCAL_CFLAGS += -Wall -Wextra -Werror

LOCAL_SANITIZE := address

LOCAL_MODULE:= cameraservice_test
LOCAL_LICENSE_KINDS:= SPDX-license-identifier-Apache-2.0
LOCAL_LICENSE_CONDITIONS:= notice
LOCAL_NOTICE_FILE:= $(LOCAL_PATH)/../NOTICE
LOCAL_COMPATIBILITY_SUITE := device-tests
LOCAL_MODULE_TAGS := tests

include $(BUILD_NATIVE_TEST)
