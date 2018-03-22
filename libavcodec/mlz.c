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

#include "mlz.h"

av_cold void ff_mlz_init_dict(void* context, MLZ *mlz) {
    mlz->dict = av_mallocz_array(TABLE_SIZE, sizeof(*mlz->dict));

    mlz->flush_code            = FLUSH_CODE;
    mlz->current_dic_index_max = DIC_INDEX_INIT;
    mlz->dic_code_bit          = CODE_BIT_INIT;
    mlz->bump_code             = (DIC_INDEX_INIT - 1);
    mlz->next_code             = FIRST_CODE;
    mlz->freeze_flag           = 0;
    mlz->context               = context;
}

av_cold void ff_mlz_flush_dict(MLZ *mlz) {
    MLZDict *dict = mlz->dict;
    int i;
    for ( i = 0; i < TABLE_SIZE; i++ ) {
        dict[i].string_code = CODE_UNSET;
        dict[i].parent_code = CODE_UNSET;
        dict[i].match_len = 0;
    }
    mlz->current_dic_index_max = DIC_INDEX_INIT;
    mlz->dic_code_bit          = CODE_BIT_INIT;  // DicCodeBitInit;
    mlz->bump_code             = mlz->current_dic_index_max - 1;
    mlz->next_code             = FIRST_CODE;
    mlz->freeze_flag           = 0;
}

static void set_new_entry_dict(MLZDict* dict, int string_code, int parent_code, int char_code) {
    dict[string_code].parent_code = parent_code;
    dict[string_code].string_code = string_code;
    dict[string_code].char_code   = char_code;
    if (parent_code < FIRST_CODE) {
        dict[string_code].match_len = 2;
    } else {
        dict[string_code].match_len = (dict[parent_code].match_len) + 1;
    }
}

static int decode_string(MLZ* mlz, unsigned char *buff, int string_code, int *first_char_code, unsigned long bufsize) {
    MLZDict* dict = mlz->dict;
    unsigned long count, offset;
    int current_code, parent_code, tmp_code;

    count            = 0;
    current_code     = string_code;
    *first_char_code = CODE_UNSET;

    while (count < bufsize) {
        switch (current_code) {
        case CODE_UNSET:
            return count;
            break;
        default:
            if (current_code < FIRST_CODE) {
                *first_char_code = current_code;
                buff[0] = current_code;
                count++;
                return count;
            } else {
                offset  = dict[current_code].match_len - 1;
                tmp_code = dict[current_code].char_code;
                if (offset >= bufsize) {
                    av_log(mlz->context, AV_LOG_ERROR, "MLZ offset error.\n");
                    return count;
                }
                buff[offset] = tmp_code;
                count++;
            }
            current_code = dict[current_code].parent_code;
            if ((current_code < 0) || (current_code > (DIC_INDEX_MAX - 1))) {
                av_log(mlz->context, AV_LOG_ERROR, "MLZ dic index error.\n");
                return count;
            }
            if (current_code > FIRST_CODE) {
                parent_code = dict[current_code].parent_code;
                offset = (dict[current_code].match_len) - 1;
                if (parent_code < 0 || parent_code > DIC_INDEX_MAX-1) {
                    av_log(mlz->context, AV_LOG_ERROR, "MLZ dic index error.\n");
                    return count;
                }
                if (( offset > (DIC_INDEX_MAX - 1))) {
                    av_log(mlz->context, AV_LOG_ERROR, "MLZ dic offset error.\n");
                    return count;
                }
            }
            break;
        }
    }
    return count;
}

static int input_code(GetBitContext* gb, int len) {
    int tmp_code = 0;
    int i;
    for (i = 0; i < len; ++i) {
        tmp_code |= get_bits1(gb) << i;
    }
    return tmp_code;
}

