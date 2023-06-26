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

#define LOG_TAG "CamGraBuf"
#define LOG_NDEBUG 0
#include <log/log.h>
#include <utils/threads.h>
#include <utils/Log.h>
#include <ui/GraphicBufferAllocator.h>
#include <ui/GraphicBufferMapper.h>
#include <ui/GraphicBuffer.h>
#include <linux/videodev2.h>
#include "VirtualCameraGralloc.h"

using namespace android;
namespace virtuals {
struct dma_buf_sync {
    __u64 flags;
};

#define DMA_BUF_SYNC_READ      (1 << 0)
#define DMA_BUF_SYNC_WRITE     (2 << 0)
#define DMA_BUF_SYNC_RW        (DMA_BUF_SYNC_READ | DMA_BUF_SYNC_WRITE)
#define DMA_BUF_SYNC_START     (0 << 2)
#define DMA_BUF_SYNC_END       (1 << 2)
#define DMA_BUF_SYNC_VALID_FLAGS_MASK \
        (DMA_BUF_SYNC_RW | DMA_BUF_SYNC_END)
#define DMA_BUF_BASE            'b'
#define DMA_BUF_IOCTL_SYNC      _IOW(DMA_BUF_BASE, 0, struct dma_buf_sync)

static cam_mem_handle_t*  cam_mem_gralloc_ops_init_vir(
        int iommu_enabled, unsigned int mem_flag, int phy_continuos)
{
    int ret = 0;
    const hw_module_t *allocMod = NULL;
    gralloc_module_t const* gm;
    cam_mem_handle_t* handle = NULL;
    ret= hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &allocMod);
    if (ret == 0)
        gm = reinterpret_cast<gralloc_module_t const *>(allocMod);
    else
        goto init_error;
    handle = (cam_mem_handle_t*)malloc(sizeof(cam_mem_handle_t));
    if (!handle) {
        LOGE("%s:can't alloc handle!",__FUNCTION__);
        goto init_error;
    }
    memset(handle, 0x0, sizeof(*handle));

    handle->mem_type = CAM_MEM_TYPE_GRALLOC;
    handle->iommu_enabled = iommu_enabled;
    handle->phy_continuos = phy_continuos;
    handle->priv = (void*)gm;

    if (mem_flag & CAM_MEM_FLAG_HW_WRITE)
        handle->flag |= GRALLOC_USAGE_HW_CAMERA_WRITE;
    if (mem_flag & CAM_MEM_FLAG_HW_READ)
        handle->flag |= GRALLOC_USAGE_HW_CAMERA_READ;
    if (mem_flag & CAM_MEM_FLAG_SW_WRITE)
        handle->flag |= GRALLOC_USAGE_SW_WRITE_OFTEN;
    if (mem_flag & CAM_MEM_FLAG_SW_READ)
        handle->flag |= GRALLOC_USAGE_SW_READ_OFTEN;

    return handle;
init_error:
    if (!handle)
        free(handle);
    return NULL;
}

//alloc GraphicBuffer
static cam_mem_info_t* cam_mem_gralloc_ops_alloc_vir(
        cam_mem_handle_t* handle, size_t size)
{
    int ret;
    unsigned int grallocFlags = 0;
    unsigned int halPixFmt;
    void* mem_addr = NULL;
    GraphicBuffer* mgraphicbuf;
    cam_mem_info_t* mem = NULL;
    gralloc_module_t* mGrallocModule;
    ALOGE("cam_mem_gralloc_ops_alloc_vir");
    if (!handle) {
        LOGE("invalid ion mem handle!");
        return NULL;
    }
    mGrallocModule = (gralloc_module_t*)(handle->priv);

    mem = (cam_mem_info_t*)malloc(sizeof(cam_mem_info_t));
    if (!mem) {
        LOGE("can't alloc cam_mem_info_t!");
        goto  error_alloc;
    }
    halPixFmt = HAL_PIXEL_FORMAT_RGB_565;
    // use rgb565 format to alloce buffer size, so size should be divided 2

    grallocFlags = handle->flag;
    mgraphicbuf = new GraphicBuffer(size/2, 1, halPixFmt,grallocFlags);
    mgraphicbuf->incStrong(mgraphicbuf);
    if (mgraphicbuf->initCheck()) {
        LOGE("GraphicBuffer error : %s", strerror(errno));
        goto error_alloc;
    }

    ret = mgraphicbuf->lock(grallocFlags, (void**)&mem_addr);
    if (ret) {
        LOGE("lock buffer error : %s", strerror(errno));
        goto lock_error;
    }
    mgraphicbuf->unlock();
    ret = mGrallocModule->perform(
        mGrallocModule,
        GRALLOC_MODULE_PERFORM_GET_HADNLE_PRIME_FD,
        mgraphicbuf->handle,
        &mem->fd);
    if (ret) {
        LOGE("get handle error : %s", strerror(errno));
        goto lock_error;
    }

    mem->vir_addr = (unsigned long)mem_addr;
    mem->handlle = handle;
    mem->iommu_maped = 0;
    mem->mmu_addr = 0;
    mem->phy_addr = 0;
    mem->size = size;
    mem->priv = (void*)(mgraphicbuf);

    LOGD("alloc graphic buffer sucess,mem %p, vir_addr %p, fd %d",
        mem, mem_addr, mem->fd);

    return mem;
lock_error:
    //delete mgraphicbuf;
    mgraphicbuf->decStrong(mgraphicbuf);
    mgraphicbuf = NULL;
error_alloc:
    if (mem)
        free(mem);
    return NULL;
}

