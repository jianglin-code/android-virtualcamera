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

#ifndef ANDROID_SERVERS_CAMERA3_OUTPUT_UTILS_H
#define ANDROID_SERVERS_CAMERA3_OUTPUT_UTILS_H

#include <memory>
#include <mutex>

#include <cutils/native_handle.h>

#include <fmq/MessageQueue.h>

#include <common/CameraDeviceBase.h>

#include "device3/BufferUtils.h"
#include "device3/DistortionMapper.h"
#include "device3/ZoomRatioMapper.h"
#include "device3/RotateAndCropMapper.h"
#include "device3/InFlightRequest.h"
#include "device3/Camera3Stream.h"
#include "device3/Camera3OutputStreamInterface.h"
#include "utils/SessionStatsBuilder.h"
#include "utils/TagMonitor.h"

namespace android {

using ResultMetadataQueue = hardware::MessageQueue<uint8_t, hardware::kSynchronizedReadWrite>;

namespace camera3 {

    typedef struct camera_stream_configuration {
        uint32_t num_streams;
        camera_stream_t **streams;
        uint32_t operation_mode;
        bool input_is_multi_resolution;
    } camera_stream_configuration_t;

    typedef struct camera_capture_request {
        uint32_t frame_number;
        const camera_metadata_t *settings;
        camera_stream_buffer_t *input_buffer;
        uint32_t num_output_buffers;
        const camera_stream_buffer_t *output_buffers;
        uint32_t num_physcam_settings;
        const char **physcam_id;
        const camera_metadata_t **physcam_settings;
        int32_t input_width;
        int32_t input_height;
    } camera_capture_request_t;

    typedef struct camera_capture_result {
        uint32_t frame_number;
        const camera_metadata_t *result;
        uint32_t num_output_buffers;
        const camera_stream_buffer_t *output_buffers;
        const camera_stream_buffer_t *input_buffer;
        uint32_t partial_result;
        uint32_t num_physcam_metadata;
        const char **physcam_ids;
        const camera_metadata_t **physcam_metadata;
    } camera_capture_result_t;

    typedef struct camera_shutter_msg {
        uint32_t frame_number;
        uint64_t timestamp;
    } camera_shutter_msg_t;

    typedef struct camera_error_msg {
        uint32_t frame_number;
        camera_stream_t *error_stream;
        int error_code;
    } camera_error_msg_t;

    typedef enum camera_error_msg_code {
        CAMERA_MSG_ERROR_DEVICE = 1,
        CAMERA_MSG_ERROR_REQUEST = 2,
        CAMERA_MSG_ERROR_RESULT = 3,
        CAMERA_MSG_ERROR_BUFFER = 4,
        CAMERA_MSG_NUM_ERRORS
    } camera_error_msg_code_t;

    typedef struct camera_notify_msg {
        int type;

        union {
            camera_error_msg_t error;
            camera_shutter_msg_t shutter;
        } message;
    } camera_notify_msg_t;

    /**
     * Helper methods shared between Camera3Device/Camera3OfflineSession for HAL callbacks
     */

    // helper function to return the output buffers to output streams. The
    // function also optionally calls notify(ERROR_BUFFER).
    void returnOutputBuffers(
            bool useHalBufManager,
            sp<NotificationListener> listener, // Only needed when outputSurfaces is not empty
            const camera_stream_buffer_t *outputBuffers,
            size_t numBuffers, nsecs_t timestamp, bool requested, nsecs_t requestTimeNs,
            SessionStatsBuilder& sessionStatsBuilder, bool timestampIncreasing = true,
            // The following arguments are only meant for surface sharing use case
            const SurfaceMap& outputSurfaces = SurfaceMap{},
            // Used to send buffer error callback when failing to return buffer
            const CaptureResultExtras &resultExtras = CaptureResultExtras{},
            ERROR_BUF_STRATEGY errorBufStrategy = ERROR_BUF_RETURN,
            int32_t transform = -1);

    // helper function to return the output buffers to output streams, and
    // remove the returned buffers from the inflight request's pending buffers
    // vector.
    void returnAndRemovePendingOutputBuffers(
            bool useHalBufManager,
            sp<NotificationListener> listener, // Only needed when outputSurfaces is not empty
            InFlightRequest& request, SessionStatsBuilder& sessionStatsBuilder);