int ff_mlz_decompression(MLZ* mlz, GetBitContext* gb, int size, unsigned char *buff) {
    MLZDict *dict = mlz->dict;
    unsigned long output_chars;
    int string_code, last_string_code, char_code;

    string_code = 0;
    char_code   = -1;
    last_string_code = -1;
    output_chars = 0;

    while (output_chars < size) {
        string_code = input_code(gb, mlz->dic_code_bit);
        switch (string_code) {
            case FLUSH_CODE:
            case MAX_CODE:
                ff_mlz_flush_dict(mlz);
                char_code = -1;
                last_string_code = -1;
                break;
            case FREEZE_CODE:
                mlz->freeze_flag = 1;
                break;
            default:
                if (string_code > mlz->current_dic_index_max) {
                    av_log(mlz->context, AV_LOG_ERROR, "String code %d exceeds maximum value of %d.\n", string_code, mlz->current_dic_index_max);
                    return output_chars;
                }
                if (string_code == (int) mlz->bump_code) {
                    ++mlz->dic_code_bit;
                    mlz->current_dic_index_max *= 2;
                    mlz->bump_code = mlz->current_dic_index_max - 1;
                } else {
                    if (string_code >= mlz->next_code) {
                        int ret = decode_string(mlz, &buff[output_chars], last_string_code, &char_code, size - output_chars);
                        if (ret < 0 || ret > size - output_chars) {
                            av_log(mlz->context, AV_LOG_ERROR, "output chars overflow\n");
                            return output_chars;
                        }
                        output_chars += ret;
                        ret = decode_string(mlz, &buff[output_chars], char_code, &char_code, size - output_chars);
                        if (ret < 0 || ret > size - output_chars) {
                            av_log(mlz->context, AV_LOG_ERROR, "output chars overflow\n");
                            return output_chars;
                        }
                        output_chars += ret;
                        set_new_entry_dict(dict, mlz->next_code, last_string_code, char_code);
                        if (mlz->next_code >= TABLE_SIZE - 1) {
                            av_log(mlz->context, AV_LOG_ERROR, "Too many MLZ codes\n");
                            return output_chars;
                        }
                        mlz->next_code++;
                    } else {
                        int ret = decode_string(mlz, &buff[output_chars], string_code, &char_code, size - output_chars);
                        if (ret < 0 || ret > size - output_chars) {
                            av_log(mlz->context, AV_LOG_ERROR, "output chars overflow\n");
                            return output_chars;
                        }
                        output_chars += ret;
                        if (output_chars <= size && !mlz->freeze_flag) {
                            if (last_string_code != -1) {
                                set_new_entry_dict(dict, mlz->next_code, last_string_code, char_code);
                                if (mlz->next_code >= TABLE_SIZE - 1) {
                                    av_log(mlz->context, AV_LOG_ERROR, "Too many MLZ codes\n");
                                    return output_chars;
                                }
                                mlz->next_code++;
                            }
                        } else {
                            break;
                        }
                    }
                    last_string_code = string_code;
                }
                break;
        }
    }
    return output_chars;
}



void mlz_create(MLZ* mlz)
{
    mlz_alloc_dict(mlz);
    mlz_init_dict(mlz);
    mlz_flush_dict(mlz);
}

void mlz_delete(MLZ* mlz)
{
    mlz_free_dict(mlz);
}

void mlz_alloc_dict(MLZ* mlz)
{
    int i;
    //pDict       = new DICT [ TABLE_SIZE ];
    //ppHashTable = new int * [ TABLE_SIZE ];
    //for ( i = 0; i < TABLE_SIZE; i++ ) ppHashTable[i] = new int [ WORD_SIZE ];
    mlz->p_dict = (MLZDict*)malloc(sizeof(MLZDict) * TABLE_SIZE);
    mlz->pp_hash_table = (int**)malloc(sizeof(int*) * TABLE_SIZE);
    for(i = 0; i < TABLE_SIZE; i++)
        mlz->pp_hash_table[i] = (int*)malloc(sizeof(int) * WORD_SIZE);


    //for encoder
    //b_pDict       = new DICT [ TABLE_SIZE ];
    //b_ppHashTable = new int * [ TABLE_SIZE ];
    //for ( i = 0; i < TABLE_SIZE; i++ ) b_ppHashTable[i] = new int [ WORD_SIZE ];
    mlz->bp_dict = (MLZDict*)malloc(sizeof(MLZDict) * TABLE_SIZE);
    mlz->bpp_hash_table = (int**)malloc(sizeof(int*) * TABLE_SIZE);
    for(i = 0; i < TABLE_SIZE; i++)
        mlz->bpp_hash_table[i] = (int*)malloc(sizeof(int) * WORD_SIZE);
}

void mlz_init_dict(MLZ* mlz)
{
    mlz->m_flush_code = FLUSH_CODE;
    mlz->m_current_dict_index_max = DIC_INDEX_INIT;
    mlz->m_dict_code_bit = CODE_BIT_INIT;
    mlz->m_bump_code = (DIC_INDEX_INIT) - 1;
    mlz->m_next_code = FIRST_CODE;
    mlz->m_freeze_flag = 0;
}




