/*****************************************************************************
 * p264: h264 decoder
 *****************************************************************************
 * Copyright (C) 2003 Laurent Aimar
 * $Id: set.c,v 1.1 2004/06/03 19:27:07 fenrir Exp $
 *
 * Authors: Laurent Aimar <fenrir@via.ecp.fr>
 *
 * Maintained by Peter Lee (lspbeyond@126.com)
 * 2005-2006 at icas of Ningbo university, China
 * p264 Decoder site: http://p264decoder.zj.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/
 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
//#include <stdint.h>

#include "bs.h"
#include "sps_pps.h"

static const uint8_t zigzag_scan[16]={
    0+0*4, 1+0*4, 0+1*4, 0+2*4,
    1+1*4, 2+0*4, 3+0*4, 2+1*4,
    1+2*4, 0+3*4, 1+3*4, 2+2*4,
    3+1*4, 3+2*4, 2+3*4, 3+3*4,
};

static const uint8_t ff_zigzag_direct[64] = {
    0,   1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};


static const uint8_t default_scaling4[2][16]={
{ 
    6,13,20,28,
   13,20,28,32,
   20,28,32,37,
   28,32,37,42
},{
   10,14,20,24,
   14,20,24,27,
   20,24,27,30,
   24,27,30,34
}};

static const uint8_t default_scaling8[2][64]={
{   6,10,13,16,18,23,25,27,
   10,11,16,18,23,25,27,29,
   13,16,18,23,25,27,29,31,
   16,18,23,25,27,29,31,33,
   18,23,25,27,29,31,33,36,
   23,25,27,29,31,33,36,38,
   25,27,29,31,33,36,38,40,
   27,29,31,33,36,38,40,42
},{
    9,13,15,17,19,21,22,24,
   13,13,17,19,21,22,24,25,
   15,17,19,21,22,24,25,27,
   17,19,21,22,24,25,27,28,
   19,21,22,24,25,27,28,30,
   21,22,24,25,27,28,30,32,
   22,24,25,27,28,30,32,33,
   24,25,27,28,30,32,33,35
}};

static void decode_scaling_list(bs_t *s, uint8_t *factors, int size,
                                const uint8_t *jvt_list, const uint8_t *fallback_list){
    int i, last = 8, next = 8;
    const uint8_t *scan = size == 16 ? zigzag_scan : ff_zigzag_direct;

    if(!bs_read( s, 1 )) /* matrix not written, we use the predicted one */
        memcpy(factors, fallback_list, size*sizeof(uint8_t));
    else
    {
        for(i=0;i<size;i++){
            if(next)
                next = (last + bs_read_ue( s )) & 0xff;
            if(!i && !next){ /* matrix not written, we use the preset one */
                memcpy(factors, jvt_list, size*sizeof(uint8_t));
                break;
            }
            last = factors[scan[i]] = next ? next : last;
        }
    }
}

static void decode_scaling_matrices(bs_t *s, h264_sps_t *sps, h264_pps_t *pps, int is_sps,
                                   uint8_t (*scaling_matrix4)[16], uint8_t (*scaling_matrix8)[64]){
    int fallback_sps = !is_sps && sps->scaling_matrix_present;
    const uint8_t *fallback[4] = {
        fallback_sps ? sps->scaling_matrix4[0] : default_scaling4[0],
        fallback_sps ? sps->scaling_matrix4[3] : default_scaling4[1],
        fallback_sps ? sps->scaling_matrix8[0] : default_scaling8[0],
        fallback_sps ? sps->scaling_matrix8[3] : default_scaling8[1]
    };
    if(bs_read( s, 1 )){
        sps->scaling_matrix_present |= is_sps;
        decode_scaling_list(s,scaling_matrix4[0],16,default_scaling4[0],fallback[0]); // Intra, Y
        decode_scaling_list(s,scaling_matrix4[1],16,default_scaling4[0],scaling_matrix4[0]); // Intra, Cr
        decode_scaling_list(s,scaling_matrix4[2],16,default_scaling4[0],scaling_matrix4[1]); // Intra, Cb
        decode_scaling_list(s,scaling_matrix4[3],16,default_scaling4[1],fallback[1]); // Inter, Y
        decode_scaling_list(s,scaling_matrix4[4],16,default_scaling4[1],scaling_matrix4[3]); // Inter, Cr
        decode_scaling_list(s,scaling_matrix4[5],16,default_scaling4[1],scaling_matrix4[4]); // Inter, Cb
        if(is_sps || (pps&&pps->b_transform_8x8_mode)){
            decode_scaling_list(s,scaling_matrix8[0],64,default_scaling8[0],fallback[2]);  // Intra, Y
            if(sps->i_chroma_format_idc == 3){
                decode_scaling_list(s,scaling_matrix8[1],64,default_scaling8[0],scaling_matrix8[0]);  // Intra, Cr
                decode_scaling_list(s,scaling_matrix8[2],64,default_scaling8[0],scaling_matrix8[1]);  // Intra, Cb
            }
            decode_scaling_list(s,scaling_matrix8[3],64,default_scaling8[1],fallback[3]);  // Inter, Y
            if(sps->i_chroma_format_idc == 3){
                decode_scaling_list(s,scaling_matrix8[4],64,default_scaling8[1],scaling_matrix8[3]);  // Inter, Cr
                decode_scaling_list(s,scaling_matrix8[5],64,default_scaling8[1],scaling_matrix8[4]);  // Inter, Cb
            }
        }
    }
}


#define EXTENDED_SAR          255

enum AVColorPrimaries{
    AVCOL_PRI_BT709      =1, ///< also ITU-R BT1361 / IEC 61966-2-4 / SMPTE RP177 Annex B
    AVCOL_PRI_UNSPECIFIED=2,
    AVCOL_PRI_BT470M     =4,
    AVCOL_PRI_BT470BG    =5, ///< also ITU-R BT601-6 625 / ITU-R BT1358 625 / ITU-R BT1700 625 PAL & SECAM
    AVCOL_PRI_SMPTE170M  =6, ///< also ITU-R BT601-6 525 / ITU-R BT1358 525 / ITU-R BT1700 NTSC
    AVCOL_PRI_SMPTE240M  =7, ///< functionally identical to above
    AVCOL_PRI_FILM       =8,
    AVCOL_PRI_NB           , ///< Not part of ABI
};

