/*
 * Copyright (C) 2018 The Android Open Source Project
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
#define LOG_TAG "VirCamUtils@3.4"
//#define LOG_NDEBUG 0
#include <log/log.h>

#include <cmath>
#include <cstring>
#include <sys/mman.h>
#include <linux/videodev2.h>

#define HAVE_JPEG // required for libyuv.h to export MJPEG decode APIs
#include <libyuv.h>

#include <jpeglib.h>

#include "VirtualCameraUtils_3.4.h"

#include "VirtualCameraGralloc4.h"

namespace {

buffer_handle_t sEmptyBuffer = nullptr;

} // Anonymous namespace

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace V3_4 {
namespace virtuals{
namespace implementation {

Frame::Frame(uint32_t width, uint32_t height, uint32_t fourcc) :
        mWidth(width), mHeight(height), mFourcc(fourcc) {}

V4L2Frame::V4L2Frame(
        uint32_t w, uint32_t h, uint32_t fourcc,
        int bufIdx, int fd, uint32_t dataSize, uint64_t offset) :
        Frame(w, h, fourcc),
        mBufferIndex(bufIdx), mFd(fd), mDataSize(dataSize), mOffset(offset) {

        }

int V4L2Frame::map(uint8_t** data, size_t* dataSize) {
    //ALOGE("map");
    if (data == nullptr || dataSize == nullptr) {
        ALOGI("%s: V4L2 buffer map bad argument: data %p, dataSize %p",
                __FUNCTION__, data, dataSize);
        return -EINVAL;
    }

    std::lock_guard<std::mutex> lk(mLock);
    if (!mMapped) {
#if 1
        void* addr = mmap(NULL, mDataSize, PROT_READ, MAP_SHARED, mFd, mOffset);

        if (addr == MAP_FAILED) {
            ALOGE("%s: V4L2 buffer map failed: %s", __FUNCTION__, strerror(errno));
            return -EINVAL;
        }
#else
        void* addr = nullptr;
#endif
        //ALOGE("camerabuffer...");
        mData = static_cast<uint8_t*>(addr);
        mMapped = true;
    }
    *data = mData;
    *dataSize = mDataSize;
    ALOGV("%s: V4L map FD %d, data %p size %zu", __FUNCTION__, mFd, mData, mDataSize);
    return 0;
}

int V4L2Frame::unmap() {
    std::lock_guard<std::mutex> lk(mLock);
    if (mMapped) {
        ALOGV("%s: V4L unmap data %p size %zu", __FUNCTION__, mData, mDataSize);
#if 1
        if (munmap(mData, mDataSize) != 0) {
            ALOGE("%s: V4L2 buffer unmap failed: %s", __FUNCTION__, strerror(errno));
            return -EINVAL;
        }
#endif
        mMapped = false;

    }
    return 0;
}

V4L2Frame::~V4L2Frame() {
    unmap();
}

int V4L2Frame::getData(uint8_t** outData, size_t* dataSize) {
    return map(outData, dataSize);
}

AllocatedFrame::AllocatedFrame(
        uint32_t w, uint32_t h) :
        Frame(w, h, V4L2_PIX_FMT_YUV420) {};

AllocatedFrame::~AllocatedFrame() {}

int AllocatedFrame::allocate(YCbCrLayout* out) {
    std::lock_guard<std::mutex> lk(mLock);
    if ((mWidth % 2) || (mHeight % 2)) {
        ALOGE("%s: bad dimension %dx%d (not multiple of 2)", __FUNCTION__, mWidth, mHeight);
        return -EINVAL;
    }

    uint32_t dataSize = mWidth * mHeight * 3 / 2; // YUV420
    if (mData.size() != dataSize) {
        mData.resize(dataSize);
    }

    if (out != nullptr) {
        out->y = mData.data();
        out->yStride = mWidth;
        uint8_t* cbStart = mData.data() + mWidth * mHeight;
        uint8_t* crStart = cbStart + mWidth * mHeight / 4;
        out->cb = cbStart;
        out->cr = crStart;
        out->cStride = mWidth / 2;
        out->chromaStep = 1;
    }
    return 0;
}

int AllocatedFrame::getData(uint8_t** outData, size_t* dataSize) {
    YCbCrLayout layout;
    int ret = allocate(&layout);
    if (ret != 0) {
        return ret;
    }
    *outData = mData.data();
    *dataSize = mData.size();
    return 0;
}

int AllocatedFrame::getLayout(YCbCrLayout* out) {
    IMapper::Rect noCrop = {0, 0,
            static_cast<int32_t>(mWidth),
            static_cast<int32_t>(mHeight)};
    return getCroppedLayout(noCrop, out);
}

int AllocatedFrame::getCroppedLayout(const IMapper::Rect& rect, YCbCrLayout* out) {
    if (out == nullptr) {
        ALOGE("%s: null out", __FUNCTION__);
        return -1;
    }

    std::lock_guard<std::mutex> lk(mLock);
    if ((rect.left + rect.width) > static_cast<int>(mWidth) ||
        (rect.top + rect.height) > static_cast<int>(mHeight) ||
            (rect.left % 2) || (rect.top % 2) || (rect.width % 2) || (rect.height % 2)) {
        ALOGE("%s: bad rect left %d top %d w %d h %d", __FUNCTION__,
                rect.left, rect.top, rect.width, rect.height);
        return -1;
    }

    out->y = mData.data() + mWidth * rect.top + rect.left;
    out->yStride = mWidth;
    uint8_t* cbStart = mData.data() + mWidth * mHeight;
    uint8_t* crStart = cbStart + mWidth * mHeight / 4;
    out->cb = cbStart + mWidth * rect.top / 4 + rect.left / 2;
    out->cr = crStart + mWidth * rect.top / 4 + rect.left / 2;
    out->cStride = mWidth / 2;
    out->chromaStep = 1;
    return 0;
}

bool isAspectRatioClose(float ar1, float ar2) {
    const float kAspectRatioMatchThres = 0.025f; // This threshold is good enough to distinguish
                                                // 4:3/16:9/20:9
                                                // 1.33 / 1.78 / 2
    return (std::abs(ar1 - ar2) < kAspectRatioMatchThres);
}

double SupportedV4L2Format::FrameRate::getDouble() const {
    return durationDenominator / static_cast<double>(durationNumerator);
}

::android::hardware::camera::common::V1_0::Status importBufferImpl(
        /*inout*/std::map<int, CirculatingBuffers>& circulatingBuffers,
        /*inout*/HandleImporter& handleImporter,
        int32_t streamId,
        uint64_t bufId, buffer_handle_t buf,
        /*out*/buffer_handle_t** outBufPtr,
        bool allowEmptyBuf) {
    using ::android::hardware::camera::common::V1_0::Status;
    if (buf == nullptr && bufId == BUFFER_ID_NO_BUFFER) {
        if (allowEmptyBuf) {
            *outBufPtr = &sEmptyBuffer;
            return Status::OK;
        } else {
            ALOGE("%s: bufferId %" PRIu64 " has null buffer handle!", __FUNCTION__, bufId);
            return Status::ILLEGAL_ARGUMENT;
        }
    }

    CirculatingBuffers& cbs = circulatingBuffers[streamId];
    if (cbs.count(bufId) == 0) {
        if (buf == nullptr) {
            ALOGE("%s: bufferId %" PRIu64 " has null buffer handle!", __FUNCTION__, bufId);
            return Status::ILLEGAL_ARGUMENT;
        }
        // Register a newly seen buffer
        buffer_handle_t importedBuf = buf;
        handleImporter.importBuffer(importedBuf);
        if (importedBuf == nullptr) {
            ALOGE("%s: output buffer for stream %d is invalid!", __FUNCTION__, streamId);
            return Status::INTERNAL_ERROR;
        } else {
            cbs[bufId] = importedBuf;
        }
    }
    *outBufPtr = &cbs[bufId];
    return Status::OK;
}

