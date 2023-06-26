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

#define LOG_NDEBUG 0
#define LOG_TAG "Test_NV12Compressor"

#include "NV12Compressor.h"

#include <libexif/exif-data.h>
#include <netinet/in.h>

using namespace android;
using namespace android::camera3;

namespace std {
template <>
struct default_delete<ExifEntry> {
    inline void operator()(ExifEntry* entry) const { exif_entry_unref(entry); }
};

template <>
struct default_delete<ExifData> {
    inline void operator()(ExifData* data) const { exif_data_unref(data); }
};

}  // namespace std

bool NV12Compressor::compress(const unsigned char* data, int width, int height, int quality) {
    if (!configureCompressor(width, height, quality)) {
        // the method will have logged a more detailed error message than we can
        // provide here so just return.
        return false;
    }

    return compressData(data, /*exifData*/ nullptr);
}

bool NV12Compressor::compressWithExifOrientation(const unsigned char* data, int width, int height,
        int quality, android::camera3::ExifOrientation exifValue) {
    std::unique_ptr<ExifData> exifData(exif_data_new());
    if (exifData.get() == nullptr) {
        return false;
    }

    exif_data_set_option(exifData.get(), EXIF_DATA_OPTION_FOLLOW_SPECIFICATION);
    exif_data_set_data_type(exifData.get(), EXIF_DATA_TYPE_COMPRESSED);
    exif_data_set_byte_order(exifData.get(), EXIF_BYTE_ORDER_INTEL);
    std::unique_ptr<ExifEntry> exifEntry(exif_entry_new());
    if (exifEntry.get() ==  nullptr) {
        return false;
    }

    exifEntry->tag = EXIF_TAG_ORIENTATION;
    exif_content_add_entry(exifData->ifd[EXIF_IFD_0], exifEntry.get());
    exif_entry_initialize(exifEntry.get(), exifEntry->tag);
    exif_set_short(exifEntry->data, EXIF_BYTE_ORDER_INTEL, exifValue);

    if (!configureCompressor(width, height, quality)) {
        return false;
    }

    return compressData(data, exifData.get());
}

const std::vector<uint8_t>& NV12Compressor::getCompressedData() const {
    return mDestManager.mBuffer;
}

bool NV12Compressor::configureCompressor(int width, int height, int quality) {
    mCompressInfo.err = jpeg_std_error(&mErrorManager);
    // NOTE! DANGER! Do not construct any non-trivial objects below setjmp!
    // The compiler will not generate code to destroy them during the return
    // below so they will leak. Additionally, do not place any calls to libjpeg
    // that can fail above this line or any error will cause undefined behavior.
    if (setjmp(mErrorManager.mJumpBuffer)) {
        // This is where the error handler will jump in case setup fails
        // The error manager will ALOG an appropriate error message
        return false;
    }

    jpeg_create_compress(&mCompressInfo);

    mCompressInfo.image_width = width;
    mCompressInfo.image_height = height;
    mCompressInfo.input_components = 3;
    mCompressInfo.in_color_space = JCS_YCbCr;
    jpeg_set_defaults(&mCompressInfo);

    jpeg_set_quality(&mCompressInfo, quality, TRUE);
    // It may seem weird to set color space here again but this will also set
    // other fields. These fields might be overwritten by jpeg_set_defaults
    jpeg_set_colorspace(&mCompressInfo, JCS_YCbCr);
    mCompressInfo.raw_data_in = TRUE;
    mCompressInfo.dct_method = JDCT_IFAST;
    // Set sampling factors
    mCompressInfo.comp_info[0].h_samp_factor = 2;
    mCompressInfo.comp_info[0].v_samp_factor = 2;
    mCompressInfo.comp_info[1].h_samp_factor = 1;
    mCompressInfo.comp_info[1].v_samp_factor = 1;
    mCompressInfo.comp_info[2].h_samp_factor = 1;
    mCompressInfo.comp_info[2].v_samp_factor = 1;

    mCompressInfo.dest = &mDestManager;

    return true;
}

static void deinterleave(const uint8_t* vuPlanar, std::vector<uint8_t>& uRows,
        std::vector<uint8_t>& vRows, int rowIndex, int width, int height, int stride) {
    int numRows = (height - rowIndex) / 2;
    if (numRows > 8) numRows = 8;
    for (int row = 0; row < numRows; ++row) {
        int offset = ((rowIndex >> 1) + row) * stride;
        const uint8_t* vu = vuPlanar + offset;
        for (int i = 0; i < (width >> 1); ++i) {
            int index = row * (width >> 1) + i;
            uRows[index] = vu[1];
            vRows[index] = vu[0];
            vu += 2;
        }
    }
}

