//
// Created by yuzh on 2018/9/10.
//

#ifndef __NATIVEDECODEC_H_
#define __NATIVEDECODEC_H_

#ifndef U8
#define U8
typedef unsigned char u8;
#endif

#ifndef U32
#define U32
typedef unsigned int u32;
#endif

#ifdef __cplusplus
extern "C" {
#endif
typedef struct stNativeCodec NativeCodec;
NativeCodec* NativeCodec_Create(int height, int width);
void NativeDecodec_Destroy(NativeCodec* decodec);
void NaticeDecodec_InputData(NativeCodec *codec, void *data, int length, u32 timestamp);

#ifdef __cplusplus
}
#endif

#endif //JELLYFISH_NATIVEDECODEC_H