enum AVColorTransferCharacteristic{
    AVCOL_TRC_BT709      =1, ///< also ITU-R BT1361
    AVCOL_TRC_UNSPECIFIED=2,
    AVCOL_TRC_GAMMA22    =4, ///< also ITU-R BT470M / ITU-R BT1700 625 PAL & SECAM
    AVCOL_TRC_GAMMA28    =5, ///< also ITU-R BT470BG
    AVCOL_TRC_SMPTE240M  =7,
    AVCOL_TRC_NB           , ///< Not part of ABI
};

enum AVColorSpace{
    AVCOL_SPC_RGB        =0,
    AVCOL_SPC_BT709      =1, ///< also ITU-R BT1361 / IEC 61966-2-4 xvYCC709 / SMPTE RP177 Annex B
    AVCOL_SPC_UNSPECIFIED=2,
    AVCOL_SPC_FCC        =4,
    AVCOL_SPC_BT470BG    =5, ///< also ITU-R BT601-6 625 / ITU-R BT1358 625 / ITU-R BT1700 625 PAL & SECAM / IEC 61966-2-4 xvYCC601
    AVCOL_SPC_SMPTE170M  =6, ///< also ITU-R BT601-6 525 / ITU-R BT1358 525 / ITU-R BT1700 NTSC / functionally identical to above
    AVCOL_SPC_SMPTE240M  =7,
    AVCOL_SPC_YCGCO      =8,
    AVCOL_SPC_NB           , ///< Not part of ABI
};

static inline int decode_hrd_parameters(bs_t *s, h264_sps_t *sps)
{
//    int cpb_count, i;
//    cpb_count = get_ue_golomb_31(&s->gb) + 1;
//
//    if(cpb_count > 32U){
//        av_log(h->s.avctx, AV_LOG_ERROR, "cpb_count %d invalid\n", cpb_count);
//        return -1;
//    }
//
//    bs_read(s, 4); /* bit_rate_scale */
//    bs_read(s, 4); /* cpb_size_scale */
//    for(i=0; i<cpb_count; i++){
//        get_ue_golomb_long(&s->gb); /* bit_rate_value_minus1 */
//        get_ue_golomb_long(&s->gb); /* cpb_size_value_minus1 */
//        bs_read1(s);     /* cbr_flag */
//    }
//    sps->initial_cpb_removal_delay_length = bs_read(s, 5) + 1;
//    sps->cpb_removal_delay_length = bs_read(s, 5) + 1;
//    sps->dpb_output_delay_length = bs_read(s, 5) + 1;
//    sps->time_offset_length = bs_read(s, 5);
//    sps->cpb_cnt = cpb_count;
    return 0;
}

//static const AVRational pixel_aspect[17]={
// {0, 1},
// {1, 1},
// {12, 11},
// {10, 11},
// {16, 11},
// {40, 33},
// {24, 11},
// {20, 11},
// {32, 11},
// {80, 33},
// {18, 11},
// {15, 11},
// {64, 33},
// {160,99},
// {4, 3},
// {3, 2},
// {2, 1},
//};
//
static int decode_vui_parameters(bs_t *s, h264_sps_t *sps)
{
    int aspect_ratio_info_present_flag;
    unsigned int aspect_ratio_idc;

    aspect_ratio_info_present_flag= bs_read( s, 1 );
    sps->vui.b_aspect_ratio_info_present = aspect_ratio_info_present_flag;

    if( aspect_ratio_info_present_flag ) {
        aspect_ratio_idc= bs_read(s, 8);
        if( aspect_ratio_idc == EXTENDED_SAR ) {
            sps->vui.i_sar_width= bs_read(s, 16);
            sps->vui.i_sar_height= bs_read(s, 16);
        }else if(aspect_ratio_idc < 17){
            //sps->sar=  pixel_aspect[aspect_ratio_idc];
        }else{
            //_TRACE("illegal aspect ratio\n");
            return -1;
        }
    }else{
        sps->vui.i_sar_width=
        sps->vui.i_sar_height= 0;
    }
//            s->avctx->aspect_ratio= sar_width*s->width / (float)(s->height*sar_height);

    if(bs_read( s, 1 )){      /* overscan_info_present_flag */
        bs_read( s, 1 );      /* overscan_appropriate_flag */
    }

    sps->vui.b_signal_type_present = bs_read( s, 1 );
    //_TRACE("sps->vui.b_signal_type_present=%d\r\n", sps->vui.b_signal_type_present);
    if(sps->vui.b_signal_type_present){
        sps->vui.i_vidformat = bs_read(s, 3);    /* video_format */
        //_TRACE("sps->vui.i_vidformat=%d\r\n", sps->vui.i_vidformat);
        sps->vui.b_fullrange = bs_read( s, 1 ); /* video_full_range_flag */

        sps->vui.b_color_description_present = bs_read( s, 1 );
        if(sps->vui.b_color_description_present){
            sps->vui.i_colorprim = bs_read(s, 8); /* colour_primaries */
            sps->vui.i_transfer       = bs_read(s, 8); /* transfer_characteristics */
            sps->vui.i_colmatrix      = bs_read(s, 8); /* matrix_coefficients */
            if (sps->vui.i_colorprim >= AVCOL_PRI_NB)
                sps->vui.i_colorprim  = AVCOL_PRI_UNSPECIFIED;
            if (sps->vui.i_transfer >= AVCOL_TRC_NB)
                sps->vui.i_transfer  = AVCOL_TRC_UNSPECIFIED;
            if (sps->vui.i_colmatrix >= AVCOL_SPC_NB)
                sps->vui.i_colmatrix  = AVCOL_SPC_UNSPECIFIED;
        }
    }

    if(bs_read( s, 1 )){      /* chroma_location_info_present_flag */
        sps->vui.i_chroma_loc_top = bs_read_ue(s)+1;  /* chroma_sample_location_type_top_field */
        bs_read_ue(s);  /* chroma_sample_location_type_bottom_field */
    }

    sps->vui.b_timing_info_present = bs_read( s, 1 );
    if(sps->vui.b_timing_info_present){
        sps->vui.i_num_units_in_tick = bs_get_bits_long(s, 32);
        sps->vui.i_time_scale = bs_get_bits_long(s, 32);
        if(!sps->vui.i_num_units_in_tick || !sps->vui.i_time_scale){
            //_TRACE("time_scale/num_units_in_tick invalid or unsupported (%d/%d)\n", sps->vui.i_time_scale, sps->vui.i_num_units_in_tick);
            return -1;
        }
#ifdef _DEBUG1
		fprintf( stderr, "************* %d\n", bs_pos(s) );	//gavin 20130716
#endif
        sps->vui.b_fixed_frame_rate = bs_read( s, 1 );
        //_TRACE("b_fixed_frame_rate=%d, time_scale/num_units_in_tick =(%d/%d)\n", sps->vui.b_fixed_frame_rate, sps->vui.i_time_scale, sps->vui.i_num_units_in_tick);
    }

    sps->vui.nal_hrd_parameters_present_flag = bs_read( s, 1 );
    if(sps->vui.nal_hrd_parameters_present_flag)
        if(decode_hrd_parameters(s, sps) < 0)
            return -1;
    sps->vui.vcl_hrd_parameters_present_flag = bs_read( s, 1 );
    if(sps->vui.vcl_hrd_parameters_present_flag)
        if(decode_hrd_parameters(s, sps) < 0)
            return -1;
    if(sps->vui.nal_hrd_parameters_present_flag || sps->vui.vcl_hrd_parameters_present_flag)
        bs_read( s, 1 );     /* low_delay_hrd_flag */
    sps->vui.pic_struct_present_flag = bs_read( s, 1 );
    if(bs_eof(s))
        return 0;
    sps->vui.b_bitstream_restriction = bs_read( s, 1 );
    if(sps->vui.b_bitstream_restriction){
        bs_read( s, 1 );     /* motion_vectors_over_pic_boundaries_flag */
        bs_read_ue(s); /* max_bytes_per_pic_denom */
        bs_read_ue(s); /* max_bits_per_mb_denom */
        bs_read_ue(s); /* log2_max_mv_length_horizontal */
        bs_read_ue(s); /* log2_max_mv_length_vertical */
        sps->vui.i_num_reorder_frames= bs_read_ue(s);
        bs_read_ue(s); /*max_dec_frame_buffering*/

        if (bs_eof(s)) {
            sps->vui.i_num_reorder_frames=0;
            sps->vui.b_bitstream_restriction= 0;
        }

        if( (unsigned)sps->vui.i_num_reorder_frames > 16U /*max_dec_frame_buffering || max_dec_frame_buffering > 16*/){
            //_TRACE("illegal num_reorder_frames %d\n", sps->vui.i_num_reorder_frames);
            return -1;
        }
    }
    if (bs_eof(s)) {
        //_TRACE("Overread VUI by bits\n");
        return -1;
    }

    return 0;
}