bool NV12Compressor::compressData(const unsigned char* data, ExifData* exifData) {
    const uint8_t* y[16];
    const uint8_t* cb[8];
    const uint8_t* cr[8];
    const uint8_t** planes[3] = { y, cb, cr };

    int i, offset;
    int width = mCompressInfo.image_width;
    int height = mCompressInfo.image_height;
    const uint8_t* yPlanar = data;
    const uint8_t* vuPlanar = data + (width * height);
    std::vector<uint8_t> uRows(8 * (width >> 1));
    std::vector<uint8_t> vRows(8 * (width >> 1));

    // NOTE! DANGER! Do not construct any non-trivial objects below setjmp!
    // The compiler will not generate code to destroy them during the return
    // below so they will leak. Additionally, do not place any calls to libjpeg
    // that can fail above this line or any error will cause undefined behavior.
    if (setjmp(mErrorManager.mJumpBuffer)) {
        // This is where the error handler will jump in case compression fails
        // The error manager will ALOG an appropriate error message
        return false;
    }

    jpeg_start_compress(&mCompressInfo, TRUE);

    attachExifData(exifData);

    // process 16 lines of Y and 8 lines of U/V each time.
    while (mCompressInfo.next_scanline < mCompressInfo.image_height) {
        //deinterleave u and v
        deinterleave(vuPlanar, uRows, vRows, mCompressInfo.next_scanline,
                     width, height, width);

        // Jpeg library ignores the rows whose indices are greater than height.
        for (i = 0; i < 16; i++) {
            // y row
            y[i] = yPlanar + (mCompressInfo.next_scanline + i) * width;

            // construct u row and v row
            if ((i & 1) == 0) {
                // height and width are both halved because of downsampling
                offset = (i >> 1) * (width >> 1);
                cb[i/2] = &uRows[offset];
                cr[i/2] = &vRows[offset];
            }
          }
        jpeg_write_raw_data(&mCompressInfo, const_cast<JSAMPIMAGE>(planes), 16);
    }

    jpeg_finish_compress(&mCompressInfo);
    jpeg_destroy_compress(&mCompressInfo);

    return true;
}

bool NV12Compressor::attachExifData(ExifData* exifData) {
    if (exifData == nullptr) {
        // This is not an error, we don't require EXIF data
        return true;
    }

    // Save the EXIF data to memory
    unsigned char* rawData = nullptr;
    unsigned int size = 0;
    exif_data_save_data(exifData, &rawData, &size);
    if (rawData == nullptr) {
        ALOGE("Failed to create EXIF data block");
        return false;
    }

    jpeg_write_marker(&mCompressInfo, JPEG_APP0 + 1, rawData, size);
    free(rawData);
    return true;
}

NV12Compressor::ErrorManager::ErrorManager() {
    error_exit = &onJpegError;
}

void NV12Compressor::ErrorManager::onJpegError(j_common_ptr cinfo) {
    // NOTE! Do not construct any non-trivial objects in this method at the top
    // scope. Their destructors will not be called. If you do need such an
    // object create a local scope that does not include the longjmp call,
    // that ensures the object is destroyed before longjmp is called.
    ErrorManager* errorManager = reinterpret_cast<ErrorManager*>(cinfo->err);

    // Format and log error message
    char errorMessage[JMSG_LENGTH_MAX];
    (*errorManager->format_message)(cinfo, errorMessage);
    errorMessage[sizeof(errorMessage) - 1] = '\0';
    ALOGE("JPEG compression error: %s", errorMessage);
    jpeg_destroy(cinfo);

    // And through the looking glass we go
    longjmp(errorManager->mJumpBuffer, 1);
}

NV12Compressor::DestinationManager::DestinationManager() {
    init_destination = &initDestination;
    empty_output_buffer = &emptyOutputBuffer;
    term_destination = &termDestination;
}

void NV12Compressor::DestinationManager::initDestination(j_compress_ptr cinfo) {
    auto manager = reinterpret_cast<DestinationManager*>(cinfo->dest);

    // Start out with some arbitrary but not too large buffer size
    manager->mBuffer.resize(16 * 1024);
    manager->next_output_byte = &manager->mBuffer[0];
    manager->free_in_buffer = manager->mBuffer.size();
}

boolean NV12Compressor::DestinationManager::emptyOutputBuffer(
        j_compress_ptr cinfo) {
    auto manager = reinterpret_cast<DestinationManager*>(cinfo->dest);

    // Keep doubling the size of the buffer for a very low, amortized
    // performance cost of the allocations
    size_t oldSize = manager->mBuffer.size();
    manager->mBuffer.resize(oldSize * 2);
    manager->next_output_byte = &manager->mBuffer[oldSize];
    manager->free_in_buffer = manager->mBuffer.size() - oldSize;
    return manager->free_in_buffer != 0;
}

