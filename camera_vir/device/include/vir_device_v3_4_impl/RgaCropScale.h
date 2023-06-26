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

#ifndef HAL_ROCKCHIP_PSL_RKISP1_RGACROPSCALE_H_
namespace android {
namespace camera2 {

class RgaCropScale {
 public:
    struct Params {
        /* use share fd if it's valid */
        int fd;
        /* if fd == -1, use virtual address */
        char *vir_addr;
        int offset_x;
        int offset_y;
        int width_stride;
        int height_stride;
        int width;
        int height;
        /* only support NV12,NV21 now */
        int fmt;
        /* just for src params */
        bool mirror;
    };

    static int CropScaleNV12Or21(struct Params* in, struct Params* out);
    static int rga_nv12_scale_crop(
		int src_width, int src_height,
		unsigned long src_fd, unsigned long dst_fd,
		int dst_width, int dst_height,
		int zoom_val, bool mirror, bool isNeedCrop,
		bool isDstNV21, bool is16Align, bool isYuyvFormat);

};

} /* namespace camera2 */
} /* namespace android */

#endif  // HAL_ROCKCHIP_PSL_RKISP1_RGACROPSCALE_H_