uint32_t getFourCcFromLayout(const YCbCrLayout& layout) {
    intptr_t cb = reinterpret_cast<intptr_t>(layout.cb);
    intptr_t cr = reinterpret_cast<intptr_t>(layout.cr);
    if (std::abs(cb - cr) == 1 && layout.chromaStep == 2) {
        // Interleaved format
        if (layout.cb > layout.cr) {
            return V4L2_PIX_FMT_NV21;
        } else {
            return V4L2_PIX_FMT_NV12;
        }
    } else if (layout.chromaStep == 1) {
        // Planar format
        if (layout.cb > layout.cr) {
            return V4L2_PIX_FMT_YVU420; // YV12
        } else {
            return V4L2_PIX_FMT_YUV420; // YU12
        }
    } else {
        return FLEX_YUV_GENERIC;
    }
}

int getCropRect(
        CroppingType ct, const Size& inSize, const Size& outSize, IMapper::Rect* out) {
    if (out == nullptr) {
        ALOGE("%s: out is null", __FUNCTION__);
        return -1;
    }

    uint32_t inW = inSize.width;
    uint32_t inH = inSize.height;
    uint32_t outW = outSize.width;
    uint32_t outH = outSize.height;

    // Handle special case where aspect ratio is close to input but scaled
    // dimension is slightly larger than input
    float arIn = ASPECT_RATIO(inSize);
    float arOut = ASPECT_RATIO(outSize);
    if (isAspectRatioClose(arIn, arOut)) {
        out->left = 0;
        out->top = 0;
        out->width = inW;
        out->height = inH;
        return 0;
    }

    if (ct == VERTICAL) {
        uint64_t scaledOutH = static_cast<uint64_t>(outH) * inW / outW;
        if (scaledOutH > inH) {
            ALOGE("%s: Output size %dx%d cannot be vertically cropped from input size %dx%d",
                    __FUNCTION__, outW, outH, inW, inH);
            return -1;
        }
        scaledOutH = scaledOutH & ~0x1; // make it multiple of 2

        out->left = 0;
        out->top = ((inH - scaledOutH) / 2) & ~0x1;
        out->width = inW;
        out->height = static_cast<int32_t>(scaledOutH);
        ALOGV("%s: crop %dx%d to %dx%d: top %d, scaledH %d",
                __FUNCTION__, inW, inH, outW, outH, out->top, static_cast<int32_t>(scaledOutH));
    } else {
        uint64_t scaledOutW = static_cast<uint64_t>(outW) * inH / outH;
        if (scaledOutW > inW) {
            ALOGE("%s: Output size %dx%d cannot be horizontally cropped from input size %dx%d",
                    __FUNCTION__, outW, outH, inW, inH);
            return -1;
        }
        scaledOutW = scaledOutW & ~0x1; // make it multiple of 2

        out->left = ((inW - scaledOutW) / 2) & ~0x1;
        out->top = 0;
        out->width = static_cast<int32_t>(scaledOutW);
        out->height = inH;
        ALOGV("%s: crop %dx%d to %dx%d: top %d, scaledW %d",
                __FUNCTION__, inW, inH, outW, outH, out->top, static_cast<int32_t>(scaledOutW));
    }

    return 0;
}

