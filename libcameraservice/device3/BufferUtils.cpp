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

#define LOG_TAG "Camera3-BufUtils"
#define ATRACE_TAG ATRACE_TAG_CAMERA
//#define LOG_NDEBUG 0
//#define LOG_NNDEBUG 0  // Per-frame verbose logging

#include <inttypes.h>

#include <utils/Log.h>

#include "device3/BufferUtils.h"

namespace android {
namespace camera3 {

camera_buffer_status_t mapHidlBufferStatus(hardware::camera::device::V3_2::BufferStatus status) {
    using hardware::camera::device::V3_2::BufferStatus;

    switch (status) {
        case BufferStatus::OK: return CAMERA_BUFFER_STATUS_OK;
        case BufferStatus::ERROR: return CAMERA_BUFFER_STATUS_ERROR;
    }
    return CAMERA_BUFFER_STATUS_ERROR;
}

void BufferRecords::takeInflightBufferMap(BufferRecords& other) {
    std::lock_guard<std::mutex> oLock(other.mInflightLock);
    std::lock_guard<std::mutex> lock(mInflightLock);
    if (mInflightBufferMap.size() > 0) {
        ALOGE("%s: inflight map is set in non-empty state!", __FUNCTION__);
    }
    mInflightBufferMap = std::move(other.mInflightBufferMap);
    other.mInflightBufferMap.clear();
}

void BufferRecords::takeRequestedBufferMap(BufferRecords& other) {
    std::lock_guard<std::mutex> oLock(other.mRequestedBuffersLock);
    std::lock_guard<std::mutex> lock(mRequestedBuffersLock);
    if (mRequestedBufferMap.size() > 0) {
        ALOGE("%s: requested buffer map is set in non-empty state!", __FUNCTION__);
    }
    mRequestedBufferMap = std::move(other.mRequestedBufferMap);
    other.mRequestedBufferMap.clear();
}

void BufferRecords::takeBufferCaches(BufferRecords& other, const std::vector<int32_t>& streams) {
    std::lock_guard<std::mutex> oLock(other.mBufferIdMapLock);
    std::lock_guard<std::mutex> lock(mBufferIdMapLock);
    if (mBufferIdMaps.size() > 0) {
        ALOGE("%s: buffer ID map is set in non-empty state!", __FUNCTION__);
    }
    for (auto streamId : streams) {
        mBufferIdMaps.insert({streamId, std::move(other.mBufferIdMaps.at(streamId))});
    }
    other.mBufferIdMaps.clear();
}

std::pair<bool, uint64_t> BufferRecords::getBufferId(
        const buffer_handle_t& buf, int streamId) {
    std::lock_guard<std::mutex> lock(mBufferIdMapLock);

    BufferIdMap& bIdMap = mBufferIdMaps.at(streamId);
    auto it = bIdMap.find(buf);
    if (it == bIdMap.end()) {
        bIdMap[buf] = mNextBufferId++;
        ALOGV("stream %d now have %zu buffer caches, buf %p",
                streamId, bIdMap.size(), buf);
        return std::make_pair(true, mNextBufferId - 1);
    } else {
        return std::make_pair(false, it->second);
    }
}

void BufferRecords::tryCreateBufferCache(int streamId) {
    std::lock_guard<std::mutex> lock(mBufferIdMapLock);
    if (mBufferIdMaps.count(streamId) == 0) {
        mBufferIdMaps.emplace(streamId, BufferIdMap{});
    }
}

void BufferRecords::removeInactiveBufferCaches(const std::set<int32_t>& activeStreams) {
    std::lock_guard<std::mutex> lock(mBufferIdMapLock);
    for(auto it = mBufferIdMaps.begin(); it != mBufferIdMaps.end();) {
        int streamId = it->first;
        bool active = activeStreams.count(streamId) > 0;
        if (!active) {
            it = mBufferIdMaps.erase(it);
        } else {
            ++it;
        }
    }
}

uint64_t BufferRecords::removeOneBufferCache(int streamId, const native_handle_t* handle) {
    std::lock_guard<std::mutex> lock(mBufferIdMapLock);
    uint64_t bufferId = BUFFER_ID_NO_BUFFER;
    auto mapIt = mBufferIdMaps.find(streamId);
    if (mapIt == mBufferIdMaps.end()) {
        // streamId might be from a deleted stream here
        ALOGI("%s: stream %d has been removed",
                __FUNCTION__, streamId);
        return BUFFER_ID_NO_BUFFER;
    }
    BufferIdMap& bIdMap = mapIt->second;
    auto it = bIdMap.find(handle);
    if (it == bIdMap.end()) {
        ALOGW("%s: cannot find buffer %p in stream %d",
                __FUNCTION__, handle, streamId);
        return BUFFER_ID_NO_BUFFER;
    } else {
        bufferId = it->second;
        bIdMap.erase(it);
        ALOGV("%s: stream %d now have %zu buffer caches after removing buf %p",
                __FUNCTION__, streamId, bIdMap.size(), handle);
    }
    return bufferId;
}

std::vector<uint64_t> BufferRecords::clearBufferCaches(int streamId) {
    std::lock_guard<std::mutex> lock(mBufferIdMapLock);
    std::vector<uint64_t> ret;
    auto mapIt = mBufferIdMaps.find(streamId);
    if (mapIt == mBufferIdMaps.end()) {
        ALOGE("%s: streamId %d not found!", __FUNCTION__, streamId);
        return ret;
    }
    BufferIdMap& bIdMap = mapIt->second;
    ret.reserve(bIdMap.size());
    for (const auto& it : bIdMap) {
        ret.push_back(it.second);
    }
    bIdMap.clear();
    return ret;
}

bool BufferRecords::isStreamCached(int streamId) {
    std::lock_guard<std::mutex> lock(mBufferIdMapLock);
    return mBufferIdMaps.find(streamId) != mBufferIdMaps.end();
}

bool BufferRecords::verifyBufferIds(
        int32_t streamId, std::vector<uint64_t>& bufIds) {
    std::lock_guard<std::mutex> lock(mBufferIdMapLock);
    camera3::BufferIdMap& bIdMap = mBufferIdMaps.at(streamId);
    if (bIdMap.size() != bufIds.size()) {
        ALOGE("%s: stream ID %d buffer cache number mismatch: %zu/%zu (service/HAL)",
                __FUNCTION__, streamId, bIdMap.size(), bufIds.size());
        return false;
    }
    std::vector<uint64_t> internalBufIds;
    internalBufIds.reserve(bIdMap.size());
    for (const auto& pair : bIdMap) {
        internalBufIds.push_back(pair.second);
    }
    std::sort(bufIds.begin(), bufIds.end());
    std::sort(internalBufIds.begin(), internalBufIds.end());
    for (size_t i = 0; i < bufIds.size(); i++) {
        if (bufIds[i] != internalBufIds[i]) {
            ALOGE("%s: buffer cache mismatch! Service %" PRIu64 ", HAL %" PRIu64,
                    __FUNCTION__, internalBufIds[i], bufIds[i]);
            return false;
        }
    }
    return true;
}

void BufferRecords::getInflightBufferKeys(
        std::vector<std::pair<int32_t, int32_t>>* out) {
    std::lock_guard<std::mutex> lock(mInflightLock);
    out->clear();
    out->reserve(mInflightBufferMap.size());
    for (auto& pair : mInflightBufferMap) {
        uint64_t key = pair.first;
        int32_t streamId = key & 0xFFFFFFFF;
        int32_t frameNumber = (key >> 32) & 0xFFFFFFFF;
        out->push_back(std::make_pair(frameNumber, streamId));
    }
    return;
}

status_t BufferRecords::pushInflightBuffer(
        int32_t frameNumber, int32_t streamId, buffer_handle_t *buffer) {
    std::lock_guard<std::mutex> lock(mInflightLock);
    uint64_t key = static_cast<uint64_t>(frameNumber) << 32 | static_cast<uint64_t>(streamId);
    mInflightBufferMap[key] = buffer;
    return OK;
}

status_t BufferRecords::popInflightBuffer(
        int32_t frameNumber, int32_t streamId,
        /*out*/ buffer_handle_t **buffer) {
    std::lock_guard<std::mutex> lock(mInflightLock);

    uint64_t key = static_cast<uint64_t>(frameNumber) << 32 | static_cast<uint64_t>(streamId);
    auto it = mInflightBufferMap.find(key);
    if (it == mInflightBufferMap.end()) return NAME_NOT_FOUND;
    if (buffer != nullptr) {
        *buffer = it->second;
    }
    mInflightBufferMap.erase(it);
    return OK;
}

void BufferRecords::popInflightBuffers(
        const std::vector<std::pair<int32_t, int32_t>>& buffers) {
    for (const auto& pair : buffers) {
        int32_t frameNumber = pair.first;
        int32_t streamId = pair.second;
        popInflightBuffer(frameNumber, streamId, nullptr);
    }
}

status_t BufferRecords::pushInflightRequestBuffer(
        uint64_t bufferId, buffer_handle_t* buf, int32_t streamId) {
    std::lock_guard<std::mutex> lock(mRequestedBuffersLock);
    auto pair = mRequestedBufferMap.insert({bufferId, {streamId, buf}});
    if (!pair.second) {
        ALOGE("%s: bufId %" PRIu64 " is already inflight!",
                __FUNCTION__, bufferId);
        return BAD_VALUE;
    }
    return OK;
}

// Find and pop a buffer_handle_t based on bufferId
status_t BufferRecords::popInflightRequestBuffer(
        uint64_t bufferId,
        /*out*/ buffer_handle_t** buffer,
        /*optional out*/ int32_t* streamId) {
    if (buffer == nullptr) {
        ALOGE("%s: buffer (%p) must not be null", __FUNCTION__, buffer);
        return BAD_VALUE;
    }
    std::lock_guard<std::mutex> lock(mRequestedBuffersLock);
    auto it = mRequestedBufferMap.find(bufferId);
    if (it == mRequestedBufferMap.end()) {
        ALOGE("%s: bufId %" PRIu64 " is not inflight!",
                __FUNCTION__, bufferId);
        return BAD_VALUE;
    }
    *buffer = it->second.second;
    if (streamId != nullptr) {
        *streamId = it->second.first;
    }
    mRequestedBufferMap.erase(it);
    return OK;
}

void BufferRecords::getInflightRequestBufferKeys(
        std::vector<uint64_t>* out) {
    std::lock_guard<std::mutex> lock(mRequestedBuffersLock);
    out->clear();
    out->reserve(mRequestedBufferMap.size());
    for (auto& pair : mRequestedBufferMap) {
        out->push_back(pair.first);
    }
    return;
}


} // camera3
} // namespace android
