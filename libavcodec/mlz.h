/*
 * Copyright (c) 2016 Umair Khan <omerjerk@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_MLZ_H
#define AVCODEC_MLZ_H

#include "get_bits.h"

#define CODE_UNSET          -1
#define CODE_BIT_INIT       9
#define CODE_BIT_MAX        15
#define DIC_INDEX_INIT      512     // 2^9
#define DIC_INDEX_MAX       32768   // 2^15
#define FLUSH_CODE          256
#define FREEZE_CODE         257
#define FIRST_CODE          258
#define MAX_CODE            32767
#define TABLE_SIZE          35023   // TABLE_SIZE must be a prime number
#define WORD_SIZE           8
#define MAX_SEARCH          4
/** Dictionary structure for mlz decompression
 */
typedef struct MLZDict {
    int  string_code;
    int  parent_code;
    int  char_code;
    int  match_len;
} MLZDict;

/** MLZ data strucure
 */
typedef struct MLZ {
    int dic_code_bit;
    int current_dic_index_max;
    unsigned int bump_code;
    unsigned int flush_code;
    int next_code;
    int freeze_flag;
    MLZDict* dict;
    void* context;

    //////// fields for encoder/////////////////
    MLZDict *p_dict;
    int **pp_hash_table;
    
    // buffer information
    //mp - pointers to main fields


    unsigned char   *mp_input_buff;
    unsigned char   *mp_input_mask;
    unsigned long    m_sizeof_input_buff;
    unsigned char   *mp_encode_buff;
    unsigned long    m_sizeof_encode_buff;
    unsigned long    mp_out_position;
    
    int              m_dict_code_bit;
    int              m_current_dict_index_max;
    unsigned int     m_bump_code;
    unsigned int     m_flush_code;
    int              m_next_code;
    int              m_freeze_flag;

    // dictionary backup area for the encoder
    MLZDict *        bp_dict;
    int **           bpp_hash_table;
    int              b_dict_code_bit;
    int              b_current_dict_index_max;
    unsigned int     b_bump_code;
    unsigned int     b_flush_code;
    int              b_next_code;
    int              b_freeze_flag;





} MLZ;

void mlz_create(MLZ* mlz);//+

void mlz_delete(MLZ* mlz);//+

void mlz_alloc_dict(MLZ* mlz);//+

void mlz_free_dict(MLZ* mlz);//+

void mlz_backup_dict(MLZ* mlz);//+

void mlz_resume_dict(MLZ* mlz);//+

void mlz_init_dict(MLZ* mlz);//+

void mlz_flush_dict(MLZ* mlz);//+

unsigned long mlz_encode(     //ret: encoded size (bits)
 MLZ* mlz,
 unsigned char *p_input_buff,     //in: input string buffer inputBuff[sizeofInBuff]
 unsigned char *p_input_mask,     //in: input mask buffer inputMask[sizeofInBuff]
 unsigned long sizeof_input_buff, //in: buffer size
 unsigned char *p_encode_buff,    //out: coded data
 unsigned long sizeof_encode_buff //in: buffer size
);

int mlz_output_code(MLZ* mlz, int string_code);//+

int mlz_get_vacant_hash_index(//+
    MLZ* mlz,
    int parent_code,
    int char_code,
    int mask_size
);

void mlz_set_new_entry_to_dict_with_hash(//+
  MLZ* mlz,
  int string_code,
  int parent_code,
  int char_code,
  int match_len
);

int mlz_get_hash_index(//+
  MLZ* mlz,
  int parent_code,       // in: parent index code of the dict.
  int char_code,     // in: charCode = charCode & mask
  int  mask_size,       // in: mask bit width
  int *p_candidates, // out: list of stringCodes
  int  num_index_max      // in: maxnum of candidates
);

int mlz_get_root_index(MLZ* mlz, unsigned long position);//+

int mlz_search_dict(  //+     // return: length of matched string indicated by the index
  MLZ* mlz,
  int last_char_code,     // stringCode which is based on the search
  int *string_code,          // out: index of the dict.
  unsigned long position    // in:  position of m_pInputBuff
);







/** Initialize the dictionary
 */
void ff_mlz_init_dict(void* context, MLZ *mlz);

/** Flush the dictionary
 */
void ff_mlz_flush_dict(MLZ *dict);

/** Run mlz decompression on the next size bits and the output will be stored in buff
 */
int ff_mlz_decompression(MLZ* mlz, GetBitContext* gb, int size, unsigned char *buff);

#endif /*AVCODEC_MLZ_H*/
