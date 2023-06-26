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

#ifndef ANDROID_SERVERS_ENABLE_HFR_TRACES_H_
#define ANDROID_SERVERS_ENABLE_HFR_TRACES_H_

#include <utils/Trace.h>

#ifdef HFR_ENABLE_TRACING
#define ATRACE_HFR_CALL() ATRACE_CALL()
#else
#define ATRACE_HFR_CALL()
#endif

#endif