int formatConvert(
        const YCbCrLayout& in, const YCbCrLayout& out, Size sz, uint32_t format) {
    int ret = 0;
    switch (format) {
        case V4L2_PIX_FMT_NV21:
            ret = libyuv::I420ToNV21(
                    static_cast<uint8_t*>(in.y),
                    in.yStride,
                    static_cast<uint8_t*>(in.cb),
                    in.cStride,
                    static_cast<uint8_t*>(in.cr),
                    in.cStride,
                    static_cast<uint8_t*>(out.y),
                    out.yStride,
                    static_cast<uint8_t*>(out.cr),
                    out.cStride,
                    sz.width,
                    sz.height);
            if (ret != 0) {
                ALOGE("%s: convert to NV21 buffer failed! ret %d",
                            __FUNCTION__, ret);
                return ret;
            }
            break;
        case V4L2_PIX_FMT_NV12:
            ret = libyuv::I420ToNV12(
                    static_cast<uint8_t*>(in.y),
                    in.yStride,
                    static_cast<uint8_t*>(in.cb),
                    in.cStride,
                    static_cast<uint8_t*>(in.cr),
                    in.cStride,
                    static_cast<uint8_t*>(out.y),
                    out.yStride,
                    static_cast<uint8_t*>(out.cb),
                    out.cStride,
                    sz.width,
                    sz.height);
            if (ret != 0) {
                ALOGE("%s: convert to NV12 buffer failed! ret %d",
                            __FUNCTION__, ret);
                return ret;
            }
            break;
        case V4L2_PIX_FMT_YVU420: // YV12
        case V4L2_PIX_FMT_YUV420: // YU12
            // TODO: maybe we can speed up here by somehow save this copy?
            ret = libyuv::I420Copy(
                    static_cast<uint8_t*>(in.y),
                    in.yStride,
                    static_cast<uint8_t*>(in.cb),
                    in.cStride,
                    static_cast<uint8_t*>(in.cr),
                    in.cStride,
                    static_cast<uint8_t*>(out.y),
                    out.yStride,
                    static_cast<uint8_t*>(out.cb),
                    out.cStride,
                    static_cast<uint8_t*>(out.cr),
                    out.cStride,
                    sz.width,
                    sz.height);
            if (ret != 0) {
                ALOGE("%s: copy to YV12 or YU12 buffer failed! ret %d",
                            __FUNCTION__, ret);
                return ret;
            }
            break;
        case FLEX_YUV_GENERIC:
            // TODO: b/72261744 write to arbitrary flexible YUV layout. Slow.
            ALOGE("%s: unsupported flexible yuv layout"
                    " y %p cb %p cr %p y_str %d c_str %d c_step %d",
                    __FUNCTION__, out.y, out.cb, out.cr,
                    out.yStride, out.cStride, out.chromaStep);
            return -1;
        default:
            ALOGE("%s: unknown YUV format 0x%x!", __FUNCTION__, format);
            return -1;
    }
    return 0;
}

