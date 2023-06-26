/*
 * Copyright (c) 2018, Fuzhou Rockchip Electronics Co., Ltd
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

#ifndef ANDROID_HARDWARE_CAMERA_GRALLOC
#define ANDROID_HARDWARE_CAMERA_GRALLOC

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <log/log.h>
#include "utils/Mutex.h"

namespace virtuals {

#define LOGD(msg,...)       ALOGD("%s(%d): " msg ,__FUNCTION__,__LINE__,##__VA_ARGS__)
#define LOGE(msg,...)       ALOGE("%s(%d): " msg ,__FUNCTION__,__LINE__,##__VA_ARGS__)
#define PAGE_ALIGN(x)   (((x) + 0xFFF) & (~0xFFF)) // Set as multiple of 4K

#ifdef __cplusplus
extern "C"
{
#endif

enum cam_mem_type_e {
    CAM_MEM_TYPE_INVALID,
    CAM_MEM_TYPE_ION,
    CAM_MEM_TYPE_IONDMA,
    CAM_MEM_TYPE_GRALLOC,
};

enum cam_mem_flag_e {
    CAM_MEM_FLAG_HW_WRITE	= 0x1,
    CAM_MEM_FLAG_HW_READ	= 0x2,
    CAM_MEM_FLAG_SW_WRITE	= 0x4,
    CAM_MEM_FLAG_SW_READ	= 0x8,
};

typedef struct cam_mem_handle_s {
    enum cam_mem_type_e mem_type;
    int iommu_enabled;
    int phy_continuos;
    int camsys_fd;
    unsigned int flag;
    void* priv;
}cam_mem_handle_t;

typedef struct cam_mem_info_s {
    cam_mem_handle_t* handlle;
    unsigned long vir_addr;
    unsigned long phy_addr;
    unsigned long mmu_addr;
    int iommu_maped;
    size_t size;
    int fd;
    void* priv;
}cam_mem_info_t;

typedef struct cam_mem_ops_s {
    //init
    cam_mem_handle_t* (*init)(int iommu_enabled, unsigned int mem_flag, int phy_continuos);
    //alloc
    cam_mem_info_t* (*alloc)(cam_mem_handle_t* handle,size_t size);
    //free
    int (*free)(cam_mem_handle_t* handle, cam_mem_info_t* mem);
    //flush cache
    int (*flush_cache)(cam_mem_handle_t* handle, cam_mem_info_t* mem);
    //deinit
    int (*deInit)(cam_mem_handle_t* handle);
}cam_mem_ops_t;

cam_mem_ops_t* get_cam_ops_vir(enum cam_mem_type_e mem_type);

struct cam_mem_ops_des_s{
    const char* name;
    enum cam_mem_type_e mem_type;
    cam_mem_ops_t* ops;
};

#ifdef __cplusplus
}
#endif

}//namespace virtuals
#endif // ANDROID_HARDWARE_CAMERA_GRALLOC