//free
static int cam_mem_gralloc_ops_free_vir(
        cam_mem_handle_t* handle, cam_mem_info_t* mem)
{
    int ret = 0;
    GraphicBuffer* mgraphicbuf;

    if (!handle || !mem) {
        LOGE("invalid ion mem handle!");
        return -1;
    }

    if (mem->iommu_maped) {
        LOGE("ion mem is mmumaped, should be unmapped firstly!");
        return -1;
    }
    mgraphicbuf = (GraphicBuffer*)(mem->priv);
    mgraphicbuf->decStrong(mgraphicbuf);
    free(mem);

    return ret;
}

//flush cache
static int cam_mem_gralloc_ops_flush_cache_vir(
        cam_mem_handle_t* handle, cam_mem_info_t* mem)
{
    struct dma_buf_sync sync_args;
    int ret = 0;
    void* mem_addr;

    GraphicBuffer* mgraphicbuf = (GraphicBuffer*)(mem->priv);
    if (!handle || !mem) {
        LOGE("invalid ion mem handle!");
        return -1;
    }

    ret = mgraphicbuf->lock(handle->flag, (void**)&mem_addr);
    if (ret) {
        LOGE("lock buffer error : %s",strerror(errno));
        return -1;
    }

    sync_args.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;
    ret = ioctl(mem->fd, DMA_BUF_IOCTL_SYNC, &sync_args);
    if (ret != 0)
        LOGE("ret %d ,DMA_BUF_IOCTL_SYNC failed!", ret);
    mgraphicbuf->unlock();

    return ret;
}

//deinit
static int cam_mem_gralloc_ops_deInit_vir(cam_mem_handle_t* handle)
{
    int ret = 0;

    if (!handle) {
        LOGE("invalid ion mem handle!");
        return -1;
    }
    free(handle);

    return ret;
}

cam_mem_ops_t g_rk_gralloc_mem_ops_vir {
    //init
    .init = cam_mem_gralloc_ops_init_vir,
    //alloc
    .alloc = cam_mem_gralloc_ops_alloc_vir,
    //free
    .free = cam_mem_gralloc_ops_free_vir,
    //flush cache
    .flush_cache = cam_mem_gralloc_ops_flush_cache_vir,
    //deinit
    .deInit = cam_mem_gralloc_ops_deInit_vir,
};

static struct cam_mem_ops_des_s cam_mem_ops_array_vir[] = {
    {"ion",CAM_MEM_TYPE_ION,NULL},
    {"ionDma",CAM_MEM_TYPE_IONDMA,NULL},
    {"gralloc",CAM_MEM_TYPE_GRALLOC,&g_rk_gralloc_mem_ops_vir},
};

cam_mem_ops_t* get_cam_ops_vir(enum cam_mem_type_e mem_type)
{
    int ops_index = -1;
    switch(mem_type) {
        case CAM_MEM_TYPE_ION:
        ops_index = 0;
    break;
    case CAM_MEM_TYPE_IONDMA:
        ops_index = 1;
    break;
    case CAM_MEM_TYPE_GRALLOC:
        ops_index = 2;
    break;
    default:
        ops_index = -1;
    break;
    }

    if (ops_index != -1)
        return cam_mem_ops_array_vir[ops_index].ops;
    else
        return NULL;
}

//}namespace virtuals