int encodeJpegYU12(
        const Size & inSz, const YCbCrLayout& inLayout,
        int jpegQuality, const void *app1Buffer, size_t app1Size,
        void *out, const size_t maxOutSize, size_t &actualCodeSize)
{
    /* libjpeg is a C library so we use C-style "inheritance" by
     * putting libjpeg's jpeg_destination_mgr first in our custom
     * struct. This allows us to cast jpeg_destination_mgr* to
     * CustomJpegDestMgr* when we get it passed to us in a callback */
    struct CustomJpegDestMgr {
        struct jpeg_destination_mgr mgr;
        JOCTET *mBuffer;
        size_t mBufferSize;
        size_t mEncodedSize;
        bool mSuccess;
    } dmgr;

    jpeg_compress_struct cinfo = {};
    jpeg_error_mgr jerr;

    /* Initialize error handling with standard callbacks, but
     * then override output_message (to print to ALOG) and
     * error_exit to set a flag and print a message instead
     * of killing the whole process */
    cinfo.err = jpeg_std_error(&jerr);

    cinfo.err->output_message = [](j_common_ptr cinfo) {
        char buffer[JMSG_LENGTH_MAX];

        /* Create the message */
        (*cinfo->err->format_message)(cinfo, buffer);
        ALOGE("libjpeg error: %s", buffer);
    };
    cinfo.err->error_exit = [](j_common_ptr cinfo) {
        (*cinfo->err->output_message)(cinfo);
        if(cinfo->client_data) {
            auto & dmgr =
                *reinterpret_cast<CustomJpegDestMgr*>(cinfo->client_data);
            dmgr.mSuccess = false;
        }
    };
    /* Now that we initialized some callbacks, let's create our compressor */
    jpeg_create_compress(&cinfo);

    /* Initialize our destination manager */
    dmgr.mBuffer = static_cast<JOCTET*>(out);
    dmgr.mBufferSize = maxOutSize;
    dmgr.mEncodedSize = 0;
    dmgr.mSuccess = true;
    cinfo.client_data = static_cast<void*>(&dmgr);

    /* These lambdas become C-style function pointers and as per C++11 spec
     * may not capture anything */
    dmgr.mgr.init_destination = [](j_compress_ptr cinfo) {
        auto & dmgr = reinterpret_cast<CustomJpegDestMgr&>(*cinfo->dest);
        dmgr.mgr.next_output_byte = dmgr.mBuffer;
        dmgr.mgr.free_in_buffer = dmgr.mBufferSize;
        ALOGV("%s:%d jpeg start: %p [%zu]",
              __FUNCTION__, __LINE__, dmgr.mBuffer, dmgr.mBufferSize);
    };

    dmgr.mgr.empty_output_buffer = [](j_compress_ptr cinfo __unused) {
        ALOGV("%s:%d Out of buffer", __FUNCTION__, __LINE__);
        return 0;
    };

    dmgr.mgr.term_destination = [](j_compress_ptr cinfo) {
        auto & dmgr = reinterpret_cast<CustomJpegDestMgr&>(*cinfo->dest);
        dmgr.mEncodedSize = dmgr.mBufferSize - dmgr.mgr.free_in_buffer;
        ALOGV("%s:%d Done with jpeg: %zu", __FUNCTION__, __LINE__, dmgr.mEncodedSize);
    };
    cinfo.dest = reinterpret_cast<struct jpeg_destination_mgr*>(&dmgr);

    /* We are going to be using JPEG in raw data mode, so we are passing
     * straight subsampled planar YCbCr and it will not touch our pixel
     * data or do any scaling or anything */
    cinfo.image_width = inSz.width;
    cinfo.image_height = inSz.height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_YCbCr;

    /* Initialize defaults and then override what we want */
    jpeg_set_defaults(&cinfo);

    jpeg_set_quality(&cinfo, jpegQuality, 1);
    jpeg_set_colorspace(&cinfo, JCS_YCbCr);
    cinfo.raw_data_in = 1;
    cinfo.dct_method = JDCT_IFAST;

    /* Configure sampling factors. The sampling factor is JPEG subsampling 420
     * because the source format is YUV420. Note that libjpeg sampling factors
     * are... a little weird. Sampling of Y=2,U=1,V=1 means there is 1 U and
     * 1 V value for each 2 Y values */
    cinfo.comp_info[0].h_samp_factor = 2;
    cinfo.comp_info[0].v_samp_factor = 2;
    cinfo.comp_info[1].h_samp_factor = 1;
    cinfo.comp_info[1].v_samp_factor = 1;
    cinfo.comp_info[2].h_samp_factor = 1;
    cinfo.comp_info[2].v_samp_factor = 1;

    /* Let's not hardcode YUV420 in 6 places... 5 was enough */
    int maxVSampFactor = std::max( {
        cinfo.comp_info[0].v_samp_factor,
        cinfo.comp_info[1].v_samp_factor,
        cinfo.comp_info[2].v_samp_factor
    });
    int cVSubSampling = cinfo.comp_info[0].v_samp_factor /
                        cinfo.comp_info[1].v_samp_factor;

    /* Start the compressor */
    jpeg_start_compress(&cinfo, TRUE);

    /* Compute our macroblock height, so we can pad our input to be vertically
     * macroblock aligned.
     * TODO: Does it need to be horizontally MCU aligned too? */

    size_t mcuV = DCTSIZE*maxVSampFactor;
    size_t paddedHeight = mcuV * ((inSz.height + mcuV - 1) / mcuV);

    /* libjpeg uses arrays of row pointers, which makes it really easy to pad
     * data vertically (unfortunately doesn't help horizontally) */
    std::vector<JSAMPROW> yLines (paddedHeight);
    std::vector<JSAMPROW> cbLines(paddedHeight/cVSubSampling);
    std::vector<JSAMPROW> crLines(paddedHeight/cVSubSampling);

    uint8_t *py = static_cast<uint8_t*>(inLayout.y);
    uint8_t *pcr = static_cast<uint8_t*>(inLayout.cr);
    uint8_t *pcb = static_cast<uint8_t*>(inLayout.cb);

    for(uint32_t i = 0; i < paddedHeight; i++)
    {
        /* Once we are in the padding territory we still point to the last line
         * effectively replicating it several times ~ CLAMP_TO_EDGE */
        int li = std::min(i, inSz.height - 1);
        yLines[i]  = static_cast<JSAMPROW>(py + li * inLayout.yStride);
        if(i < paddedHeight / cVSubSampling)
        {
            li = std::min(i, (inSz.height - 1) / cVSubSampling);
            crLines[i] = static_cast<JSAMPROW>(pcr + li * inLayout.cStride);
            cbLines[i] = static_cast<JSAMPROW>(pcb + li * inLayout.cStride);
        }
    }

    /* If APP1 data was passed in, use it */
    if(app1Buffer && app1Size)
    {
        jpeg_write_marker(&cinfo, JPEG_APP0 + 1,
             static_cast<const JOCTET*>(app1Buffer), app1Size);
    }

    /* While we still have padded height left to go, keep giving it one
     * macroblock at a time. */
    while (cinfo.next_scanline < cinfo.image_height) {
        const uint32_t batchSize = DCTSIZE * maxVSampFactor;
        const uint32_t nl = cinfo.next_scanline;
        JSAMPARRAY planes[3]{ &yLines[nl],
                              &cbLines[nl/cVSubSampling],
                              &crLines[nl/cVSubSampling] };

        uint32_t done = jpeg_write_raw_data(&cinfo, planes, batchSize);

        if (done != batchSize) {
            ALOGE("%s: compressed %u lines, expected %u (total %u/%u)",
              __FUNCTION__, done, batchSize, cinfo.next_scanline,
              cinfo.image_height);
            return -1;
        }
    }

    /* This will flush everything */
    jpeg_finish_compress(&cinfo);

    /* Grab the actual code size and set it */
    actualCodeSize = dmgr.mEncodedSize;

    return 0;
}