/* return -1 if invalid, else the id */
int h264_sps_read( unsigned char *nal, int nal_len, h264_sps_t *sps)
{
    int i_profile_idc;
    int i_level_idc;

    int b_constraint_set0;
    int b_constraint_set1;
    int b_constraint_set2;

    int id;
    bs_t bs;
    bs_t *s = &bs;

    sps->scaling_matrix_present = 0;

    bs_init( &bs, nal+1, nal_len-1 );

    //P264_TRACE_ADDRESS();
    i_profile_idc     = bs_read( s, 8 );
    //_TRACE("SPS: profile_idc = %d\n", i_profile_idc);
    b_constraint_set0 = bs_read( s, 1 );
    b_constraint_set1 = bs_read( s, 1 );
    b_constraint_set2 = bs_read( s, 1 );

    bs_skip( s, 5 );    /* reserved */
    //P264_TRACE_ADDRESS();
    i_level_idc       = bs_read( s, 8 );
    //_TRACE("SPS: level_idc = %d\n", i_level_idc);


    id = bs_read_ue( s );
    if( bs_eof( s ) || id >= 32 )
    {
        /* the sps is invalid, no need to corrupt sps_array[0] */
        return -1;
    }

    sps->i_id = id;

    /* put pack parsed value */
    sps->i_profile_idc     = i_profile_idc;
    sps->i_level_idc       = i_level_idc;
    sps->b_constraint_set0 = b_constraint_set0;
    sps->b_constraint_set1 = b_constraint_set1;
    sps->b_constraint_set2 = b_constraint_set2;

    if(sps->i_profile_idc >= 100){ //high profile
        sps->i_chroma_format_idc= bs_read_ue( s );
        if(sps->i_chroma_format_idc >= 32 )
            return -1;
        if(sps->i_chroma_format_idc == 3)
            bs_read( s, 1 );//residual_color_transform_flag
        //sps->bit_depth_luma   = get_ue_golomb(&s->gb) + 8;
        //sps->bit_depth_chroma = get_ue_golomb(&s->gb) + 8;
        //sps->transform_bypass = get_bits1(&s->gb);
        bs_read_ue( s );
        bs_read_ue( s );
        bs_read( s, 1 );
        decode_scaling_matrices(s, sps, NULL, 1, sps->scaling_matrix4, sps->scaling_matrix8);
    }

    sps->i_log2_max_frame_num = bs_read_ue( s ) + 4;

    sps->i_poc_type = bs_read_ue( s );
    if( sps->i_poc_type == 0 )
    {
        sps->i_log2_max_poc_lsb = bs_read_ue( s ) + 4;
    }
    else if( sps->i_poc_type == 1 )
    {
        int i;
        sps->b_delta_pic_order_always_zero = bs_read( s, 1 );
        sps->i_offset_for_non_ref_pic = bs_read_se( s );
        sps->i_offset_for_top_to_bottom_field = bs_read_se( s );
        sps->i_num_ref_frames_in_poc_cycle = bs_read_ue( s );
        if( sps->i_num_ref_frames_in_poc_cycle > 256 )
        {
            /* FIXME what to do */
            sps->i_num_ref_frames_in_poc_cycle = 256;
        }
        for( i = 0; i < sps->i_num_ref_frames_in_poc_cycle; i++ )
        {
            sps->i_offset_for_ref_frame[i] = bs_read_se( s );
        }
    }
    else if( sps->i_poc_type > 2 )
    {
        goto error;
    }

    sps->i_num_ref_frames = bs_read_ue( s );
    //_TRACE("SPS: num_ref_frames = %d\n", sps->i_num_ref_frames);
    sps->b_gaps_in_frame_num_value_allowed = bs_read( s, 1 );
    sps->i_mb_width = bs_read_ue( s ) + 1;
    //_TRACE("SPS: mb_width = %d\n", sps->i_mb_width);
    sps->i_mb_height= bs_read_ue( s ) + 1;
    //_TRACE("SPS: mb_height = %d\n", sps->i_mb_height);
    sps->b_frame_mbs_only = bs_read( s, 1 );
    if( !sps->b_frame_mbs_only )
    {
        sps->b_mb_adaptive_frame_field = bs_read( s, 1 );
    }
    else
    {
        sps->b_mb_adaptive_frame_field = 0;
    }
    sps->b_direct8x8_inference = bs_read( s, 1 );

    sps->b_crop = bs_read( s, 1 );
    if( sps->b_crop )
    {
        sps->crop.i_left  = bs_read_ue( s );
        sps->crop.i_right = bs_read_ue( s );
        sps->crop.i_top   = bs_read_ue( s );
        sps->crop.i_bottom= bs_read_ue( s );
    }
    else
    {
        sps->crop.i_left  = 0;
        sps->crop.i_right = 0;
        sps->crop.i_top   = 0;
        sps->crop.i_bottom= 0;
    }

    sps->b_vui = bs_read( s, 1 );
    if( sps->b_vui )
    {
        /* FIXME */
        //_TRACE( "decode vui %d\n", bs_pos(s) );
        decode_vui_parameters(s, sps);
    }
    else
    {

    }

    if( bs_eof( s ) )
    {
        /* no rbsp trailing */
        //_TRACE( "incomplete SPS\n" );
		sps->i_id = -1;
		return -1000;
    }
/*
    _TRACE2("h264_sps_read: sps:0x%x profile:%d/%d poc:%d ref:%d %dx%d crop:%d-%d-%d-%d\n",
             sps->i_id,
             sps->i_profile_idc, sps->i_level_idc,
             sps->i_poc_type,
             sps->i_num_ref_frames,
             sps->i_mb_width, sps->i_mb_height,
             sps->crop.i_left, sps->crop.i_right,
             sps->crop.i_top, sps->crop.i_bottom );
*/    
 //   _TRACE2("sps->i_mb_width=%d, sps->i_mb_height%d\r\n", sps->i_mb_width, sps->i_mb_height);
    return id;

error:
    /* invalidate this sps */
    sps->i_id = -1;
    return -1;
}

