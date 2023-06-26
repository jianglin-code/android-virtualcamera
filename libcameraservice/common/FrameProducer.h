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

#ifndef ANDROID_SERVERS_CAMERA_FRAMEPRODUCER_H
#define ANDROID_SERVERS_CAMERA_FRAMEPRODUCER_H

#include <utils/RefBase.h>
#include <utils/String8.h>
#include <utils/Timers.h>

#include "camera/CameraMetadata.h"
#include "camera/CaptureResult.h"

namespace android {

/**
 * Abstract class for HAL frame producers
 */
class FrameProducer : public virtual RefBase {
  public:
    /**
     * Retrieve the static characteristics metadata buffer
     */
    virtual const CameraMetadata& info() const = 0;

    /**
     * Retrieve the device camera ID
     */
    virtual const String8& getId() const = 0;

    /**
     * Wait for a new frame to be produced, with timeout in nanoseconds.
     * Returns TIMED_OUT when no frame produced within the specified duration
     * May be called concurrently to most methods, except for getNextFrame
     */
    virtual status_t waitForNextFrame(nsecs_t timeout) = 0;

    /**
     * Get next capture result frame from the result queue. Returns NOT_ENOUGH_DATA
     * if the queue is empty; caller takes ownership of the metadata buffer inside
     * the capture result object's metadata field.
     * May be called concurrently to most methods, except for waitForNextFrame.
     */
    virtual status_t getNextResult(CaptureResult *frame) = 0;

}; // class FrameProducer

} // namespace android

#endif
