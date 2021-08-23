#ifndef __ANSYNC_DECODER_H__
#define __ANSYNC_DECODER_H__


#ifndef CAPI
#ifdef __cplusplus
#define CAPI extern "C"
#else
#define CAPI
#endif
#endif

#ifndef U8
#define U8
typedef unsigned char u8;
#endif

#ifndef U32
#define U32
typedef unsigned int u32;
#endif


typedef struct stAnsyncDecoder AnsyncDecoder;

typedef void (*decoder_callback)(void *userdata, void *data, int dataLen, int w, int h, u32 timestamp, int mediaType);

CAPI AnsyncDecoder* AnsyncDecoder_Create(const void *sps, int sps_length, const void *pps, int pps_length, void *userdata, decoder_callback callback);
CAPI void AnsyncDecoder_Destroy(AnsyncDecoder *ad);
CAPI void AnsyncDecoder_ReceiveData(AnsyncDecoder *ad, void *data, int len, u32 timestamp, int mediaType); // video = 1 audio = 2

CAPI int AnsyncDecoder_GetWidth(AnsyncDecoder *ad);
CAPI int AnsyncDecoder_GetHeight(AnsyncDecoder *ad);

#endif /* __ANSYNC_DECODER_H__ */
