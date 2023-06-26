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

#ifndef ANDROID_SERVERS_CAMERA3_OUTPUT_INTERFACE_H
#define ANDROID_SERVERS_CAMERA3_OUTPUT_INTERFACE_H

#include <memory>

#include <cutils/native_handle.h>

#include <utils/Timers.h>

#include "device3/Camera3StreamInterface.h"

namespace android {

namespace camera3 {

    /**
     * Interfaces used by result/notification path shared between Camera3Device and
     * Camera3OfflineSession
     */
    class SetErrorInterface {
    public:
        // Switch device into error state and send a ERROR_DEVICE notification
        virtual void setErrorState(const char *fmt, ...) = 0;
        // Same as setErrorState except this method assumes callers holds the main object lock
        virtual void setErrorStateLocked(const char *fmt, ...) = 0;

        virtual ~SetErrorInterface() {}
    };

    // Interface used by callback path to update buffer records
    class BufferRecordsInterface {
    public:
        // method to extract buffer's unique ID
        // return pair of (newlySeenBuffer?, bufferId)
        virtual std::pair<bool, uint64_t> getBufferId(const buffer_handle_t& buf, int streamId) = 0;

        // Return the removed buffer ID if input cache is found.
        // Otherwise return BUFFER_ID_NO_BUFFER
        virtual uint64_t removeOneBufferCache(int streamId, const native_handle_t* handle) = 0;

        // Find a buffer_handle_t based on frame number and stream ID
        virtual status_t popInflightBuffer(int32_t frameNumber, int32_t streamId,
                /*out*/ buffer_handle_t **buffer) = 0;

        // Register a bufId (streamId, buffer_handle_t) to inflight request buffer
        virtual status_t pushInflightRequestBuffer(
                uint64_t bufferId, buffer_handle_t* buf, int32_t streamId) = 0;

        // Find a buffer_handle_t based on bufferId
        virtual status_t popInflightRequestBuffer(uint64_t bufferId,
                /*out*/ buffer_handle_t** buffer,
                /*optional out*/ int32_t* streamId = nullptr) = 0;

        virtual ~BufferRecordsInterface() {}
    };

    class InflightRequestUpdateInterface {
    public:
        // Caller must hold the lock proctecting InflightRequestMap
        // duration: the maxExpectedDuration of the removed entry
        virtual void onInflightEntryRemovedLocked(nsecs_t duration) = 0;

        virtual void checkInflightMapLengthLocked() = 0;

        virtual void onInflightMapFlushedLocked() = 0;

        virtual ~InflightRequestUpdateInterface() {}
    };

    class RequestBufferInterface {
    public:
        // Return if the state machine currently allows for requestBuffers.
        // If this returns true, caller must call endRequestBuffer() later to signal end of a
        // request buffer transaction.
        virtual bool startRequestBuffer() = 0;

        virtual void endRequestBuffer() = 0;

        // Returns how long should implementation wait for a buffer returned
        virtual nsecs_t getWaitDuration() = 0;

        virtual ~RequestBufferInterface() {}
    };

    class FlushBufferInterface {
    public:
        virtual void getInflightBufferKeys(std::vector<std::pair<int32_t, int32_t>>* out) = 0;

        virtual void getInflightRequestBufferKeys(std::vector<uint64_t>* out) = 0;

        virtual std::vector<sp<Camera3StreamInterface>> getAllStreams() = 0;

        virtual ~FlushBufferInterface() {}
    };
} // namespace camera3

} // namespace android

#endif
