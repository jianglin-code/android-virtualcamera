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

#include "RgaCropScale.h"
#include <utils/Singleton.h>
#include <RockchipRga.h>

namespace android {
namespace camera2 {

#if (defined(TARGET_RK32) || defined(TARGET_RK3368))
#define RGA_VER (2.0)
#define RGA_ACTIVE_W (4096)
#define RGA_VIRTUAL_W (4096)
#define RGA_ACTIVE_H (4096)
#define RGA_VIRTUAL_H (4096)

#else
#define RGA_VER (1.0)
#define RGA_ACTIVE_W (2048)
#define RGA_VIRTUAL_W (4096)
#define RGA_ACTIVE_H (2048)
#define RGA_VIRTUAL_H (2048)

#endif

int RgaCropScale::CropScaleNV12Or21(struct Params* in, struct Params* out)
{
    rga_info_t src, dst;

    memset(&src, 0, sizeof(rga_info_t));
    memset(&dst, 0, sizeof(rga_info_t));

    if (!in || !out)
        return -1;

    if((out->width > RGA_VIRTUAL_W) || (out->height > RGA_VIRTUAL_H)){
        ALOGE("%s(%d): out wxh %dx%d beyond rga capability",
            __FUNCTION__, __LINE__,
            out->width, out->height);
        return -1;
    }

    if ((in->fmt != HAL_PIXEL_FORMAT_YCrCb_NV12 &&
        in->fmt != HAL_PIXEL_FORMAT_YCrCb_420_SP) ||
        (out->fmt != HAL_PIXEL_FORMAT_YCrCb_NV12 &&
        out->fmt != HAL_PIXEL_FORMAT_YCrCb_420_SP)) {
        ALOGE("%s(%d): only accept NV12 or NV21 now. in fmt %d, out fmt %d",
            __FUNCTION__, __LINE__,
            in->fmt, out->fmt);
        return -1;
    }
    RockchipRga& rkRga(RockchipRga::get());

    if (in->fd == -1) {
        src.fd = -1;
        src.virAddr = (void*)in->vir_addr;
    } else {
        src.fd = in->fd;
    }
    src.mmuFlag = ((2 & 0x3) << 4) | 1 | (1 << 8) | (1 << 10);

    if (out->fd == -1 ) {
        dst.fd = -1;
        dst.virAddr = (void*)out->vir_addr;
    } else {
        dst.fd = out->fd;
    }
    dst.mmuFlag = ((2 & 0x3) << 4) | 1 | (1 << 8) | (1 << 10);

    rga_set_rect(&src.rect,
                in->offset_x,
                in->offset_y,
                in->width,
                in->height,
                in->width_stride,
                in->height_stride,
                in->fmt);

    rga_set_rect(&dst.rect,
                out->offset_x,
                out->offset_y,
                out->width,
                out->height,
                out->width_stride,
                out->height_stride,
                out->fmt);
    if (in->mirror)
        src.rotation = DRM_RGA_TRANSFORM_FLIP_H;

    if (rkRga.RkRgaBlit(&src, &dst, NULL)) {
        ALOGE("%s:rga blit failed", __FUNCTION__);
        return -1;
    }

    return 0;
}

int RgaCropScale::rga_nv12_scale_crop(
		int src_width, int src_height,
		unsigned long src_fd, unsigned long dst_fd,
		int dst_width, int dst_height,
		int zoom_val, bool mirror, bool isNeedCrop,
		bool isDstNV21, bool is16Align, bool isYuyvFormat)
{
    int ret = 0;
    rga_info_t src,dst;
    int zoom_cropW,zoom_cropH;
    int ratio = 0;
    int zoom_top_offset=0,zoom_left_offset=0;

    RockchipRga& rkRga(RockchipRga::get());

    memset(&src, 0, sizeof(rga_info_t));
    if (isYuyvFormat) {
        src.fd = -1;
        src.virAddr = (void*)src_fd;
    } else {
        src.fd = src_fd;
    }
    src.mmuFlag = ((2 & 0x3) << 4) | 1 | (1 << 8) | (1 << 10);
    memset(&dst, 0, sizeof(rga_info_t));
    dst.fd = dst_fd;
    dst.mmuFlag = ((2 & 0x3) << 4) | 1 | (1 << 8) | (1 << 10);

    if((dst_width > RGA_VIRTUAL_W) || (dst_height > RGA_VIRTUAL_H)){
        ALOGE("(dst_width > RGA_VIRTUAL_W) || (dst_height > RGA_VIRTUAL_H), switch to arm ");
        ret = -1;
        goto END;
    }

    //need crop ? when cts FOV,don't crop
    if(isNeedCrop && (src_width*100/src_height) != (dst_width*100/dst_height)) {
        ratio = ((src_width*100/dst_width) >= (src_height*100/dst_height))?
                (src_height*100/dst_height):
                (src_width*100/dst_width);
        zoom_cropW = (ratio*dst_width/100) & (~0x01);
        zoom_cropH = (ratio*dst_height/100) & (~0x01);
        zoom_left_offset=((src_width-zoom_cropW)>>1) & (~0x01);
        zoom_top_offset=((src_height-zoom_cropH)>>1) & (~0x01);
    }else{
        zoom_cropW = src_width;
        zoom_cropH = src_height;
        zoom_left_offset=0;
        zoom_top_offset=0;
    }

    if(zoom_val > 100){
        zoom_cropW = zoom_cropW*100/zoom_val & (~0x01);
        zoom_cropH = zoom_cropH*100/zoom_val & (~0x01);
        zoom_left_offset = ((src_width-zoom_cropW)>>1) & (~0x01);
        zoom_top_offset= ((src_height-zoom_cropH)>>1) & (~0x01);
    }

    //usb camera height align to 16,the extra eight rows need to be croped.
    if(!is16Align){
        zoom_top_offset = zoom_top_offset & (~0x07);
    }

    rga_set_rect(&src.rect, zoom_left_offset, zoom_top_offset,
                zoom_cropW, zoom_cropH, src_width,
                src_height, HAL_PIXEL_FORMAT_YCrCb_NV12);
    if (isDstNV21)
        rga_set_rect(&dst.rect, 0, 0, dst_width, dst_height,
                    dst_width, dst_height,
                    HAL_PIXEL_FORMAT_YCrCb_420_SP);
    else
        rga_set_rect(&dst.rect, 0,0,dst_width,dst_height,
                    dst_width,dst_height,
                    HAL_PIXEL_FORMAT_YCrCb_NV12);

    if (mirror)
        src.rotation = DRM_RGA_TRANSFORM_FLIP_H;
    //TODO:sina,cosa,scale_mode,render_mode
    ret = rkRga.RkRgaBlit(&src, &dst, NULL);
    if (ret) {
        ALOGE("%s:rga blit failed", __FUNCTION__);
        goto END;
    }

    END:
    return ret;
}

} /* namespace camera2 */
} /* namespace android */