/* return -1 if invalid, else the id */
int h264_pps_read( unsigned char *nal, int nal_len, h264_pps_t *pps )
{
    int id;
    int i;
    bs_t bs;
    bs_t *s = &bs;

    bs_init( &bs, nal+1, nal_len-1 );

    //P264_TRACE_ADDRESS();
    id = bs_read_ue( s ); //pps id
    //_TRACE("PPS: pic_parameter_set_id = %d\n", id);
    if( bs_eof( s ) || id >= 256 )
    {
        fprintf( stderr, "id invalid\n" );
        return -1;
    }
    pps->i_id = id;
    //P264_TRACE_ADDRESS();
    pps->i_sps_id = bs_read_ue( s );
    //_TRACE("PPS: seq_parameter_set_id = %d\n", pps->i_sps_id);
    if( pps->i_sps_id >= 32 )
    {
        goto error;
    }
    //P264_TRACE_ADDRESS();
    pps->b_cabac = bs_read( s, 1 );
    //_TRACE("PPS: b_cabac = %d\n", pps->b_cabac);
    pps->b_pic_order = bs_read( s, 1 );
    //P264_TRACE_ADDRESS();
    pps->i_num_slice_groups = bs_read_ue( s ) + 1;
    //_TRACE("PPS: num_slice_groups = %d\n", pps->i_num_slice_groups);
    if( pps->i_num_slice_groups > 1 )
    {
        //_TRACE("FMO unsupported\n " );

        pps->i_slice_group_map_type  =bs_read_ue( s );
        if( pps->i_slice_group_map_type == 0 )
        {
            for( i = 0; i < pps->i_num_slice_groups; i++ )
            {
                pps->i_run_length[i] = bs_read_ue( s );
            }
        }
        else if( pps->i_slice_group_map_type == 2 )
        {
            for( i = 0; i < pps->i_num_slice_groups; i++ )
            {
                pps->i_top_left[i] = bs_read_ue( s );
                pps->i_bottom_right[i] = bs_read_ue( s );
            }
        }
        else if( pps->i_slice_group_map_type == 3 ||
                 pps->i_slice_group_map_type == 4 ||
                 pps->i_slice_group_map_type == 5 )
        {
            pps->b_slice_group_change_direction = bs_read( s, 1 );
            pps->i_slice_group_change_rate = bs_read_ue( s ) + 1;
        }
        else if( pps->i_slice_group_map_type == 6 )
        {
            pps->i_pic_size_in_map_units = bs_read_ue( s ) + 1;
            for( i = 0; i < pps->i_pic_size_in_map_units; i++ )
            {
               /*  FIXME */
                /* pps->i_slice_group_id = bs_read( s, ceil( log2( pps->i_pic_size_in_map_units +1 ) ) ); */
            }
        }
    }
    pps->i_num_ref_idx_l0_active = bs_read_ue( s ) + 1;
    pps->i_num_ref_idx_l1_active = bs_read_ue( s ) + 1;
    pps->b_weighted_pred = bs_read( s, 1 );
    pps->b_weighted_bipred = bs_read( s, 2 );

    //P264_TRACE_ADDRESS();
    pps->i_pic_init_qp = bs_read_se( s ) + 26;
    //_TRACE("PPS: pic_init_qp = %d\n", pps->i_pic_init_qp);
    pps->i_pic_init_qs = bs_read_se( s ) + 26;

    pps->i_chroma_qp_index_offset = bs_read_se( s );

    pps->b_deblocking_filter_control = bs_read( s, 1 );
    pps->b_constrained_intra_pred = bs_read( s, 1 );
    pps->b_redundant_pic_cnt = bs_read( s, 1 );

    if( bs_eof( s ) )
    {
        /* no rbsp trailing */
        //_TRACE("incomplete PPS\n" );
        goto error;
    }
/*
    _TRACE2("h264_pps_read: pps:0x%x sps:0x%x %s slice_groups=%d ref0:%d ref1:%d QP:%d QS:%d QC=%d DFC:%d CIP:%d RPC:%d\n",
             pps->i_id, pps->i_sps_id,
             pps->b_cabac ? "CABAC" : "CAVLC",
             pps->i_num_slice_groups,
             pps->i_num_ref_idx_l0_active,
             pps->i_num_ref_idx_l1_active,
             pps->i_pic_init_qp, pps->i_pic_init_qs, pps->i_chroma_qp_index_offset,
             pps->b_deblocking_filter_control,
             pps->b_constrained_intra_pred,
             pps->b_redundant_pic_cnt );
*/
    //lsp?? //temp way
    //for( i = 0; i < 6; i++ )
    //  pps->scaling_list[i] = h264_cqm_flat16;
    //lsp

    return id;
error:
    pps->i_id = -1;
    return -1;
}