void mlz_flush_dict(MLZ* mlz)
{
    int i, j;
    for ( i = 0; i < TABLE_SIZE; i++ ) {
        mlz->p_dict[i].string_code = CODE_UNSET;
        mlz->p_dict[i].parent_code = CODE_UNSET;
        mlz->p_dict[i].match_len = 0;
        for ( j=0; j < WORD_SIZE; j++ )
            mlz->pp_hash_table[i][j] = CODE_UNSET;
    }
    //// read first part
    // initial DicCodes
    // $0 - 255 xxxx
    // $256 FLUSH_CODE
    // $257 FREEZE_CODE
    // $258 - $(max-2) code
    // $(max-1) BUMP_CODE
    // $(max-1) BumpCode  1st BumpCode = 511
    // add first entry to dictionary as [$258]
    mlz->m_current_dict_index_max = DIC_INDEX_INIT;
    mlz->m_dict_code_bit = CODE_BIT_INIT;  // DicCodeBitInit;
    mlz->m_bump_code = mlz->m_current_dict_index_max - 1;
    mlz->m_next_code = FIRST_CODE;
    mlz->m_freeze_flag = 0;
}

void mlz_free_dict(MLZ* mlz)
{
    int i;
    if ( mlz->p_dict != NULL ) {
        free(mlz->p_dict);
        mlz->p_dict = NULL;
    }
    if ( mlz->pp_hash_table != NULL ) {
        for ( i = 0; i < TABLE_SIZE; i++ ) free(mlz->pp_hash_table[i]);
        free(mlz->pp_hash_table);
        mlz->pp_hash_table = NULL;
    }

    if ( mlz->bp_dict != NULL ) {
        free(mlz->bp_dict);
        mlz->bp_dict = NULL;
    }
    if ( mlz->bpp_hash_table != NULL ) {
        for ( i = 0; i < TABLE_SIZE; i++ ) free(mlz->bpp_hash_table[i]);
        free(mlz->bpp_hash_table);
        mlz->bpp_hash_table = NULL;    }
}


void mlz_backup_dict(MLZ* mlz)
{
    int i, j;
    for ( i = 0; i < TABLE_SIZE; i++ ) {
        mlz->bp_dict[i].string_code = mlz->p_dict[i].string_code;
        mlz->bp_dict[i].char_code   = mlz->p_dict[i].char_code;
        mlz->bp_dict[i].match_len   = mlz->p_dict[i].match_len;
        mlz->bp_dict[i].parent_code = mlz->p_dict[i].parent_code;
        for ( j = 0; j < WORD_SIZE; j++ ) {
            mlz->bpp_hash_table[i][j] = mlz->pp_hash_table[i][j];
        }
    }
    mlz->b_current_dict_index_max = mlz->m_current_dict_index_max;
    mlz->b_dict_code_bit = mlz->m_dict_code_bit;
    mlz->b_bump_code = mlz->m_bump_code;
    mlz->b_next_code = mlz->m_next_code;
    mlz->b_freeze_flag = mlz->m_freeze_flag;
}


void mlz_resume_dict(MLZ* mlz)
{
    int i, j;
    for ( i = 0; i < TABLE_SIZE; i++ ) {
        mlz->p_dict[i].string_code = mlz->bp_dict[i].string_code;
        mlz->p_dict[i].char_code   = mlz->bp_dict[i].char_code;
        mlz->p_dict[i].match_len   = mlz->bp_dict[i].match_len;
        mlz->p_dict[i].parent_code = mlz->bp_dict[i].parent_code;
        for ( j = 0; j < WORD_SIZE; j++ ) {
            mlz->pp_hash_table[i][j] = mlz->bpp_hash_table[i][j];
        }
    }
    mlz->m_current_dict_index_max = mlz->b_current_dict_index_max;
    mlz->m_dict_code_bit         = mlz->b_dict_code_bit;
    mlz->m_bump_code           = mlz->b_bump_code;
    mlz->m_next_code           = mlz->b_next_code;
    mlz->m_freeze_flag         = mlz->b_freeze_flag;
}

