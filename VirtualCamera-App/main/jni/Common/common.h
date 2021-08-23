#ifndef __COMMON_H__
#define __COMMON_H__

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

#ifndef U16
#define U16
typedef unsigned short u16;
#endif

#ifndef U32
#define U32
typedef unsigned int u32;
#endif

#ifndef U64
#define U64
typedef unsigned long long u64;
#endif

#endif