#if(0)
static void ParseSei( decoder_t *p_dec, block_t *p_frag )
{
    decoder_sys_t *p_sys = p_dec->p_sys;
    uint8_t *pb_dec;
    int i_dec;

    /* */
    CreateDecodedNAL( &pb_dec, &i_dec, &p_frag->p_buffer[5], p_frag->i_buffer - 5 );
    if( !pb_dec )
        return;

    /* The +1 is for rbsp trailing bits */
    for( int i_used = 0; i_used+1 < i_dec; )
    {
        /* Read type */
        int i_type = 0;
        while( i_used+1 < i_dec )
        {
            const int i_byte = pb_dec[i_used++];
            i_type += i_byte;
            if( i_byte != 0xff )
                break;
        }
        /* Read size */
        int i_size = 0;
        while( i_used+1 < i_dec )
        {
            const int i_byte = pb_dec[i_used++];
            i_size += i_byte;
            if( i_byte != 0xff )
                break;
        }
        /* Check room */
        if( i_used + i_size + 1 > i_dec )
            break;

        /* Look for user_data_registered_itu_t_t35 */
        if( i_type == 4 )
        {
            static const uint8_t p_dvb1_data_start_code[] = {
                0xb5,
                0x00, 0x31,
                0x47, 0x41, 0x39, 0x34
            };
            const int      i_t35 = i_size;
            const uint8_t *p_t35 = &pb_dec[i_used];

            /* Check for we have DVB1_data() */
            if( i_t35 >= 5 &&
                !memcmp( p_t35, p_dvb1_data_start_code, sizeof(p_dvb1_data_start_code) ) )
            {
                cc_Extract( &p_sys->cc_next, true, &p_t35[3], i_t35 - 3 );
            }
        }
        i_used += i_size;
    }

    free( pb_dec );
}
#endif


