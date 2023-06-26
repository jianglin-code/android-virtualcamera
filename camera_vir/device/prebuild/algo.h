#ifndef __ALGO_H__
#define __ALGO_H__

//#include "ir_process.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PREISP_CALIB_ITEM_NUM 8

#define PREISP_PRODUCT_DATE_NAME    "sn_code"
#define PREISP_PRODUCT_DATE_NAME    "sn_code"

struct calib_item
{
    unsigned char name[48];
    unsigned int  offset;
    unsigned int  size;
    unsigned int  temp;
    unsigned int  crc32;
};

struct calib_head
{
    unsigned char magic[16];
    unsigned int  version;
    unsigned int  head_size;
    unsigned int  image_size;
    unsigned int  items_number;
    unsigned char reserved0[32];
    unsigned int  hash_len;
    unsigned char hash[32];
    unsigned char reserved1[28];
    unsigned int  sign_tag;
    unsigned int  sign_len;
    unsigned char rsa_hash[256];
    unsigned char reserved2[120];
    struct calib_item item[PREISP_CALIB_ITEM_NUM];
};


int doAlgo(char * buf, int w, int h, int bpp, unsigned short * depthMap);
int algoInit(int width, int height, const char *data_path, const char *data_path_out);
void algoDeinit(void);

#ifdef __cplusplus
};
#endif

#endif