unsigned long mlz_encode(     //ret: encoded size (bits)
 MLZ* mlz,
 unsigned char *p_input_buff,     //in: input string buffer inputBuff[sizeofInBuff]
 unsigned char *p_input_mask,     //in: input mask buffer inputMask[sizeofInBuff]
 unsigned long sizeof_input_buff, //in: buffer size
 unsigned char *p_encode_buff,    //out: coded data
 unsigned long sizeof_encode_buff //in: buffer size
)
{
    int             last_match_len, match_len;
    int             string_code, last_string_code, parent_code, char_code;
    unsigned long   position, output_bits;

    //set buffer information
    mlz->m_sizeof_input_buff   = sizeof_input_buff;
    mlz->m_sizeof_encode_buff  = sizeof_encode_buff;
    mlz->mp_input_buff        = p_input_buff;
    mlz->mp_input_mask        = p_input_mask;
    mlz->mp_encode_buff       = p_encode_buff;
    mlz->mp_out_position      = 0;

    position = 0;
    output_bits = 0;
    last_string_code = -1;

    while ( position < sizeof_input_buff ) {
        // search dictionary
        match_len = mlz_search_dict( mlz, last_string_code, &string_code, position ); // in: position / out: stringCode

        //Output Longest match code
        output_bits += mlz_output_code( mlz, string_code );
        if ( position + match_len >= sizeof_input_buff ) {
            position += match_len;
            break;
        } else {
            if (( mlz->m_next_code + 1 >= (int )mlz->m_bump_code ) && ( mlz->m_current_dict_index_max >= DIC_INDEX_MAX )) {
                output_bits += mlz_output_code( mlz, FLUSH_CODE );
                mlz_flush_dict(mlz);
                position += match_len;
                last_string_code = -1;
            } else {
                if ( mlz->m_next_code + 1 >= (int )mlz->m_bump_code ) {
                    output_bits += mlz_output_code( mlz, mlz->m_bump_code );
                    mlz->m_current_dict_index_max *= 2;
                    mlz->m_bump_code = mlz->m_current_dict_index_max - 1;
                    mlz->m_dict_code_bit++;
                }

                char_code = mlz_get_root_index( mlz, position + match_len );
                mlz_set_new_entry_to_dict_with_hash( mlz, mlz->m_next_code, string_code, char_code, match_len + 1 );

                parent_code = mlz->m_next_code;
                mlz->m_next_code++;
    
                position += match_len;
                last_string_code = char_code;
                last_match_len = match_len;
                // check index code
            }
        }
    }

    return output_bits;





}

int mlz_output_code(MLZ* mlz, int string_code)
{
    int  i;
    for ( i = 0; i < mlz->m_dict_code_bit; i++ ) {
        if ( mlz->mp_out_position >= mlz->m_sizeof_encode_buff ) return i;
        mlz->mp_encode_buff[mlz->mp_out_position++] = (unsigned char )( (string_code >> (mlz->m_dict_code_bit - i - 1)) & 0x01 );
    }
    return mlz->m_dict_code_bit;
}


int mlz_get_vacant_hash_index(MLZ* mlz, int parent_code, int char_code, int mask_size)
{
    int hash_index;
    int offset;

    hash_index = 0;
    hash_index = ( char_code << ( CODE_BIT_MAX - WORD_SIZE ) ) ^ parent_code;  // here, charCode == charCode & mask
    if ( hash_index == 0 )
        offset = 1;
    else
        offset = TABLE_SIZE - hash_index;
    while ( mlz->pp_hash_table[ hash_index ][ mask_size % WORD_SIZE ] != CODE_UNSET )
    {
        hash_index -= offset;
        if ( hash_index < 0 )
            hash_index += TABLE_SIZE;
    }
    return hash_index;
}

void mlz_set_new_entry_to_dict_with_hash(MLZ* mlz, int string_code, int parent_code, int char_code, int match_len)
{
    unsigned int mask;
    int hash_index;
    int i;
    
    // add stringCode to pDict
    mlz->p_dict[ string_code ].string_code = string_code;
    mlz->p_dict[ string_code ].parent_code = parent_code;
    mlz->p_dict[ string_code ].char_code   = char_code;
    mlz->p_dict[ string_code ].match_len   = match_len;

    hash_index = mlz_get_vacant_hash_index( mlz, parent_code, char_code, 0 );
    
    mlz->pp_hash_table[hash_index][0] = string_code;

    for ( i = 1; i < WORD_SIZE; i++ ) {
        mask = ( 0x01 << i ) - 1;
        mask <<= ( WORD_SIZE - i );
        hash_index = mlz_get_vacant_hash_index( mlz, parent_code, (char_code & mask), i );

        mlz->pp_hash_table[hash_index][i] = string_code;
    }
}