void main2()
{
#if 0
    bs_t bs;
    int i_ret;
    int first_mb, slice_type;
    int i_pic_parameter_set_id,i_frame_num;
    int i_field_pic_flag,i_bottom_field_flag;
    h264_sps_t sps;
    h264_pps_t pps;

    char sps_nal[]={
//        0x67, 0x42, 0x00, 0x28, 0xE9, 0x01, 0x60, 0x24, 0xC8
//        //松下(WV-SP306 FW1.44)-720P.h264
//        0x67, 0x64, 0x00, 0x28, 0xAD, 0xC5, 0x4D, 0x86, 0x38, 0x8C, 0x54, 0x56, 0x2A, 0x6C, 0x31, 0xC4, 
//        0x62, 0xA2, 0xB1, 0x53, 0x61, 0x8E, 0x23, 0x15, 0x15, 0x10, 0x48, 0x8C, 0x47, 0x36, 0x49, 0x22, 
//        0x09, 0x11, 0x88, 0xE6, 0xC9, 0x24, 0x41, 0x22, 0x31, 0x1C, 0xD9, 0x24, 0x2D, 0x00, 0xA0, 0x0B, 
//        0x7F, 0xE0, 0x35, 0x48, 0x00, 0x00, 0x5D, 0xD8, 0x00, 0x0A, 0xFC, 0x87, 0xB1, 0x03, 0xE8, 0x00, 
//        0x06, 0x1A, 0x85, 0xFF, 0xFF, 0x1D, 0x88, 0x1F, 0x40, 0x00, 0x30, 0xD4, 0x2F, 0xFF, 0xF8, 0x50, 
//            //耐杰(NVC-HD200BRZ)-720P.h264
//        0x67, 0x4D, 0x00, 0x29, 0x9A, 0x62, 0x80, 0xA0, 0x0B, 0x7F, 0xE0, 0x2D, 0x40, 0x40, 0x40, 0x50, 
//        0x00, 0x00, 0x3E, 0x80, 0x00, 0x0C, 0x35, 0x0E, 0x86, 0x00, 0x10, 0x00, 0x00, 0x03, 0x01, 0x00, 
//        0x01, 0xBB, 0xCB, 0x8D, 0x0C, 0x00, 0x20, 0x00, 0x00, 0x03, 0x02, 0x00, 0x03, 0x77, 0x97, 0x0A, 
//            
//            //(ST-N9120HF)
//        0x27, 0x4D, 0x40, 0x28, 0x8D, 0x8D, 0x28, 0x0F, 0x00, 0x44, 0xFC, 0xB8, 0x0B, 0x50, 0x10, 0x10, 
//        0x14, 0x00, 0x00, 0x0F, 0xA0, 0x00, 0x03, 0x0D, 0x41, 0xA1, 0x80, 0x0C, 0xB7, 0x00, 0x03, 0x93, 
//        0x52, 0xEF, 0x2E, 0x34, 0x30, 0x01, 0x96, 0xE0, 0x00, 0x72, 0x6A, 0x5D, 0xE5, 0xC1, 0x7A, 0x2C, 
        
            //晧维ST-N9120HD
//        0x27, 0x4D, 0x40, 0x28, 0x8D, 0x8D, 0x28, 0x0A, 0x00, 0xB7, 0x60, 0x2D, 0x40, 0x40, 0x40, 0x50, 
//        //0x00, 0x00, 0x3E, 0x80, 0x00, 0x0C, 0x35, 0x06, 0x86, 0x00, 0x3E, 0x80, 0x00, 0x04, 0x65, 0x02,
//        0x00, 0x00, 0x3E, 0x80, 0x00, 0x06, 0x1A, 0x8E, 0x86, 0x00, 0x3E, 0x80, 0x00, 0x04, 0x65, 0x02,
//        0xEF, 0x2E, 0x34, 0x30, 0x01, 0xF4, 0x00, 0x00, 0x23, 0x28, 0x17, 0x79, 0x70, 0x5E, 0x8F,

//
//            //hikvison
//        0x67, 0x42, 0x80, 0x20, 0x88, 0x8B, 0x40, 0x28, 0x03, 0xCD, 0x08, 0x00, 0x00, 0x70, 0x80, 0x00, 
//        0x0A, 0xFC, 0x80

        0x67, 0x64, 0x00, 0x28, 0xAD, 0x84, 0x05, 0x45, 0x62, 0xB8, 0xAC, 0x54, 0x74, 0x20, 0x2A, 0x2B, 
        0x15, 0xC5, 0x62, 0xA3, 0xA1, 0x01, 0x51, 0x58, 0xAE, 0x2B, 0x15, 0x1D, 0x08, 0x0A, 0x8A, 0xC5, 
        0x71, 0x58, 0xA8, 0xE8, 0x40, 0x54, 0x56, 0x2B, 0x8A, 0xC5, 0x47, 0x42, 0x02, 0xA2, 0xB1, 0x5C, 
        0x56, 0x2A, 0x3A, 0x10, 0x24, 0x85, 0x21, 0x39, 0x3C, 0x9F, 0x27, 0xE4, 0xFE, 0x4F, 0xC9, 0xF2, 
        0x79, 0xB9, 0xB3, 0x4D, 0x08, 0x12, 0x42, 0x90, 0x9C, 0x9E, 0x4F, 0x93, 0xF2, 0x7F, 0x27, 0xE4, 
        0xF9, 0x3C, 0xDC, 0xD9, 0xA6, 0xB4, 0x03, 0xC0, 0x11, 0x3F, 0x2E, 0x02, 0xA9, 0x00, 0x00, 0x03, 
        0x00, 0x01, 0x00, 0x00, 0x03, 0x01, 0x2C, 0x60, 0x40, 0x00, 0x05, 0xF5, 0xE1, 0x00, 0x00, 0x2F, 
        0xAF, 0x0A, 0xF7, 0xBE, 0x17, 0x84, 0x42, 0x35
    };

    char pps_nal[] = {0x68, 0xCE, 0x31, 0x52};

    char slice_data[]={
        0x88, 0x83, 0x00, 0x11, 0x98, 0xDF, 0x8C, 0xE7, 0x39, 0x9F, 0x85, 0x80, 0x00, 0x41, 0x1D, 0x51, 
        0x02, 0x68, 0x31, 0xB1, 0xDF, 0xE2, 0x9F, 0xFB, 0xF9, 0xB4, 0xFC, 0xAD, 0x69, 0x44, 0x69, 0xA7, 
        0xE0, 0x6A, 0x60, 0xE2, 0xDD, 0x80, 0x14, 0xA0, 0x1A, 0x60, 0x53, 0x79, 0x93, 0xBB, 0x9C, 0xA8, 
        0x65, 0x00, 0x00, 0x80, 0x10, 0x2D, 0xCD, 0x00, 0x10, 0x5C, 0x44, 0x7F, 0xA4, 0x42, 0xC5, 0xE0, 
        0x11, 0xE4, 0xBC, 0x39, 0x74, 0xC6, 0x4A, 0xD0, 0xB5, 0x94, 0x5A, 0x6B, 0x8D, 0x0D, 0x80, 0x00, 
        0x40, 0x5A, 0x1F, 0x00, 0x0E, 0x0D, 0x5C, 0xB2, 0x76, 0x5F, 0x93, 0x63, 0xD3, 0x88, 0xF8, 0x61, 
        0xFA, 0x0B, 0xED, 0x6E, 0x92, 0xB3, 0x59, 0xC7, 0x66, 0x5C, 0xF8, 0x84, 0xFC, 0x2B, 0x0B, 0x02, 
        0xD0, 0x7C, 0x83, 0xEA, 0x90, 0x91, 0xBC, 0x90, 0x80, 0x12, 0xC9, 0x82, 0x5A, 0x82, 0xA9, 0x40, 
        0x08, 0xEA, 0x43, 0x62, 0xC8, 0xAF, 0x3E, 0x42, 0x1A, 0xA8, 0x15, 0x0D, 0xE0, 0x70, 0x81, 0x30, 
        0x23, 0x2A, 0x1B, 0x5E, 0xDD, 0xB6, 0xF1, 0xFF, 0x87, 0xA5, 0x1D, 0x68, 0xFC, 0xF9, 0xEB, 0xA8, 
        0x41, 0x05, 0xA6, 0xA9, 0x57, 0x95, 0xA3, 0xB1, 0xEA, 0x46, 0x7E, 0x55, 0xB8, 0xD2, 0xC9, 0x7E, 
        0x34, 0xFF, 0xF1, 0xEA, 0x03, 0x5F, 0x8A, 0xEA, 0x63, 0x46, 0x8F, 0x85, 0x6F, 0x75, 0xB7, 0xFF, 
        0xF8, 0xE3, 0xE1, 0xFA, 0x05, 0x31, 0xD6, 0x86, 0xE6, 0xFA, 0xAD, 0x7C, 0x07, 0xF3, 0xC8, 0xD3, 
        0xC7, 0x5A, 0x3C, 0x55, 0x20, 0xC1, 0xBC, 0xCF, 0x8E, 0xE0, 0xCA, 0x59, 0x2F, 0x1F, 0x32, 0x81, 
        0xF4, 0xAC, 0x69, 0x24, 0x9E, 0x42, 0xCF, 0x47, 0x5A, 0x28, 0x39, 0x08, 0xC2, 0x3D

    };

//    _TRACE2("==========================================\r\n");
    if( ( i_ret = h264_sps_read(  (unsigned char*)sps_nal, sizeof(sps_nal), &sps ) ) < 0 )
    {
      fprintf( stderr, "h264: h264_sps_read failed\n" );
    }
    else
//        _TRACE2("resolution = %dx%d\r\n", sps.i_mb_width*16, sps.i_mb_height*16);
//    _TRACE2("0x%02X\r\n", sps_nal[23]);

    if( ( i_ret = h264_pps_read(  (unsigned char*)pps_nal, sizeof(pps_nal), &pps ) ) < 0 )
    {
      fprintf( stderr, "h264: h264_sps_read failed\n" );
    }

    bs_init( &bs, slice_data, sizeof(slice_data));
    first_mb = bs_read_ue( &bs );/* first_mb_in_slice */
    slice_type = bs_read_ue( &bs );/* slice_type */

    i_pic_parameter_set_id = bs_read_ue( &bs );
    i_frame_num = bs_read( &bs, sps.i_log2_max_frame_num + 4 );

//    _TRACE2("%d %d %d %d\r\n", first_mb, slice_type, i_pic_parameter_set_id, i_frame_num);
    if( !sps.b_frame_mbs_only )
    {
        /* field_pic_flag */
        i_field_pic_flag = bs_read( &bs, 1 );
//        _TRACE2("i_field_pic_flag=%d\r\n", i_field_pic_flag);
        if( i_field_pic_flag )
        {
            i_bottom_field_flag = bs_read( &bs, 1 );
//            _TRACE2("i_bottom_field_flag=%d\r\n", i_bottom_field_flag);
        }
    }
    
    //i_idr_pic_id = bs_read_ue( &s );
#endif
}

