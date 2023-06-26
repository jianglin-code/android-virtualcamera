/*
 * Copyright (C) 2014-2018 The Android Open Source Project
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

#define LOG_TAG "Camera3-FakeStream"
#define ATRACE_TAG ATRACE_TAG_CAMERA
//#define LOG_NDEBUG 0

#include <utils/Log.h>
#include <utils/Trace.h>
#include "Camera3FakeStream.h"

namespace android {

namespace camera3 {

const String8 Camera3FakeStream::FAKE_ID;

Camera3FakeStream::Camera3FakeStream(int id) :
        Camera3IOStreamBase(id, CAMERA_STREAM_OUTPUT, FAKE_WIDTH, FAKE_HEIGHT,
                /*maxSize*/0, FAKE_FORMAT, FAKE_DATASPACE, FAKE_ROTATION,
                FAKE_ID, std::unordered_set<int32_t>{ANDROID_SENSOR_PIXEL_MODE_DEFAULT}) {

}

Camera3FakeStream::~Camera3FakeStream() {

}

status_t Camera3FakeStream::getBufferLocked(camera_stream_buffer *,
        const std::vector<size_t>&) {
    ATRACE_CALL();
    ALOGE("%s: Stream %d: Fake stream cannot produce buffers!", __FUNCTION__, mId);
    return INVALID_OPERATION;
}

status_t Camera3FakeStream::returnBufferLocked(
        const camera_stream_buffer &,
        nsecs_t, int32_t, const std::vector<size_t>&) {
    ATRACE_CALL();
    ALOGE("%s: Stream %d: Fake stream cannot return buffers!", __FUNCTION__, mId);
    return INVALID_OPERATION;
}

status_t Camera3FakeStream::returnBufferCheckedLocked(
            const camera_stream_buffer &,
            nsecs_t,
            bool,
            int32_t,
            const std::vector<size_t>&,
            /*out*/
            sp<Fence>*) {
    ATRACE_CALL();
    ALOGE("%s: Stream %d: Fake stream cannot return buffers!", __FUNCTION__, mId);
    return INVALID_OPERATION;
}

void Camera3FakeStream::dump(int fd, const Vector<String16> &args) const {
    (void) args;
    String8 lines;
    lines.appendFormat("    Stream[%d]: Fake\n", mId);
    write(fd, lines.string(), lines.size());

    Camera3IOStreamBase::dump(fd, args);
}

status_t Camera3FakeStream::setTransform(int) {
    ATRACE_CALL();
    // Do nothing
    return OK;
}

status_t Camera3FakeStream::detachBuffer(sp<GraphicBuffer>* buffer, int* fenceFd) {
    (void) buffer;
    (void) fenceFd;
    // Do nothing
    return OK;
}

status_t Camera3FakeStream::configureQueueLocked() {
    // Do nothing
    return OK;
}

status_t Camera3FakeStream::disconnectLocked() {
    mState = (mState == STATE_IN_RECONFIG) ? STATE_IN_CONFIG
                                           : STATE_CONSTRUCTED;
    return OK;
}

status_t Camera3FakeStream::getEndpointUsage(uint64_t *usage) const {
    *usage = FAKE_USAGE;
    return OK;
}

bool Camera3FakeStream::isVideoStream() const {
    return false;
}

bool Camera3FakeStream::isConsumerConfigurationDeferred(size_t /*surface_id*/) const {
    return false;
}

status_t Camera3FakeStream::dropBuffers(bool /*dropping*/) {
    return OK;
}

const String8& Camera3FakeStream::getPhysicalCameraId() const {
    return FAKE_ID;
}

status_t Camera3FakeStream::setConsumers(const std::vector<sp<Surface>>& /*consumers*/) {
    ALOGE("%s: Stream %d: Fake stream doesn't support set consumer surface!",
            __FUNCTION__, mId);
    return INVALID_OPERATION;
}

status_t Camera3FakeStream::updateStream(const std::vector<sp<Surface>> &/*outputSurfaces*/,
            const std::vector<OutputStreamInfo> &/*outputInfo*/,
            const std::vector<size_t> &/*removedSurfaceIds*/,
            KeyedVector<sp<Surface>, size_t> * /*outputMap*/) {
    ALOGE("%s: this method is not supported!", __FUNCTION__);
    return INVALID_OPERATION;
}

status_t Camera3FakeStream::setBatchSize(size_t /*batchSize*/) {
    ALOGE("%s: this method is not supported!", __FUNCTION__);
    return INVALID_OPERATION;
}

}; // namespace camera3

}; // namespace android