Size getMaxThumbnailResolution(const common::V1_0::helper::CameraMetadata& chars) {
    Size thumbSize { 0, 0 };
    camera_metadata_ro_entry entry =
        chars.find(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES);
    for(uint32_t i = 0; i < entry.count; i += 2) {
        Size sz { static_cast<uint32_t>(entry.data.i32[i]),
                  static_cast<uint32_t>(entry.data.i32[i+1]) };
        if(sz.width * sz.height > thumbSize.width * thumbSize.height) {
            thumbSize = sz;
        }
    }

    if (thumbSize.width * thumbSize.height == 0) {
        ALOGW("%s: non-zero thumbnail size not available", __FUNCTION__);
    }

    return thumbSize;
}

void freeReleaseFences(hidl_vec<V3_2::CaptureResult>& results) {
    for (auto& result : results) {
        if (result.inputBuffer.releaseFence.getNativeHandle() != nullptr) {
            native_handle_t* handle = const_cast<native_handle_t*>(
                    result.inputBuffer.releaseFence.getNativeHandle());
            native_handle_close(handle);
            native_handle_delete(handle);
        }
        for (auto& buf : result.outputBuffers) {
            if (buf.releaseFence.getNativeHandle() != nullptr) {
                native_handle_t* handle = const_cast<native_handle_t*>(
                        buf.releaseFence.getNativeHandle());
                native_handle_close(handle);
                native_handle_delete(handle);
            }
        }
    }
    return;
}

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define UPDATE(md, tag, data, size)               \
do {                                              \
    if ((md).update((tag), (data), (size))) {     \
        ALOGE("Update " #tag " failed!");         \
        return BAD_VALUE;                         \
    }                                             \
} while (0)

status_t fillCaptureResultCommon(
        common::V1_0::helper::CameraMetadata &md, nsecs_t timestamp,
        camera_metadata_ro_entry& activeArraySize) {
    if (activeArraySize.count < 4) {
        ALOGE("%s: cannot find active array size!", __FUNCTION__);
        return -EINVAL;
    }
    // android.control
    // For USB camera, we don't know the AE state. Set the state to converged to
    // indicate the frame should be good to use. Then apps don't have to wait the
    // AE state.
    const uint8_t aeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
    UPDATE(md, ANDROID_CONTROL_AE_STATE, &aeState, 1);

    const uint8_t ae_lock = ANDROID_CONTROL_AE_LOCK_OFF;
    UPDATE(md, ANDROID_CONTROL_AE_LOCK, &ae_lock, 1);

    // Set AWB state to converged to indicate the frame should be good to use.
    const uint8_t awbState = ANDROID_CONTROL_AWB_STATE_CONVERGED;
    UPDATE(md, ANDROID_CONTROL_AWB_STATE, &awbState, 1);

    const uint8_t awbLock = ANDROID_CONTROL_AWB_LOCK_OFF;
    UPDATE(md, ANDROID_CONTROL_AWB_LOCK, &awbLock, 1);

    const uint8_t flashState = ANDROID_FLASH_STATE_UNAVAILABLE;
    UPDATE(md, ANDROID_FLASH_STATE, &flashState, 1);

    // This means pipeline latency of X frame intervals. The maximum number is 4.
    const uint8_t requestPipelineMaxDepth = 4;
    UPDATE(md, ANDROID_REQUEST_PIPELINE_DEPTH, &requestPipelineMaxDepth, 1);

    // android.scaler
    const int32_t crop_region[] = {
          activeArraySize.data.i32[0], activeArraySize.data.i32[1],
          activeArraySize.data.i32[2], activeArraySize.data.i32[3],
    };
    UPDATE(md, ANDROID_SCALER_CROP_REGION, crop_region, ARRAY_SIZE(crop_region));

    // android.sensor
    UPDATE(md, ANDROID_SENSOR_TIMESTAMP, &timestamp, 1);

    // android.statistics
    const uint8_t lensShadingMapMode = ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF;
    UPDATE(md, ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, &lensShadingMapMode, 1);

    const uint8_t sceneFlicker = ANDROID_STATISTICS_SCENE_FLICKER_NONE;
    UPDATE(md, ANDROID_STATISTICS_SCENE_FLICKER, &sceneFlicker, 1);

    return OK;
}

#undef ARRAY_SIZE
#undef UPDATE

}  // namespace implementation
}  // namespace virtuals
}  // namespace V3_4