int mlz_get_hash_index(
  MLZ* mlz,
  int parent_code,       // in: parent index code of the dict.
  int char_code,     // in: charCode = charCode & mask
  int  mask_size,       // in: mask bit width
  int *p_candidates, // out: list of stringCodes
  int  num_index_max      // in: maxnum of candidates
)
{
    int mask;
    int  num_candidates, hash_index, offset;
    int dflag; 

    mask = ( 0x01 << mask_size ) - 0x01;
    mask <<= ( WORD_SIZE - mask_size );

    num_candidates = 0;
    hash_index = 0;
    hash_index = ( ( char_code & mask ) << ( CODE_BIT_MAX - WORD_SIZE ) ) ^ parent_code;  // here, charCode == charCode & mask
    if ( hash_index == 0 )
        offset = 1;
    else
        offset = TABLE_SIZE - hash_index;

    while ( mlz->pp_hash_table[ hash_index ][ mask_size % WORD_SIZE ] != CODE_UNSET )
    {
        dflag = 1;
        if ( mlz->p_dict[ mlz->pp_hash_table[ hash_index ][ mask_size % WORD_SIZE ] ].parent_code != parent_code ) {
            dflag = 0;
        }
        if ( char_code != ( mlz->p_dict[ mlz->pp_hash_table[ hash_index ][ mask_size % WORD_SIZE ] ].char_code & mask ) ) {    // needs to be compared with mask????
            dflag = 0;
        }
        if ( dflag == 1 ) {
            p_candidates[ num_candidates++ ] = mlz->pp_hash_table[ hash_index ][ mask_size % WORD_SIZE ]; //stringCode
            //return num_candidates;
            if ( num_candidates >= num_index_max ) {
                return num_candidates;
            }
        }
        hash_index -= offset;
        if(hash_index < 0)
            hash_index += TABLE_SIZE;
    }
    return num_candidates;





}

int mlz_get_root_index(MLZ* mlz, unsigned long position)
{
    int char_code;
    if ( position >= mlz->m_sizeof_input_buff ) return -1;
    char_code  = mlz->mp_input_buff[position];
    return char_code;
}

int mlz_search_dict(       // return: length of matched string indicated by the index
  MLZ* mlz,
  int last_char_code,     // stringCode which is based on the search
  int *string_code,          // out: index of the dict.
  unsigned long position    // in:  position of m_pInputBuff
)
{
    int match_len, ret_match_len;
    int last_string_code, ret_string_code, char_code;
    unsigned char mask_size;
    int hash_candidates[MAX_SEARCH];
    int  num_candidates;
    int  i;

    //********************
    //** FIND ROOT NODE **
    //********************
    // find first entry of first char
    if( position >= mlz->m_sizeof_input_buff ) {
        *string_code = -1;
        return 0;
    }
    if ( last_char_code < 0 ) {
        last_string_code = mlz_get_root_index( mlz, position );
        match_len = 1;
    } else if ( last_char_code < FIRST_CODE ) {
        last_string_code = last_char_code;
        match_len = 1;
    } else {
        last_string_code = last_char_code;
        match_len = mlz->p_dict[ last_string_code ].match_len;
    }

    //*********************
    //** FIND CHILD NODE **
    //*********************
    // find longer entry
    *string_code  = last_string_code;
    if ( position + 1 < mlz->m_sizeof_input_buff ) {
        char_code  = mlz->mp_input_buff[position + 1];
        mask_size = mlz->mp_input_mask[position + 1];
        num_candidates = mlz_get_hash_index( mlz, last_string_code, char_code, mask_size, hash_candidates, MAX_SEARCH );
        if ( num_candidates == 0 ) {
            return match_len; 
        } else {
            if ( position + 2 < mlz->m_sizeof_input_buff ) {
                for ( i = 0; i < num_candidates; i++ ) {
                    ret_match_len = mlz_search_dict( mlz, hash_candidates[i], &ret_string_code, position + 1 );
                    if ( ret_match_len > match_len ) {
                        match_len    = ret_match_len;
                        *string_code = ret_string_code;
                    }
                }
            }
        }
    }
    return match_len;



}