    // Camera3Device/Camera3OfflineSession internal states used in notify/processCaptureResult
    // callbacks
    struct CaptureOutputStates {
        const String8& cameraId;
        std::mutex& inflightLock;
        int64_t& lastCompletedRegularFrameNumber;
        int64_t& lastCompletedReprocessFrameNumber;
        int64_t& lastCompletedZslFrameNumber;
        InFlightRequestMap& inflightMap; // end of inflightLock scope
        std::mutex& outputLock;
        std::list<CaptureResult>& resultQueue;
        std::condition_variable& resultSignal;
        uint32_t& nextShutterFrameNum;
        uint32_t& nextReprocShutterFrameNum;
        uint32_t& nextZslShutterFrameNum;
        uint32_t& nextResultFrameNum;
        uint32_t& nextReprocResultFrameNum;
        uint32_t& nextZslResultFrameNum; // end of outputLock scope
        const bool useHalBufManager;
        const bool usePartialResult;
        const bool needFixupMonoChrome;
        const uint32_t numPartialResults;
        const metadata_vendor_id_t vendorTagId;
        const CameraMetadata& deviceInfo;
        const std::unordered_map<std::string, CameraMetadata>& physicalDeviceInfoMap;
        std::unique_ptr<ResultMetadataQueue>& fmq;
        std::unordered_map<std::string, camera3::DistortionMapper>& distortionMappers;
        std::unordered_map<std::string, camera3::ZoomRatioMapper>& zoomRatioMappers;
        std::unordered_map<std::string, camera3::RotateAndCropMapper>& rotateAndCropMappers;
        TagMonitor& tagMonitor;
        sp<Camera3Stream> inputStream;
        StreamSet& outputStreams;
        SessionStatsBuilder& sessionStatsBuilder;
        sp<NotificationListener> listener;
        SetErrorInterface& setErrIntf;
        InflightRequestUpdateInterface& inflightIntf;
        BufferRecordsInterface& bufferRecordsIntf;
        bool legacyClient;
    };

    // Handle one capture result. Assume callers hold the lock to serialize all
    // processCaptureResult calls
    void processOneCaptureResultLocked(
            CaptureOutputStates& states,
            const hardware::camera::device::V3_2::CaptureResult& result,
            const hardware::hidl_vec<
                    hardware::camera::device::V3_4::PhysicalCameraMetadata> physicalCameraMetadata);

    // Handle one notify message
    void notify(CaptureOutputStates& states,
            const hardware::camera::device::V3_2::NotifyMsg& msg);

    struct RequestBufferStates {
        const String8& cameraId;
        std::mutex& reqBufferLock; // lock to serialize request buffer calls
        const bool useHalBufManager;
        StreamSet& outputStreams;
        SessionStatsBuilder& sessionStatsBuilder;
        SetErrorInterface& setErrIntf;
        BufferRecordsInterface& bufferRecordsIntf;
        RequestBufferInterface& reqBufferIntf;
    };

    void requestStreamBuffers(RequestBufferStates& states,
            const hardware::hidl_vec<hardware::camera::device::V3_5::BufferRequest>& bufReqs,
            hardware::camera::device::V3_5::ICameraDeviceCallback::requestStreamBuffers_cb _hidl_cb);

    struct ReturnBufferStates {
        const String8& cameraId;
        const bool useHalBufManager;
        StreamSet& outputStreams;
        SessionStatsBuilder& sessionStatsBuilder;
        BufferRecordsInterface& bufferRecordsIntf;
    };

    void returnStreamBuffers(ReturnBufferStates& states,
            const hardware::hidl_vec<hardware::camera::device::V3_2::StreamBuffer>& buffers);

    struct FlushInflightReqStates {
        const String8& cameraId;
        std::mutex& inflightLock;
        InFlightRequestMap& inflightMap; // end of inflightLock scope
        const bool useHalBufManager;
        sp<NotificationListener> listener;
        InflightRequestUpdateInterface& inflightIntf;
        BufferRecordsInterface& bufferRecordsIntf;
        FlushBufferInterface& flushBufferIntf;
        SessionStatsBuilder& sessionStatsBuilder;
    };

    void flushInflightRequests(FlushInflightReqStates& states);
} // namespace camera3

} // namespace android

#endif
