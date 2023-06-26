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

#define LOG_NDEBUG 0
#define LOG_TAG "ClientManagerTest"

#include <binder/ActivityManager.h>

#include "../utils/ClientManager.h"
#include <gtest/gtest.h>

using namespace android::resource_policy;
using namespace android;

struct TestClient {
    TestClient(int id, int32_t cost, const std::set<int>& conflictingKeys, int32_t ownerId,
            int32_t score, int32_t state, bool isVendorClient) :
            mId(id), mCost(cost), mConflictingKeys(conflictingKeys),
            mOwnerId(ownerId), mScore(score), mState(state), mIsVendorClient(isVendorClient) {};
    int mId;
    int32_t mCost;    // Int 0..100
    std::set<int> mConflictingKeys;
    int32_t mOwnerId; // PID
    int32_t mScore;   // Priority
    int32_t mState;   // Foreground/background etc
    bool mIsVendorClient;
};

using TestClientDescriptor = ClientDescriptor<int, TestClient>;
using TestDescriptorPtr = std::shared_ptr<TestClientDescriptor>;

TestDescriptorPtr makeDescFromTestClient(const TestClient& tc) {
    return std::make_shared<TestClientDescriptor>(/*ID*/tc.mId, tc, tc.mCost, tc.mConflictingKeys,
            tc.mScore, tc.mOwnerId, tc.mState, tc.mIsVendorClient, /*oomScoreOffset*/0);
}

class TestClientManager : public ClientManager<int, TestClient> {
public:
    TestClientManager() {}
    virtual ~TestClientManager() {}
};


// Test ClientMager behavior when there is only one single owner
// The expected behavior is that if one owner (application or vendor) is trying
// to open second camera, it may succeed or not, but the first opened camera
// should never be evicted.
TEST(ClientManagerTest, SingleOwnerMultipleCamera) {

    TestClientManager cm;
    TestClient cam0Client(/*ID*/0, /*cost*/100, /*conflicts*/{1},
            /*ownerId*/ 1000, PERCEPTIBLE_RECENT_FOREGROUND_APP_ADJ,
            ActivityManager::PROCESS_STATE_PERSISTENT_UI, /*isVendorClient*/ false);
    auto cam0Desc = makeDescFromTestClient(cam0Client);
    auto evicted = cm.addAndEvict(cam0Desc);
    ASSERT_EQ(evicted.size(), 0u) << "Evicted list must be empty";

    TestClient cam1Client(/*ID*/1, /*cost*/100, /*conflicts*/{0},
            /*ownerId*/ 1000, PERCEPTIBLE_RECENT_FOREGROUND_APP_ADJ,
            ActivityManager::PROCESS_STATE_PERSISTENT_UI, /*isVendorClient*/ false);
    auto cam1Desc = makeDescFromTestClient(cam1Client);

    // 1. Check with conflicting devices, new client would be evicted
    auto wouldBeEvicted = cm.wouldEvict(cam1Desc);
    ASSERT_EQ(wouldBeEvicted.size(), 1u) << "Evicted list length must be 1";
    ASSERT_EQ(wouldBeEvicted[0]->getKey(), cam1Desc->getKey()) << "cam1 must be evicted";

    cm.removeAll();

    TestClient cam2Client(/*ID*/2, /*cost*/100, /*conflicts*/{},
            /*ownerId*/ 1000, PERCEPTIBLE_RECENT_FOREGROUND_APP_ADJ,
            ActivityManager::PROCESS_STATE_PERSISTENT_UI, /*isVendorClient*/ false);
    auto cam2Desc = makeDescFromTestClient(cam2Client);
    evicted = cm.addAndEvict(cam2Desc);
    ASSERT_EQ(evicted.size(), 0u) << "Evicted list must be empty";

    TestClient cam3Client(/*ID*/3, /*cost*/100, /*conflicts*/{},
            /*ownerId*/ 1000, PERCEPTIBLE_RECENT_FOREGROUND_APP_ADJ,
            ActivityManager::PROCESS_STATE_PERSISTENT_UI, /*isVendorClient*/ false);
    auto cam3Desc = makeDescFromTestClient(cam3Client);

    // 2. Check without conflicting devices, the pre-existing client won't be evicted
    // In this case, the new client would be granted, but could later be rejected by HAL due to
    // resource cost.
    wouldBeEvicted = cm.wouldEvict(cam3Desc);
    ASSERT_EQ(wouldBeEvicted.size(), 0u) << "Evicted list must be empty";

    cm.removeAll();

    evicted = cm.addAndEvict(cam0Desc);
    ASSERT_EQ(evicted.size(), 0u) << "Evicted list must be empty";

    TestClient cam0ClientNew(/*ID*/0, /*cost*/100, /*conflicts*/{1},
            /*ownerId*/ 1000, PERCEPTIBLE_RECENT_FOREGROUND_APP_ADJ,
            ActivityManager::PROCESS_STATE_PERSISTENT_UI, /*isVendorClient*/ false);
    auto cam0DescNew = makeDescFromTestClient(cam0ClientNew);
    wouldBeEvicted = cm.wouldEvict(cam0DescNew);

    // 3. Check opening the same camera twice will evict the older client
    ASSERT_EQ(wouldBeEvicted.size(), 1u) << "Evicted list length must be 1";
    ASSERT_EQ(wouldBeEvicted[0], cam0Desc) << "cam0 (old) must be evicted";

    // 4. Check that an invalid client (dead process) will be evicted

    cm.removeAll();

    TestClient camDeadClient(/*ID*/ 0, /*cost*/100, /*conflicts*/{},
            /*ownerId*/ 1000, INVALID_ADJ,
            ActivityManager::PROCESS_STATE_NONEXISTENT, /*isVendorClient*/ false);
    auto camDeadDesc = makeDescFromTestClient(camDeadClient);
    evicted = cm.addAndEvict(camDeadDesc);
    wouldBeEvicted = cm.wouldEvict(cam0Desc);

    ASSERT_EQ(evicted.size(), 0u) << "Evicted list must be empty";
    ASSERT_EQ(wouldBeEvicted.size(), 1u) << "Evicted list length must be 1";
    ASSERT_EQ(wouldBeEvicted[0], camDeadDesc) << "dead cam must be evicted";

    // 5. Check that a more important client will win

    TestClient cam0ForegroundClient(/*ID*/0, /*cost*/100, /*conflicts*/{1},
            /*ownerId*/ 1000, FOREGROUND_APP_ADJ,
            ActivityManager::PROCESS_STATE_PERSISTENT_UI, /*isVendorClient*/ false);
    auto cam0FgDesc = makeDescFromTestClient(cam0ForegroundClient);

    cm.removeAll();
    evicted = cm.addAndEvict(cam0Desc);
    wouldBeEvicted = cm.wouldEvict(cam0FgDesc);

    ASSERT_EQ(evicted.size(), 0u);
    ASSERT_EQ(wouldBeEvicted.size(), 1u);
    ASSERT_EQ(wouldBeEvicted[0],cam0Desc) << "less important cam0 must be evicted";
}
