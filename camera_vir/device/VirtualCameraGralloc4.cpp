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

#define LOG_TAG "VirCamGraBuf"
//#define LOG_NDEBUG 0
#include <log/log.h>
#include <utils/threads.h>
#include <utils/Log.h>
#include <ui/GraphicBufferAllocator.h>
#include <ui/GraphicBufferMapper.h>
#include <ui/GraphicBuffer.h>
#include <linux/videodev2.h>
#include "VirtualCameraGralloc4.h"

#include <hwbinder/IPCThreadState.h>
#include <sync/sync.h>
#include <drm_fourcc.h>


#include <hidl/ServiceManagement.h>

#include <inttypes.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wzero-length-array"
#include <sync/sync.h>
#pragma clang diagnostic pop

using namespace android;
namespace virtuals {

#define RK_GRALLOC_USAGE_SPECIFY_STRIDE 1ULL << 30

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


using android::hardware::graphics::mapper::V4_0::Error;
using android::hardware::graphics::mapper::V4_0::IMapper;
using android::hardware::graphics::allocator::V4_0::IAllocator;
using android::hardware::graphics::common::V1_2::BufferUsage;
using android::hardware::graphics::mapper::V4_0::BufferDescriptor;
using android::hardware::hidl_vec;

using android::gralloc4::MetadataType_PlaneLayouts;
using android::gralloc4::decodePlaneLayouts;
using android::gralloc4::MetadataType_Usage;
using android::gralloc4::decodeUsage;
using android::gralloc4::MetadataType_PlaneLayouts;
using android::gralloc4::decodePlaneLayouts;
using android::gralloc4::MetadataType_PixelFormatFourCC;
using android::gralloc4::decodePixelFormatFourCC;
using android::gralloc4::MetadataType_PixelFormatModifier;
using android::gralloc4::decodePixelFormatModifier;
using android::gralloc4::MetadataType_PixelFormatRequested;
using android::gralloc4::decodePixelFormatRequested;
using android::gralloc4::MetadataType_AllocationSize;
using android::gralloc4::decodeAllocationSize;
using android::gralloc4::MetadataType_LayerCount;
using android::gralloc4::decodeLayerCount;
using android::gralloc4::MetadataType_Dataspace;
using android::gralloc4::decodeDataspace;
using android::gralloc4::MetadataType_Crop;
using android::gralloc4::decodeCrop;
using android::gralloc4::MetadataType_Width;
using android::gralloc4::decodeWidth;
using android::gralloc4::MetadataType_Height;
using android::gralloc4::decodeHeight;
using ::android::Mutex;

using aidl::android::hardware::graphics::common::Dataspace;
using aidl::android::hardware::graphics::common::PlaneLayout;
using aidl::android::hardware::graphics::common::ExtendableType;
using aidl::android::hardware::graphics::common::PlaneLayout;
using aidl::android::hardware::graphics::common::PlaneLayoutComponentType;
namespace VirCamGralloc4 {
#define IMPORTBUFFER_CB 1


static constexpr Error kTransactionError = Error::NO_RESOURCES;

uint64_t getValidUsageBits() {
    static const uint64_t validUsageBits = []() -> uint64_t {
        uint64_t bits = 0;
        for (const auto bit :
             hardware::hidl_enum_range<hardware::graphics::common::V1_2::BufferUsage>()) {
            bits = bits | bit;
        }
        return bits;
    }();
    return validUsageBits;
}

static inline IMapper::Rect sGralloc4Rect(const Rect& rect) {
    IMapper::Rect outRect{};
    outRect.left = rect.left;
    outRect.top = rect.top;
    outRect.width = rect.width();
    outRect.height = rect.height();
    return outRect;
}

static inline void sBufferDescriptorInfo(std::string name, uint32_t width, uint32_t height,
                                         PixelFormat format, uint32_t layerCount, uint64_t usage,
                                         IMapper::BufferDescriptorInfo* outDescriptorInfo) {
    outDescriptorInfo->name = name;
    outDescriptorInfo->width = width;
    outDescriptorInfo->height = height;
    outDescriptorInfo->layerCount = layerCount;
    outDescriptorInfo->format = static_cast<hardware::graphics::common::V1_2::PixelFormat>(format);
    outDescriptorInfo->usage = usage;
    outDescriptorInfo->reservedSize = 0;
}




/*#defined in hardware/rockchip/libgralloc/bifrost/src/hidl_common/MapperMetadata.h*/
#define GRALLOC_ARM_METADATA_TYPE_NAME "arm.graphics.ArmMetadataType"
const static IMapper::MetadataType ArmMetadataType_PLANE_FDS
{
	GRALLOC_ARM_METADATA_TYPE_NAME,
	// static_cast<int64_t>(aidl::arm::graphics::ArmMetadataType::PLANE_FDS)
    1   // 'PLANE_FDS'
};

static IMapper &get_mapperservice()
{
    static android::sp<IMapper> cached_service = IMapper::getService();
    return *cached_service;
}

static IAllocator &get_allocservice()
{
    static android::sp<IAllocator> cached_service = IAllocator::getService();
    return *cached_service;
}

template <typename T>
static int get_metadata(IMapper &mapper, buffer_handle_t handle, IMapper::MetadataType type,
                        android::status_t (*decode)(const hidl_vec<uint8_t> &, T *), T *value)
{
	void *handle_arg = const_cast<native_handle_t *>(handle);
	assert(handle_arg);
	assert(value);
	assert(decode);

	int err = 0;
	mapper.get(handle_arg, type, [&err, value, decode](Error error, const hidl_vec<uint8_t> &metadata)
	            {
		            if (error != Error::NONE)
		            {
			            err = android::BAD_VALUE;
			            return;
		            }
		            err = decode(metadata, value);
		        });
	return err;
}

android::status_t static decodeArmPlaneFds(const hidl_vec<uint8_t>& input, std::vector<int64_t>* fds)
{
    assert (fds != nullptr);
    int64_t size = 0;

    memcpy(&size, input.data(), sizeof(int64_t));
    if (size < 0)
    {
        return android::BAD_VALUE;
    }

    fds->resize(size);

    const uint8_t *tmp = input.data() + sizeof(int64_t);
    memcpy(fds->data(), tmp, sizeof(int64_t) * size);

    return android::NO_ERROR;
}



int get_width(buffer_handle_t handle, uint64_t* width)
{
    auto &mapper = get_mapperservice();

    int err = get_metadata(mapper, handle, MetadataType_Width, decodeWidth, width);
    if (err != android::OK)
    {
        LOGE("err : %d", err);
    }

    return err;
}

int get_height(buffer_handle_t handle, uint64_t* height)
{
    auto &mapper = get_mapperservice();

    int err = get_metadata(mapper, handle, MetadataType_Height, decodeHeight, height);
    if (err != android::OK)
    {
        LOGE("err : %d", err);
    }

    return err;
}

status_t validateBufferDescriptorInfo(
        IMapper::BufferDescriptorInfo* descriptorInfo) {
    uint64_t validUsageBits = getValidUsageBits();

    if (descriptorInfo->usage & ~validUsageBits) {
        ALOGE("buffer descriptor contains invalid usage bits 0x%" PRIx64,
              descriptorInfo->usage & ~validUsageBits);
        return android::BAD_VALUE;
    }
    return android::NO_ERROR;
}

status_t createDescriptor(void* bufferDescriptorInfo,
                                          void* outBufferDescriptor) {
    IMapper::BufferDescriptorInfo* descriptorInfo =
            static_cast<IMapper::BufferDescriptorInfo*>(bufferDescriptorInfo);
    BufferDescriptor* outDescriptor = static_cast<BufferDescriptor*>(outBufferDescriptor);

    status_t status = validateBufferDescriptorInfo(descriptorInfo);
    if (status != android::NO_ERROR) {
        return status;
    }

    Error error;
    auto hidl_cb = [&](const auto& tmpError, const auto& tmpDescriptor) {
        error = tmpError;
        if (error != Error::NONE) {
            return;
        }
        *outDescriptor = tmpDescriptor;
    };

    auto &mapper = get_mapperservice();
    android::hardware::Return<void> ret = mapper.createDescriptor(*descriptorInfo, hidl_cb);

    return static_cast<status_t>((ret.isOk()) ? error : kTransactionError);
}

int vir_lock(buffer_handle_t bufferHandle,
                                  uint32_t flags,
                                  uint32_t left,
                                  uint32_t top,
                                  uint32_t width,
                                  uint32_t height,
                                  void** out_addr) {


    auto &mapper = get_mapperservice();
    auto buffer = const_cast<native_handle_t*>(bufferHandle);

    IMapper::Rect accessRegion = {(int)left, (int)top, (int)width, (int)height};

    android::hardware::hidl_handle acquireFenceHandle; // dummy

    Error error;
    auto ret = mapper.lock(buffer,
                           flags,
                           accessRegion,
                           acquireFenceHandle,
                           [&](const auto& tmpError, const auto& tmpData) {
                                error = tmpError;
                                if (error != Error::NONE) {
                                    return;
                                }
                                *out_addr = tmpData;
                           });

    error = (ret.isOk()) ? error : kTransactionError;

    ALOGE_IF(error != Error::NONE, "lock(%p, ...) failed: %d", bufferHandle, error);

    return (int)error;

}

int vir_unlock(buffer_handle_t bufferHandle) {

    auto &mapper = get_mapperservice();
    auto buffer = const_cast<native_handle_t*>(bufferHandle);

    int releaseFence = -1;
    Error error;
    auto ret = mapper.unlock(buffer,
                             [&](const auto& tmpError, const auto& tmpReleaseFence)
                             {
        error = tmpError;
        if (error != Error::NONE) {
            return;
        }

        auto fenceHandle = tmpReleaseFence.getNativeHandle(); // 预期 unlock() 不会返回有效的 release_fence.
        if (fenceHandle && fenceHandle->numFds == 1)
        {
            ALOGE("got unexpected valid fd of release_fence : %d", fenceHandle->data[0]);

            int fd = dup(fenceHandle->data[0]);
            if (fd >= 0) {
                releaseFence = fd;
            } else {
                ALOGE("failed to dup unlock release fence");
                sync_wait(fenceHandle->data[0], -1);
            }
        }
                             });

    if (!ret.isOk()) {
        error = kTransactionError;
    }

    if (error != Error::NONE) {
        ALOGE("unlock(%p) failed with %d", bufferHandle, error);
    }

    return 0;
}

int get_usage(buffer_handle_t handle, uint64_t* usage)
{
    auto &mapper = get_mapperservice();

    int err = get_metadata(mapper, handle, MetadataType_Usage, decodeUsage, usage);
    if (err != android::OK)
    {
        LOGE("Failed to get pixel_format_requested. err : %d", err);
        return err;
    }

    return err;
}

int get_allocation_size(buffer_handle_t handle, uint64_t* allocation_size)
{
    auto &mapper = get_mapperservice();

    int err = get_metadata(mapper, handle, MetadataType_AllocationSize, decodeAllocationSize, allocation_size);
    if (err != android::OK)
    {
        LOGE("Failed to get allocation_size. err : %d", err);
        return err;
    }

    return err;
}

status_t vir_importBuffer(buffer_handle_t rawHandle,
                                      buffer_handle_t* outBufferHandle) {
    ALOGV("import aa rawBuffer :%p", rawHandle);
    Error error;
    auto &mapper = get_mapperservice();
    auto ret = mapper.importBuffer(android::hardware::hidl_handle(rawHandle), [&](const auto& tmpError, const auto& tmpBuffer) {
        error = tmpError;
        if (error != Error::NONE) {
            return;
        }
        *outBufferHandle = static_cast<buffer_handle_t>(tmpBuffer);
        ALOGV("import bb outBuffer :%p", outBufferHandle);
    });

    return static_cast<status_t>((ret.isOk()) ? error : kTransactionError);
}

status_t freeBuffer(buffer_handle_t bufferHandle) {
    ALOGV("freeBuffer %p", bufferHandle);
    auto buffer = const_cast<native_handle_t*>(bufferHandle);
    auto &mapper = get_mapperservice();
    auto ret = mapper.freeBuffer(buffer);

    auto error = (ret.isOk()) ? static_cast<Error>(ret) : kTransactionError;
    ALOGE_IF(error != Error::NONE, "freeBuffer(%p) failed with %d", buffer, error);
    return static_cast<status_t>((ret.isOk()) ? error : kTransactionError);
}

int get_share_fd(buffer_handle_t buffer, int* share_fd) {
    ALOGV(" buffer:%p", buffer);
    int fd = -1;
    int err = 0;
    Mutex mLock;

    Mutex::Autolock _l(mLock);
    {
        auto &mapper = get_mapperservice();
        std::vector<int64_t> fds;

        err = get_metadata(mapper, buffer, ArmMetadataType_PLANE_FDS, decodeArmPlaneFds, &fds);
        if (err != android::OK)
        {
            ALOGE("Failed to get plane_fds. err : %d", err);
            return err;
        }
        assert (fds.size() > 0);

        *share_fd = (int)(fds[0]);
    }
    return err;
}
} // namespace VirCamGralloc4

static int allocate_gralloc_buffer(size_t width,
                                                   size_t height,
                                                   uint32_t format,
                                                   uint32_t usage,
                                                   buffer_handle_t* out_buffer,
                                                   uint32_t* out_stride) {
    ALOGV("AllocateGrallocBuffer %d, %d, %d, %d", width, height, format, usage);
    Mutex mLock;
    Mutex::Autolock _l(mLock);

    IMapper::BufferDescriptorInfo descriptorInfo;
    VirCamGralloc4::sBufferDescriptorInfo("ExternalAllocateBuffer", width, height, (PixelFormat)format, 1/*layerCount*/, usage, &descriptorInfo);

    BufferDescriptor descriptor;
    status_t error = VirCamGralloc4::createDescriptor(static_cast<void*>(&descriptorInfo),
                                              static_cast<void*>(&descriptor));
    if (error != android::NO_ERROR) {
        return error;
    }

    int bufferCount = 1;
    auto &allocator = VirCamGralloc4::get_allocservice();
    auto ret = allocator.allocate(descriptor, bufferCount,
                                    [&](const auto& tmpError, const auto& tmpStride,
                                        const auto& tmpBuffers) {
                                        error = static_cast<status_t>(tmpError);
                                        if (tmpError != Error::NONE) {
                                            return;
                                        }

                                #if IMPORTBUFFER_CB == 1
                                            for (uint32_t i = 0; i < bufferCount; i++) {
                                                error = VirCamGralloc4::vir_importBuffer(tmpBuffers[i],
                                                                             out_buffer);
                                                if (error != android::NO_ERROR) {
                                                    for (uint32_t j = 0; j < i; j++) {
                                                        VirCamGralloc4::freeBuffer(*out_buffer);
                                                        *out_buffer = nullptr;
                                                    }
                                                    return;
                                                }
                                            }
                                #else
                                            for (uint32_t i = 0; i < bufferCount; i++) {
                                                *out_buffer = native_handle_clone(
                                                        tmpBuffers[i].getNativeHandle());
                                                if (!out_buffer) {
                                                    for (uint32_t j = 0; j < i; j++) {
                                                        //auto buffer = const_cast<native_handle_t*>(
                                                        //        out_buffer);
                                                        native_handle_close(out_buffer);
                                                        native_handle_delete(out_buffer);
                                                        *out_buffer = nullptr;
                                                    }
                                                }
                                            }
                                #endif
                                        *out_stride = tmpStride;
                                    });

    if (!ret.isOk())
        return -EINVAL;

    ALOGV("AllocateGrallocBuffer %p", *out_buffer);
#if 0
    buffer_context->usage = 1;
    buffer_context_[*out_buffer] = std::move(buffer_context);
#endif
    // make sure the kernel driver sees BC_FREE_BUFFER and closes the fds now
    android::hardware::IPCThreadState::self()->flushCommands();

    return (ret.isOk()) ? error : static_cast<status_t>(VirCamGralloc4::kTransactionError);
}

static cam_mem_handle_t*  cam_mem_gralloc_ops_init_vir(
        int iommu_enabled, unsigned int mem_flag, int phy_continuos)
{
    int ret = 0;
    cam_mem_handle_t* handle = NULL;
    handle = (cam_mem_handle_t*)malloc(sizeof(cam_mem_handle_t));
    if (!handle) {
        LOGE("%s:can't alloc handle!",__FUNCTION__);
        goto init_error;
    }
    memset(handle, 0x0, sizeof(*handle));

    handle->mem_type = CAM_MEM_TYPE_GRALLOC;
    handle->iommu_enabled = iommu_enabled;
    handle->phy_continuos = phy_continuos;

    if (mem_flag & CAM_MEM_FLAG_HW_WRITE)
        handle->flag |= GRALLOC_USAGE_HW_CAMERA_WRITE;
    if (mem_flag & CAM_MEM_FLAG_HW_READ)
        handle->flag |= GRALLOC_USAGE_HW_CAMERA_READ;
    if (mem_flag & CAM_MEM_FLAG_SW_WRITE)
        handle->flag |= GRALLOC_USAGE_SW_WRITE_OFTEN;
    if (mem_flag & CAM_MEM_FLAG_SW_READ)
        handle->flag |= GRALLOC_USAGE_SW_READ_OFTEN;
    handle->flag |= RK_GRALLOC_USAGE_SPECIFY_STRIDE;
    return handle;
init_error:
    if (!handle)
        free(handle);
    return NULL;
}

//alloc GraphicBuffer
static cam_mem_info_t* cam_mem_gralloc_ops_alloc_vir(
        cam_mem_handle_t* handle, size_t size,
        uint32_t width, uint32_t height)
{
    int ret;
    unsigned int grallocFlags = 0;
    unsigned int halPixFmt;
    void* mem_addr = NULL;
    cam_mem_info_t* mem = NULL;
    buffer_handle_t buf_handle;
    uint32_t stride = 0;
    int fd = -1;
    uint64_t allocation_size;

    if (!handle) {
        LOGE("invalid ion mem handle!");
        return NULL;
    }

    mem = (cam_mem_info_t*)malloc(sizeof(cam_mem_info_t));
    if (!mem) {
        LOGE("can't alloc cam_mem_info_t!");
        goto  error_alloc;
    }

    //halPixFmt = HAL_PIXEL_FORMAT_RGB_565;
    halPixFmt = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
    grallocFlags = handle->flag;

    ret = allocate_gralloc_buffer(width,height, halPixFmt, grallocFlags, &buf_handle, &stride);
    if (ret) {
        LOGE("alloc buffer error : %s", strerror(errno));
        goto error_alloc;
    }

    ret = VirCamGralloc4::vir_lock(
                buf_handle,
                grallocFlags,
                0,
                0,
                width,
                height,
                (void**)&mem_addr);
    if (ret) {
        LOGE("lock buffer error : %s", strerror(errno));
        goto lock_error;
    }

    VirCamGralloc4::vir_unlock(buf_handle);

    ret = VirCamGralloc4::get_allocation_size(buf_handle, &allocation_size);
    ALOGV("alloc buffer size(%lld)", allocation_size);

    ret = VirCamGralloc4::get_share_fd(buf_handle, &fd);
    if (ret) {
        LOGE("get share fd error : %s", strerror(errno));
        goto lock_error;
    }

    mem->vir_addr = (unsigned long)mem_addr;
    mem->handlle = handle;
    mem->iommu_maped = 0;
    mem->mmu_addr = 0;
    mem->phy_addr = 0;
    mem->size = size;
    mem->priv = (void*)buf_handle;
    mem->fd = fd;
    mem->width = width;
    mem->height = height;

    ALOGV("alloc graphic buffer sucess,mem %p, vir_addr %p, fd %d",
        mem, mem_addr, mem->fd);

    return mem;
lock_error:
    VirCamGralloc4::freeBuffer(buf_handle);
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
    buffer_handle_t buf_handle;

    if (!handle || !mem) {
        LOGE("invalid ion mem handle!");
        return -1;
    }

    if (mem->iommu_maped) {
        LOGE("ion mem is mmumaped, should be unmapped firstly!");
        return -1;
    }
    buf_handle = (buffer_handle_t)(mem->priv);
#if IMPORTBUFFER_CB == 1
    if (buf_handle) {
        VirCamGralloc4::freeBuffer(buf_handle);
        buf_handle = nullptr;
    }
#else
    if (buf_handle) {
        auto abuffer = const_cast<native_handle_t*>(
                buf_handle);
        native_handle_close(abuffer);
        native_handle_delete(abuffer);
        buf_handle = nullptr;
    }
#endif

    free(mem);

    return ret;
}

//flush cache
static int cam_mem_gralloc_ops_flush_cache_vir(
        cam_mem_handle_t* handle, cam_mem_info_t* mem,
        uint32_t width, uint32_t height)
{
    struct dma_buf_sync sync_args;
    int ret = 0;
    void* mem_addr;

    buffer_handle_t* buf_handle = (buffer_handle_t*)(mem->priv);
    if (!handle || !mem) {
        LOGE("invalid ion mem handle!");
        return -1;
    }

    ret = VirCamGralloc4::vir_lock(
                *buf_handle,
                handle->flag,
                0,
                0,
                width,
                height,
                (void**)&mem_addr);
    if (ret) {
        LOGE("lock buffer error : %s", strerror(errno));
        return -1;
    }

    sync_args.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;
    ret = ioctl(mem->fd, DMA_BUF_IOCTL_SYNC, &sync_args);
    if (ret != 0)
        LOGE("ret %d ,DMA_BUF_IOCTL_SYNC failed!", ret);
    VirCamGralloc4::vir_unlock(*buf_handle);

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

cam_mem_ops_t g_rk_gralloc_mem_ops {
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

static struct cam_mem_ops_des_s cam_mem_ops_array[] = {
    {"ion",CAM_MEM_TYPE_ION,NULL},
    {"ionDma",CAM_MEM_TYPE_IONDMA,NULL},
    {"gralloc",CAM_MEM_TYPE_GRALLOC,&g_rk_gralloc_mem_ops},
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
        return cam_mem_ops_array[ops_index].ops;
    else
        return NULL;
}

}//namespace virtuals