void main1()
{
#if 0
    int i_ret = 0;    
    //char sps_nal[] = {0x67, 0x42, 0xe0, 0x0a, 0x89, 0x95, 0x42, 0xc1, 0x2c, 0x80};
//    char pps_nal[] = {0x68, 0xce, 0x05, 0x8b, 0x72};
//    char sps_nal[] = {0x67, 0x42, 0xe0, 0x1f, 0xda, 0x01, 0x40, 0x16, 0xc4};
//    char pps_nal[] = {0x68, 0xce, 0x30, 0xa4, 0x80};
//    char sps_nal[] = {0x67, 0x27, 0x42, 0x10, 0x09, 0x96, 0x35, 0x05, 0x89, 0xC8};
//    char pps_nal[] = {0x68, 0x28, 0xCE, 0x02, 0xFC, 0x80};
//    char sps_nal[] = {
//            0x67, 0x64, 0x00, 0x28, 0xAD, 0x84, 0x05, 0x45, 0x62, 0xB8, 0xAC, 0x54, 
//            0x74, 0x20, 0x2A, 0x2B, 0x15, 0xC5, 0x62, 0xA3, 0xA1, 0x01, 0x51, 0x58, 0xAE, 0x2B, 0x15, 0x1D, 
//            0x08, 0x0A, 0x8A, 0xC5, 0x71, 0x58, 0xA8, 0xE8, 0x40, 0x54, 0x56, 0x2B, 0x8A, 0xC5, 0x47, 0x42, 
//            0x02, 0xA2, 0xB1, 0x5C, 0x56, 0x2A, 0x3A, 0x10, 0x24, 0x85, 0x21, 0x39, 0x3C, 0x9F, 0x27, 0xE4, 
//            0xFE, 0x4F, 0xC9, 0xF2, 0x79, 0xB9, 0xB3, 0x4D, 0x08, 0x12, 0x42, 0x90, 0x9C, 0x9E, 0x4F, 0x93, 
//            0xF2, 0x7F, 0x27, 0xE4, 0xF9, 0x3C, 0xDC, 0xD9, 0xA6, 0xB4, 0x05, 0x01, 0xEC, 0x80 };
    char sps_nal_high[] = {
        0x67, 0x64, 0x00, 0x1E, 0xAD, 0x84, 0x05, 0x45, 0x62, 0xB8, 0xAC, 0x54, 0x74, 0x20, 0x2A, 0x2B, 
        0x15, 0xC5, 0x62, 0xA3, 0xA1, 0x01, 0x51, 0x58, 0xAE, 0x2B, 0x15, 0x1D, 0x08, 0x0A, 0x8A, 0xC5, 
        0x71, 0x58, 0xA8, 0xE8, 0x40, 0x54, 0x56, 0x2B, 0x8A, 0xC5, 0x47, 0x42, 0x02, 0xA2, 0xB1, 0x5C, 
        0x56, 0x2A, 0x3A, 0x10, 0x24, 0x85, 0x21, 0x39, 0x3C, 0x9F, 0x27, 0xE4, 0xFE, 0x4F, 0xC9, 0xF2, 
        0x79, 0xB9, 0xB3, 0x4D, 0x08, 0x12, 0x42, 0x90, 0x9C, 0x9E, 0x4F, 0x93, 0xF2, 0x7F, 0x27, 0xE4, 
        0xF9, 0x3C, 0xDC, 0xD9, 0xA6, 0xB4, 0x05, 0x80, 0x93, 0x42, 0x00, 0x00, 0x03, 0x03, 0xE8, 0x00, 
        0x00, 0xC3, 0x50, 0xC0, 0x80, 0x00, 0x40, 0x00, 0x00, 0x03, 0x00, 0x48, 0x00, 0x37, 0xBD, 0xF0, 
        0xBC, 0x22, 0x11, 0xA8};

    //char sps_nal[] = {0x67, 0x42, 0x80, 0x28, 0xDA, 0x01, 0xE0, 0x08, 0x97, 0x95, 0x00, 0x00};
    char pps_nal_high[] = {0x68, 0xCE, 0x3C, 0xB0};

    char sps_nal[]={
        0x67, 0x42, 0x00, 0x28, 0xE9, 0x01, 0x60, 0x4B
//        0x67, 0x42, 0x80, 0x1E, 0xDA, 0x02, 0xC0, 0x49, 0x21, 0x00, 0x00, 0x03, 
//        0x01, 0xF4, 0x00, 0x00, 0x61, 0xA8, 0x60, 0x40, 0x00, 0x20, 0x00, 0x00, 0x03, 0x00, 0x24, 0x00, 
//        0x1B, 0xDE, 0xF8, 0x5E, 0x11, 0x08, 0xD4
    };
    //char pps_nal[] = {0x68, 0xCE, 0x3C, 0x80};
    char pps_nal[] = {0x68, 0xCE, 0x31, 0x52};

    char sps_nal_720p_baseline[] = {0x67, 0x42, 0x80, 0x28, 0xDA, 0x01, 0x40, 0x16, 0xC4};
    char sps_nal_720p_main[] = {0x67, 0x4D, 0x00, 0x28, 0xDA, 0x01, 0x40, 0x16, 0xE4};
    char sps_nal_720p_high[] = {
            0x67, 0x64, 0x00, 0x28, 0xAD, 0x84, 0x05, 0x45, 0x62, 0xB8, 0xAC, 0x54, 
            0x74, 0x20, 0x2A, 0x2B, 0x15, 0xC5, 0x62, 0xA3, 0xA1, 0x01, 0x51, 0x58, 0xAE, 0x2B, 0x15, 0x1D, 
            0x08, 0x0A, 0x8A, 0xC5, 0x71, 0x58, 0xA8, 0xE8, 0x40, 0x54, 0x56, 0x2B, 0x8A, 0xC5, 0x47, 0x42, 
            0x02, 0xA2, 0xB1, 0x5C, 0x56, 0x2A, 0x3A, 0x10, 0x24, 0x85, 0x21, 0x39, 0x3C, 0x9F, 0x27, 0xE4, 
            0xFE, 0x4F, 0xC9, 0xF2, 0x79, 0xB9, 0xB3, 0x4D, 0x08, 0x12, 0x42, 0x90, 0x9C, 0x9E, 0x4F, 0x93, 
            0xF2, 0x7F, 0x27, 0xE4, 0xF9, 0x3C, 0xDC, 0xD9, 0xA6, 0xB4, 0x02, 0x80, 0x2D, 0xC8 };

    h264_sps_t sps;
    h264_pps_t pps;
//
//    char slice_data[]=
//    {
//      0x88, 0xE8, 0xFE, 0xFF, 0xFF, 0xB8, 0x80, 0x00, 0x10, 0x35, 0x48,
//      0xB7, 0x9B, 0xD3, 0x95, 0x6C, 0x45, 0x21, 0xF6, 0x16, 0x1B, 0x1B, 0xC7, 0x6C, 0x8F, 0x7A, 0x47, 
//      0xC0, 0xF9, 0x84, 0x30, 0x5F, 0xB8, 0x0F, 0xD5, 0x32, 0x4C, 0xA6, 0x7A, 0x9F, 0x53, 0x16, 0x8D, 
//      0x9A, 0x2F, 0xC7, 0x9D, 0x7A, 0x4C, 0x34, 0x61, 0x0C, 0x04, 0x6B, 0xF2, 0x5F, 0xF8, 0x15, 0x4C
//    };
//    bs_t bs;
//    int first_mb, slice_type;
//
//    bs_init( &bs, slice_data, sizeof(slice_data));
//    first_mb = bs_read_ue( &bs );/* first_mb_in_slice */
//    slice_type = bs_read_ue( &bs );/* slice_type */
//    _TRACE2("first_mb = %d, slice_type=%d\r\n", first_mb,slice_type);
//    return;

//    _TRACE2("==========================================\r\n");
    if( ( i_ret = h264_sps_read(  (unsigned char*)sps_nal, sizeof(sps_nal), &sps ) ) < 0 )
    {
      fprintf( stderr, "h264: h264_sps_read failed\n" );
    }
    else
//        _TRACE2("resolution = %dx%d\r\n", sps.i_mb_width*16, sps.i_mb_height*16);
    if( ( i_ret = h264_pps_read(  (unsigned char*)pps_nal, sizeof(pps_nal), &pps ) ) < 0 )
    {
      fprintf( stderr, "h264: h264_sps_read failed\n" );
    }
    
//    _TRACE2("==========================================\r\n");
    if( ( i_ret = h264_sps_read(  (unsigned char*)sps_nal_high, sizeof(sps_nal_high), &sps ) ) < 0 )
    {
      fprintf( stderr, "h264: h264_sps_read failed\n" );
    }
    else
//        _TRACE2("resolution = %dx%d\r\n", sps.i_mb_width*16, sps.i_mb_height*16);
    if( ( i_ret = h264_pps_read( (unsigned char*)pps_nal_high, sizeof(pps_nal_high), &pps ) ) < 0 )
    {
      fprintf( stderr, "h264: h264_sps_read failed\n" );
    }

    return;

//    _TRACE2("==========================================\r\n");
    if( ( i_ret = h264_sps_read(  (unsigned char*)sps_nal_720p_baseline, sizeof(sps_nal_720p_baseline), &sps ) ) < 0 )
    {
      fprintf( stderr, "h264: h264_sps_read failed\n" );
    }
    else
//        _TRACE2("resolution = %dx%d\r\n", sps.i_mb_width*16, sps.i_mb_height*16);
//    _TRACE2("==========================================\r\n");
    if( ( i_ret = h264_sps_read(  (unsigned char*)sps_nal_720p_main, sizeof(sps_nal_720p_main), &sps ) ) < 0 )
    {
      fprintf( stderr, "h264: h264_sps_read failed\n" );
    }
    else
//        _TRACE2("resolution = %dx%d\r\n", sps.i_mb_width*16, sps.i_mb_height*16);
//    _TRACE2("==========================================\r\n");
    if( ( i_ret = h264_sps_read(  (unsigned char*)sps_nal_720p_high, sizeof(sps_nal_720p_high), &sps ) ) < 0 )
    {
      fprintf( stderr, "h264: h264_sps_read failed\n" );
    }
    else
//        _TRACE2("resolution = %dx%d\r\n", sps.i_mb_width*16, sps.i_mb_height*16);

#endif
}
