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

#ifndef ANDROID_SERVERS_CAMERA3_INFLIGHT_REQUEST_H
#define ANDROID_SERVERS_CAMERA3_INFLIGHT_REQUEST_H

#include <set>

#include <camera/CaptureResult.h>
#include <camera/CameraMetadata.h>
#include <utils/String8.h>
#include <utils/Timers.h>

#include "common/CameraDeviceBase.h"

namespace android {

namespace camera3 {

typedef enum {
    // Cache the buffers with STATUS_ERROR within InFlightRequest
    ERROR_BUF_CACHE,
    // Return the buffers with STATUS_ERROR to the buffer queue
    ERROR_BUF_RETURN,
    // Return the buffers with STATUS_ERROR to the buffer queue, and call
    // notify(ERROR_BUFFER) as well
    ERROR_BUF_RETURN_NOTIFY
} ERROR_BUF_STRATEGY;

struct InFlightRequest {

    // Set by notify() SHUTTER call.
    nsecs_t shutterTimestamp;
    // Set by process_capture_result().
    nsecs_t sensorTimestamp;
    int     requestStatus;
    // Set by process_capture_result call with valid metadata
    bool    haveResultMetadata;
    // Decremented by calls to process_capture_result with valid output
    // and input buffers
    int     numBuffersLeft;

    // The inflight request is considered complete if all buffers are returned

    CaptureResultExtras resultExtras;
    // If this request has any input buffer
    bool hasInputBuffer;

    // The last metadata that framework receives from HAL and
    // not yet send out because the shutter event hasn't arrived.
    // It's added by process_capture_result and sent when framework
    // receives the shutter event.
    CameraMetadata pendingMetadata;

    // The metadata of the partial results that framework receives from HAL so far
    // and has sent out.
    CameraMetadata collectedPartialResult;

    // Buffers are added by process_capture_result when output buffers
    // return from HAL but framework has not yet received the shutter
    // event. They will be returned to the streams when framework receives
    // the shutter event.
    Vector<camera_stream_buffer_t> pendingOutputBuffers;

    // Whether this inflight request's shutter and result callback are to be
    // called. The policy is that if the request is the last one in the constrained
    // high speed recording request list, this flag will be true. If the request list
    // is not for constrained high speed recording, this flag will also be true.
    bool hasCallback;

    // Maximum expected frame duration for this request.
    // For manual captures, equal to the max of requested exposure time and frame duration
    // For auto-exposure modes, equal to 1/(lower end of target FPS range)
    nsecs_t maxExpectedDuration;

    // Whether the result metadata for this request is to be skipped. The
    // result metadata should be skipped in the case of
    // REQUEST/RESULT error.
    bool skipResultMetadata;

    // Whether the buffers with STATUS_ERROR should be cached as pending buffers,
    // returned to the buffer queue, or returned to the buffer queue and notify with ERROR_BUFFER.
    ERROR_BUF_STRATEGY errorBufStrategy;

    // The physical camera ids being requested.
    // For request on a physical camera stream, the inside set contains one Id
    // For request on a stream group containing physical camera streams, the
    // inside set contains all stream Ids in the group.
    std::set<std::set<String8>> physicalCameraIds;

    // Map of physicalCameraId <-> Metadata
    std::vector<PhysicalCaptureResultInfo> physicalMetadatas;

    // Indicates a still capture request.
    bool stillCapture;

    // Indicates a ZSL capture request
    bool zslCapture;

    // Indicates that ROTATE_AND_CROP was set to AUTO
    bool rotateAndCropAuto;

    // Requested camera ids (both logical and physical) with zoomRatio != 1.0f
    std::set<std::string> cameraIdsWithZoom;

    // Time of capture request (from systemTime) in Ns
    nsecs_t requestTimeNs;

    // What shared surfaces an output should go to
    SurfaceMap outputSurfaces;

    // Current output transformation
    int32_t transform;

    // TODO: dedupe
    static const nsecs_t kDefaultExpectedDuration = 100000000; // 100 ms

    // Default constructor needed by KeyedVector
    InFlightRequest() :
            shutterTimestamp(0),
            sensorTimestamp(0),
            requestStatus(OK),
            haveResultMetadata(false),
            numBuffersLeft(0),
            hasInputBuffer(false),
            hasCallback(true),
            maxExpectedDuration(kDefaultExpectedDuration),
            skipResultMetadata(false),
            errorBufStrategy(ERROR_BUF_CACHE),
            stillCapture(false),
            zslCapture(false),
            rotateAndCropAuto(false),
            requestTimeNs(0),
            transform(-1) {
    }

    InFlightRequest(int numBuffers, CaptureResultExtras extras, bool hasInput,
            bool hasAppCallback, nsecs_t maxDuration,
            const std::set<std::set<String8>>& physicalCameraIdSet, bool isStillCapture,
            bool isZslCapture, bool rotateAndCropAuto, const std::set<std::string>& idsWithZoom,
            nsecs_t requestNs, const SurfaceMap& outSurfaces = SurfaceMap{}) :
            shutterTimestamp(0),
            sensorTimestamp(0),
            requestStatus(OK),
            haveResultMetadata(false),
            numBuffersLeft(numBuffers),
            resultExtras(extras),
            hasInputBuffer(hasInput),
            hasCallback(hasAppCallback),
            maxExpectedDuration(maxDuration),
            skipResultMetadata(false),
            errorBufStrategy(ERROR_BUF_CACHE),
            physicalCameraIds(physicalCameraIdSet),
            stillCapture(isStillCapture),
            zslCapture(isZslCapture),
            rotateAndCropAuto(rotateAndCropAuto),
            cameraIdsWithZoom(idsWithZoom),
            requestTimeNs(requestNs),
            outputSurfaces(outSurfaces),
            transform(-1) {
    }
};

// Map from frame number to the in-flight request state
typedef KeyedVector<uint32_t, InFlightRequest> InFlightRequestMap;

} // namespace camera3

} // namespace android

#endif
