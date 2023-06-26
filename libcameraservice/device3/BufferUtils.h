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

#ifndef ANDROID_SERVERS_CAMERA3_BUFFER_UTILS_H
#define ANDROID_SERVERS_CAMERA3_BUFFER_UTILS_H

#include <unordered_map>
#include <mutex>
#include <set>

#include <cutils/native_handle.h>

#include <android/hardware/camera/device/3.2/ICameraDevice.h>

#include <device3/Camera3OutputInterface.h>

namespace android {

namespace camera3 {

    struct BufferHasher {
        size_t operator()(const buffer_handle_t& buf) const {
            if (buf == nullptr)
                return 0;

            size_t result = 1;
            result = 31 * result + buf->numFds;
            for (int i = 0; i < buf->numFds; i++) {
                result = 31 * result + buf->data[i];
            }
            return result;
        }
    };

    struct BufferComparator {
        bool operator()(const buffer_handle_t& buf1, const buffer_handle_t& buf2) const {
            if (buf1->numFds == buf2->numFds) {
                for (int i = 0; i < buf1->numFds; i++) {
                    if (buf1->data[i] != buf2->data[i]) {
                        return false;
                    }
                }
                return true;
            }
            return false;
        }
    };

    // Per stream buffer native handle -> bufId map
    typedef std::unordered_map<const buffer_handle_t, uint64_t,
            BufferHasher, BufferComparator> BufferIdMap;

    // streamId -> BufferIdMap
    typedef std::unordered_map<int, BufferIdMap> BufferIdMaps;

    // Map of inflight buffers sent along in capture requests.
    // Key is composed by (frameNumber << 32 | streamId)
    typedef std::unordered_map<uint64_t, buffer_handle_t*> InflightBufferMap;

    // Map of inflight buffers dealt by requestStreamBuffers API
    typedef std::unordered_map<uint64_t, std::pair<int32_t, buffer_handle_t*>> RequestedBufferMap;

    // A struct containing all buffer tracking information like inflight buffers
    // and buffer ID caches
    class BufferRecords : public BufferRecordsInterface {

    public:
        BufferRecords() {}

        BufferRecords(BufferRecords&& other) :
                mBufferIdMaps(other.mBufferIdMaps),
                mNextBufferId(other.mNextBufferId),
                mInflightBufferMap(other.mInflightBufferMap),
                mRequestedBufferMap(other.mRequestedBufferMap) {}

        virtual ~BufferRecords() {}

        // Helper methods to help moving buffer records
        void takeInflightBufferMap(BufferRecords& other);
        void takeRequestedBufferMap(BufferRecords& other);
        void takeBufferCaches(BufferRecords& other, const std::vector<int32_t>& streams);

        // method to extract buffer's unique ID
        // return pair of (newlySeenBuffer?, bufferId)
        virtual std::pair<bool, uint64_t> getBufferId(
                const buffer_handle_t& buf, int streamId) override;

        void tryCreateBufferCache(int streamId);

        void removeInactiveBufferCaches(const std::set<int32_t>& activeStreams);

        // Return the removed buffer ID if input cache is found.
        // Otherwise return BUFFER_ID_NO_BUFFER
        uint64_t removeOneBufferCache(int streamId, const native_handle_t* handle) override;

        // Clear all caches for input stream, but do not remove the stream
        // Removed buffers' ID are returned
        std::vector<uint64_t> clearBufferCaches(int streamId);

        bool isStreamCached(int streamId);

        // Return true if the input caches match what we have; otherwise false
        bool verifyBufferIds(int32_t streamId, std::vector<uint64_t>& inBufIds);

        // Get a vector of (frameNumber, streamId) pair of currently inflight
        // buffers
        void getInflightBufferKeys(std::vector<std::pair<int32_t, int32_t>>* out);

        status_t pushInflightBuffer(int32_t frameNumber, int32_t streamId,
                buffer_handle_t *buffer);

        // Find a buffer_handle_t based on frame number and stream ID
        virtual status_t popInflightBuffer(int32_t frameNumber, int32_t streamId,
                /*out*/ buffer_handle_t **buffer) override;

        // Pop inflight buffers based on pairs of (frameNumber,streamId)
        void popInflightBuffers(const std::vector<std::pair<int32_t, int32_t>>& buffers);

        // Get a vector of bufferId of currently inflight buffers
        void getInflightRequestBufferKeys(std::vector<uint64_t>* out);

        // Register a bufId (streamId, buffer_handle_t) to inflight request buffer
        virtual status_t pushInflightRequestBuffer(
                uint64_t bufferId, buffer_handle_t* buf, int32_t streamId) override;

        // Find a buffer_handle_t based on bufferId
        virtual status_t popInflightRequestBuffer(uint64_t bufferId,
                /*out*/ buffer_handle_t** buffer,
                /*optional out*/ int32_t* streamId = nullptr) override;

    private:
        std::mutex mBufferIdMapLock;
        BufferIdMaps mBufferIdMaps;
        uint64_t mNextBufferId = 1; // 0 means no buffer

        std::mutex mInflightLock;
        InflightBufferMap mInflightBufferMap;

        std::mutex mRequestedBuffersLock;
        RequestedBufferMap mRequestedBufferMap;
    }; // class BufferRecords

    static const uint64_t BUFFER_ID_NO_BUFFER = 0;

    camera_buffer_status_t mapHidlBufferStatus(
            hardware::camera::device::V3_2::BufferStatus status);
} // namespace camera3

} // namespace android

#endif