void NV12Compressor::DestinationManager::termDestination(j_compress_ptr cinfo) {
    auto manager = reinterpret_cast<DestinationManager*>(cinfo->dest);

    // Resize down to the exact size of the output, that is remove as many
    // bytes as there are left in the buffer
    manager->mBuffer.resize(manager->mBuffer.size() - manager->free_in_buffer);
}

status_t NV12Compressor::findJpegSize(uint8_t *jpegBuffer, size_t maxSize, size_t *size /*out*/) {
    if ((size == nullptr) || (jpegBuffer == nullptr)) {
        return BAD_VALUE;
    }

    if (checkJpegStart(jpegBuffer) == 0) {
        return BAD_VALUE;
    }

    // Read JFIF segment markers, skip over segment data
    *size = kMarkerLength; //jump to Start Of Image
    while (*size <= maxSize - kMarkerLength) {
        segment_t *segment = (segment_t*)(jpegBuffer + *size);
        uint8_t type = checkJpegMarker(segment->marker);
        if (type == 0) { // invalid marker, no more segments, begin JPEG data
            break;
        }
        if (type == kEndOfImage || *size > maxSize - sizeof(segment_t)) {
            return BAD_VALUE;
        }

        size_t length = ntohs(segment->length);
        *size += length + kMarkerLength;
    }

    // Find End of Image
    // Scan JPEG buffer until End of Image
    bool foundEnd = false;
    for ( ; *size <= maxSize - kMarkerLength; (*size)++) {
        if (checkJpegEnd(jpegBuffer + *size)) {
            foundEnd = true;
            *size += kMarkerLength;
            break;
        }
    }

    if (!foundEnd) {
        return BAD_VALUE;
    }

    if (*size > maxSize) {
        *size = maxSize;
    }

    return OK;
}

status_t NV12Compressor::getJpegImageDimensions(uint8_t *jpegBuffer,
        size_t jpegBufferSize, size_t *width /*out*/, size_t *height /*out*/) {
    if ((jpegBuffer == nullptr) || (width == nullptr) || (height == nullptr) ||
            (jpegBufferSize == 0u)) {
        return BAD_VALUE;
    }

    // Scan JPEG buffer until Start of Frame
    bool foundSOF = false;
    size_t currentPos;
    for (currentPos = 0; currentPos <= jpegBufferSize - kMarkerLength; currentPos++) {
        if (checkStartOfFrame(jpegBuffer + currentPos)) {
            foundSOF = true;
            currentPos += kMarkerLength;
            break;
        }
    }

    if (!foundSOF) {
        ALOGE("%s: Start of Frame not found", __func__);
        return BAD_VALUE;
    }

    sof_t *startOfFrame = reinterpret_cast<sof_t *> (jpegBuffer + currentPos);
    *width = ntohs(startOfFrame->width);
    *height = ntohs(startOfFrame->height);

    return OK;
}

status_t NV12Compressor::getExifOrientation(uint8_t *jpegBuffer, size_t jpegBufferSize,
        ExifOrientation *exifValue /*out*/) {
    if ((jpegBuffer == nullptr) || (exifValue == nullptr) || (jpegBufferSize == 0u)) {
        return BAD_VALUE;
    }

    std::unique_ptr<ExifData> exifData(exif_data_new());
    exif_data_load_data(exifData.get(), jpegBuffer, jpegBufferSize);
    ExifEntry *orientation = exif_content_get_entry(exifData->ifd[EXIF_IFD_0],
            EXIF_TAG_ORIENTATION);
    if ((orientation == nullptr) || (orientation->size != sizeof(ExifShort))) {
        return BAD_VALUE;
    }

    auto orientationValue = exif_get_short(orientation->data,
            exif_data_get_byte_order(exifData.get()));
    status_t ret;
    switch (orientationValue) {
        case ExifOrientation::ORIENTATION_0_DEGREES:
        case ExifOrientation::ORIENTATION_90_DEGREES:
        case ExifOrientation::ORIENTATION_180_DEGREES:
        case ExifOrientation::ORIENTATION_270_DEGREES:
            *exifValue = static_cast<ExifOrientation> (orientationValue);
            ret = OK;
            break;
        default:
            ALOGE("%s: Unexpected EXIF orientation value: %u", __FUNCTION__, orientationValue);
            ret = BAD_VALUE;
    }

    return ret;
}
