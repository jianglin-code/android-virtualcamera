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

#define LOG_TAG "Camera3-OutStrmIntf"
#define ATRACE_TAG ATRACE_TAG_CAMERA
//#define LOG_NDEBUG 0
//#define LOG_NNDEBUG 0  // Per-frame verbose logging


#include "Camera3OutputStreamInterface.h"

namespace android {

namespace camera3 {

status_t StreamSet::add(
        int streamId, sp<camera3::Camera3OutputStreamInterface> stream) {
    if (stream == nullptr) {
        ALOGE("%s: cannot add null stream", __FUNCTION__);
        return BAD_VALUE;
    }
    std::lock_guard<std::mutex> lock(mLock);
    return mData.add(streamId, stream);
}

ssize_t StreamSet::remove(int streamId) {
    std::lock_guard<std::mutex> lock(mLock);
    return mData.removeItem(streamId);
}

sp<camera3::Camera3OutputStreamInterface> StreamSet::get(int streamId) {
    std::lock_guard<std::mutex> lock(mLock);
    ssize_t idx = mData.indexOfKey(streamId);
    if (idx == NAME_NOT_FOUND) {
        return nullptr;
    }
    return mData.editValueAt(idx);
}

sp<camera3::Camera3OutputStreamInterface> StreamSet::operator[] (size_t index) {
    std::lock_guard<std::mutex> lock(mLock);
    return mData.editValueAt(index);
}

size_t StreamSet::size() const {
    std::lock_guard<std::mutex> lock(mLock);
    return mData.size();
}

void StreamSet::clear() {
    std::lock_guard<std::mutex> lock(mLock);
    return mData.clear();
}

std::vector<int> StreamSet::getStreamIds() {
    std::lock_guard<std::mutex> lock(mLock);
    std::vector<int> streamIds(mData.size());
    for (size_t i = 0; i < mData.size(); i++) {
        streamIds[i] = mData.keyAt(i);
    }
    return streamIds;
}

StreamSet::StreamSet(const StreamSet& other) {
    std::lock_guard<std::mutex> lock(other.mLock);
    mData = other.mData;
}

} // namespace camera3

} // namespace android