namespace V3_6 {
namespace virtuals {
namespace implementation {

AllocatedV4L2Frame::AllocatedV4L2Frame(sp<V3_4::virtuals::implementation::V4L2Frame> frameIn) :
        Frame(frameIn->mWidth, frameIn->mHeight, frameIn->mFourcc) {
    uint8_t* dataIn;
    size_t dataSize;
    if (frameIn->getData(&dataIn, &dataSize) != 0) {
        ALOGE("%s: map input V4L2 frame failed!", __FUNCTION__);
        return;
    }

    mData.resize(dataSize);
    std::memcpy(mData.data(), dataIn, dataSize);
}

int AllocatedV4L2Frame::getData(uint8_t** outData, size_t* dataSize) {
    if (outData == nullptr || dataSize == nullptr) {
        ALOGE("%s: outData(%p)/dataSize(%p) must not be null", __FUNCTION__, outData, dataSize);
        return -1;
    }

    *outData = mData.data();
    *dataSize = mData.size();
    return 0;
}

AllocatedV4L2Frame::~AllocatedV4L2Frame() {}

}  // namespace implementation
}  // namespace virtuals
}  // namespace V3_6
}  // namespace device


namespace virtuals {
namespace common {

namespace {
    const int kDefaultCameraIdOffset = 200;
    const int kDefaultJpegBufSize = 5 << 20; // 5MB
    const int kDefaultNumVideoBuffer = 4;
    const int kDefaultNumStillBuffer = 2;
    const int kDefaultOrientation = 0; // suitable for natural landscape displays like tablet/TV
                                       // For phone devices 270 is better
} // anonymous namespace

const char* VirtualCameraConfig::kDefaultCfgPath = "/vendor/etc/virtual_camera_config.xml";

VirtualCameraConfig VirtualCameraConfig::loadFromCfg(const char* cfgPath) {
    using namespace tinyxml2;
    VirtualCameraConfig ret;

    XMLDocument configXml;
    XMLError err = configXml.LoadFile(cfgPath);
    if (err != XML_SUCCESS) {
        ALOGE("%s: Unable to load virtual camera config file '%s'. Error: %s",
                __FUNCTION__, cfgPath, XMLDocument::ErrorIDToName(err));
        return ret;
    } else {
        ALOGI("%s: load virtual camera config succeed!", __FUNCTION__);
    }

    XMLElement *extCam = configXml.FirstChildElement("VirtualCamera");
    if (extCam == nullptr) {
        ALOGI("%s: no virtual camera config specified", __FUNCTION__);
        return ret;
    }

    XMLElement *providerCfg = extCam->FirstChildElement("Provider");
    if (providerCfg == nullptr) {
        ALOGI("%s: no virtual camera provider config specified", __FUNCTION__);
        return ret;
    }

    XMLElement *cameraIdOffset = providerCfg->FirstChildElement("CameraIdOffset");
    if (cameraIdOffset != nullptr) {
        ret.cameraIdOffset = std::atoi(cameraIdOffset->GetText());
    }

    XMLElement *ignore = providerCfg->FirstChildElement("ignore");
    if (ignore == nullptr) {
        ALOGI("%s: no internal ignored device specified", __FUNCTION__);
        return ret;
    }

    XMLElement *id = ignore->FirstChildElement("id");
    while (id != nullptr) {
        const char* text = id->GetText();
        if (text != nullptr) {
            ret.mInternalDevices.insert(text);
            ALOGI("%s: device %s will be ignored by camera provider",
                    __FUNCTION__, text);
        }
        id = id->NextSiblingElement("id");
    }

    XMLElement *deviceCfg = extCam->FirstChildElement("Device");
    if (deviceCfg == nullptr) {
        ALOGI("%s: no camera device config specified", __FUNCTION__);
        return ret;
    }

    XMLElement *jpegBufSz = deviceCfg->FirstChildElement("MaxJpegBufferSize");
    if (jpegBufSz == nullptr) {
        ALOGI("%s: no max jpeg buffer size specified", __FUNCTION__);
    } else {
        ret.maxJpegBufSize = jpegBufSz->UnsignedAttribute("bytes", /*Default*/kDefaultJpegBufSize);
    }

    XMLElement *numVideoBuf = deviceCfg->FirstChildElement("NumVideoBuffers");
    if (numVideoBuf == nullptr) {
        ALOGI("%s: no num video buffers specified", __FUNCTION__);
    } else {
        ret.numVideoBuffers =
                numVideoBuf->UnsignedAttribute("count", /*Default*/kDefaultNumVideoBuffer);
    }

    XMLElement *numStillBuf = deviceCfg->FirstChildElement("NumStillBuffers");
    if (numStillBuf == nullptr) {
        ALOGI("%s: no num still buffers specified", __FUNCTION__);
    } else {
        ret.numStillBuffers =
                numStillBuf->UnsignedAttribute("count", /*Default*/kDefaultNumStillBuffer);
    }

    XMLElement *fpsList = deviceCfg->FirstChildElement("FpsList");
    if (fpsList == nullptr) {
        ALOGI("%s: no fps list specified", __FUNCTION__);
    } else {
        if (!updateFpsList(fpsList, ret.fpsLimits)) {
            return ret;
        }
    }

    XMLElement *depth = deviceCfg->FirstChildElement("Depth16Supported");
    if (depth == nullptr) {
        ret.depthEnabled = false;
        ALOGI("%s: depth output is not enabled", __FUNCTION__);
    } else {
        ret.depthEnabled = depth->BoolAttribute("enabled", false);
    }

    if(ret.depthEnabled) {
        XMLElement *depthFpsList = deviceCfg->FirstChildElement("DepthFpsList");
        if (depthFpsList == nullptr) {
            ALOGW("%s: no depth fps list specified", __FUNCTION__);
        } else {
            if(!updateFpsList(depthFpsList, ret.depthFpsLimits)) {
                return ret;
            }
        }
    }

    XMLElement *minStreamSize = deviceCfg->FirstChildElement("MinimumStreamSize");
    if (minStreamSize == nullptr) {
       ALOGI("%s: no minimum stream size specified", __FUNCTION__);
    } else {
        ret.minStreamSize = {
                minStreamSize->UnsignedAttribute("width", /*Default*/0),
                minStreamSize->UnsignedAttribute("height", /*Default*/0)};
    }

    XMLElement *orientation = deviceCfg->FirstChildElement("Orientation");
    if (orientation == nullptr) {
        ALOGI("%s: no sensor orientation specified", __FUNCTION__);
    } else {
        ret.orientation = orientation->IntAttribute("degree", /*Default*/kDefaultOrientation);
    }


    XMLElement *Sensor = deviceCfg->FirstChildElement("Sensor");
    if (Sensor == nullptr) {
        ALOGI("%s: no sensor specified", __FUNCTION__);
    } else {
        const char* name = Sensor->Attribute("name");
        ret.snsName =std::string(name);
        ALOGI("@%s: snsName:%s",__FUNCTION__,ret.snsName.c_str());
    }
    ALOGI("%s: camera cfg loaded: maxJpgBufSize %d,"
            " num video buffers %d, num still buffers %d, orientation %d",
            __FUNCTION__, ret.maxJpegBufSize,
            ret.numVideoBuffers, ret.numStillBuffers, ret.orientation);
    for (const auto& limit : ret.fpsLimits) {
        ALOGI("%s: fpsLimitList: %dx%d@%f", __FUNCTION__,
                limit.size.width, limit.size.height, limit.fpsUpperBound);
    }
    for (const auto& limit : ret.depthFpsLimits) {
        ALOGI("%s: depthFpsLimitList: %dx%d@%f", __FUNCTION__, limit.size.width, limit.size.height,
              limit.fpsUpperBound);
    }
    ALOGI("%s: minStreamSize: %dx%d" , __FUNCTION__,
         ret.minStreamSize.width, ret.minStreamSize.height);
    return ret;
}

bool VirtualCameraConfig::updateFpsList(tinyxml2::XMLElement* fpsList,
        std::vector<FpsLimitation>& fpsLimits) {
    using namespace tinyxml2;
    std::vector<FpsLimitation> limits;
    XMLElement* row = fpsList->FirstChildElement("Limit");
    while (row != nullptr) {
        FpsLimitation prevLimit{{0, 0}, 1000.0};
        FpsLimitation limit;
        limit.size = {row->UnsignedAttribute("width", /*Default*/ 0),
                      row->UnsignedAttribute("height", /*Default*/ 0)};
        limit.fpsUpperBound = row->DoubleAttribute("fpsBound", /*Default*/ 1000.0);
        if (limit.size.width <= prevLimit.size.width ||
            limit.size.height <= prevLimit.size.height ||
            limit.fpsUpperBound >= prevLimit.fpsUpperBound) {
            ALOGE(
                "%s: FPS limit list must have increasing size and decreasing fps!"
                " Prev %dx%d@%f, Current %dx%d@%f",
                __FUNCTION__, prevLimit.size.width, prevLimit.size.height, prevLimit.fpsUpperBound,
                limit.size.width, limit.size.height, limit.fpsUpperBound);
            return false;
        }
        limits.push_back(limit);
        row = row->NextSiblingElement("Limit");
    }
    fpsLimits = limits;
    return true;
}

VirtualCameraConfig::VirtualCameraConfig() :
        cameraIdOffset(kDefaultCameraIdOffset),
        maxJpegBufSize(kDefaultJpegBufSize),
        numVideoBuffers(kDefaultNumVideoBuffer),
        numStillBuffers(kDefaultNumStillBuffer),
        depthEnabled(false),
        orientation(kDefaultOrientation) {
    fpsLimits.push_back({/*Size*/{ 640,  480}, /*FPS upper bound*/30.0});
    fpsLimits.push_back({/*Size*/{1280,  720}, /*FPS upper bound*/7.5});
    fpsLimits.push_back({/*Size*/{1920, 1080}, /*FPS upper bound*/5.0});
    minStreamSize = {0, 0};
}


}  // namespace common
}  // namespace virtuals
}  // namespace camera
}  // namespace hardware
}  // namespace android
