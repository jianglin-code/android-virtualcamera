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

#ifndef TEST_CAMERA_JPEG_STUB_NV12_COMPRESSOR_H
#define TEST_CAMERA_JPEG_STUB_NV12_COMPRESSOR_H

#include <setjmp.h>
#include <stdlib.h>
extern "C" {
#include <jpeglib.h>
#include <jerror.h>
}

#include <utils/Errors.h>
#include <vector>

#include "../utils/ExifUtils.h"

struct _ExifData;
typedef _ExifData ExifData;

class NV12Compressor {
public:
    NV12Compressor() {}

    /* Compress |data| which represents raw NV21 encoded data of dimensions
     * |width| * |height|.
     */
    bool compress(const unsigned char* data, int width, int height, int quality);
    bool compressWithExifOrientation(const unsigned char* data, int width, int height, int quality,
            android::camera3::ExifOrientation exifValue);

    /* Get a reference to the compressed data, this will return an empty vector
     * if compress has not been called yet
     */
    const std::vector<unsigned char>& getCompressedData() const;

    // Utility methods
    static android::status_t findJpegSize(uint8_t *jpegBuffer, size_t maxSize,
            size_t *size /*out*/);

    static android::status_t getExifOrientation(uint8_t *jpegBuffer,
            size_t jpegBufferSize, android::camera3::ExifOrientation *exifValue /*out*/);

    /* Get Jpeg image dimensions from the first Start Of Frame. Please note that due to the
     * way the jpeg buffer is scanned if the image contains a thumbnail, then the size returned
     * will be of the thumbnail and not the main image.
     */
    static android::status_t getJpegImageDimensions(uint8_t *jpegBuffer, size_t jpegBufferSize,
            size_t *width /*out*/, size_t *height /*out*/);

private:

    struct DestinationManager : jpeg_destination_mgr {
        DestinationManager();

        static void initDestination(j_compress_ptr cinfo);
        static boolean emptyOutputBuffer(j_compress_ptr cinfo);
        static void termDestination(j_compress_ptr cinfo);

        std::vector<unsigned char> mBuffer;
    };

    struct ErrorManager : jpeg_error_mgr {
        ErrorManager();

        static void onJpegError(j_common_ptr cinfo);

        jmp_buf mJumpBuffer;
    };

    static const size_t kMarkerLength = 2; // length of a marker
    static const uint8_t kMarker = 0xFF; // First byte of marker
    static const uint8_t kStartOfImage = 0xD8; // Start of Image
    static const uint8_t kEndOfImage = 0xD9; // End of Image
    static const uint8_t kStartOfFrame = 0xC0; // Start of Frame

    struct __attribute__((packed)) segment_t {
        uint8_t marker[kMarkerLength];
        uint16_t length;
    };

    struct __attribute__((packed)) sof_t {
        uint16_t length;
        uint8_t precision;
        uint16_t height;
        uint16_t width;
    };

    // check for start of image marker
    static bool checkStartOfFrame(uint8_t* buf) {
        return buf[0] == kMarker && buf[1] == kStartOfFrame;
    }

    // check for start of image marker
    static bool checkJpegStart(uint8_t* buf) {
        return buf[0] == kMarker && buf[1] == kStartOfImage;
    }

    // check for End of Image marker
    static bool checkJpegEnd(uint8_t *buf) {
        return buf[0] == kMarker && buf[1] == kEndOfImage;
    }

    // check for arbitrary marker, returns marker type (second byte)
    // returns 0 if no marker found. Note: 0x00 is not a valid marker type
    static uint8_t checkJpegMarker(uint8_t *buf) {
        return (buf[0] == kMarker) ? buf[1] : 0;
    }

    jpeg_compress_struct mCompressInfo;
    DestinationManager mDestManager;
    ErrorManager mErrorManager;

    bool configureCompressor(int width, int height, int quality);
    bool compressData(const unsigned char* data, ExifData* exifData);
    bool attachExifData(ExifData* exifData);
};

#endif  // TEST_CAMERA_JPEG_STUB_NV12_COMPRESSOR_H

