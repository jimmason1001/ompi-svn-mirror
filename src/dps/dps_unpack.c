/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University.
 *                         All rights reserved.
 * Copyright (c) 2004-2005 The Trustees of the University of Tennessee.
 *                         All rights reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */
 
/*
 * DPS Buffer Operations
 */
 
/** @file:
 *
 */

#include "orte_config.h"

#include <sys/types.h>
#include <netinet/in.h>

#include "mca/ns/ns_types.h"
#include "dps_internal.h"

/**
 * DPS UNPACK VALUE
 */

int orte_dps_unpack(orte_buffer_t *buffer, void *dst,
                    size_t *max_num_vals,
                    orte_data_type_t type)
{
    int rc=ORTE_SUCCESS;
    size_t num_vals;
    size_t mem_left;
    size_t num_bytes;
    void *src;
    uint32_t * s32;
    orte_data_type_t stored_type;

    /* check for errors */
    if (buffer == NULL || dst == NULL || max_num_vals == NULL) { 
        return (ORTE_ERR_BAD_PARAM); 
    }

    src = buffer->from_ptr;  /* get location in buffer */
    mem_left = buffer->toend;  /* how much data is left in buffer */

    /* check to see if there is enough in the buffer to hold the pack type */
    if (mem_left < sizeof(uint32_t)) {
        return ORTE_ERR_UNPACK_FAILURE;
    }

    /* first thing in the current buffer space must be the type */
    s32 = (uint32_t *) src;
    stored_type = (orte_data_type_t)ntohl(*s32);
    s32 ++;
    src = (void *) s32;
    mem_left -= sizeof(uint32_t);

    if(type == ORTE_INT || type == ORTE_UINT) {
        switch(sizeof(int)) {
            case 1:
                type = (type == ORTE_INT) ? ORTE_INT8 : ORTE_UINT8;
                break;
            case 2:
                type = (type == ORTE_INT) ? ORTE_INT16 : ORTE_UINT16;
                break;
            case 4:
                type = (type == ORTE_INT) ? ORTE_INT32 : ORTE_UINT32;
                break;
            case 8:
                type = (type == ORTE_INT) ? ORTE_INT64 : ORTE_UINT64;
                break;
            default:
                return ORTE_ERR_NOT_IMPLEMENTED;
        }
    }

    /* check for type match - for now we require this to be an exact match -
     * though we should probably support conversions when there is no loss
     * of precision.
     */
    if (stored_type != type) {
        return ORTE_PACK_MISMATCH;
    }
    
    /* got enough left for num_vals? */
    if (sizeof(uint32_t) > mem_left) { /* not enough memory  */
        return ORTE_ERR_UNPACK_FAILURE;
    }

    /* unpack the number of values */
    s32 = (uint32_t *) src;
    num_vals = (size_t)ntohl(*s32);
    if (num_vals > *max_num_vals) {  /* not enough space provided */
        return ORTE_UNPACK_INADEQUATE_SPACE;
    }
    s32++;
    src = (void *)s32;
    mem_left -= sizeof(uint32_t);

    /* will check to see if adequate storage in buffer prior
     * to unpacking the item
     */
    if (ORTE_SUCCESS != (rc = orte_dps_unpack_nobuffer(dst, src, num_vals,
                                    stored_type, &mem_left, &num_bytes))) {
        return rc;
    }
    
    /* ok, we managed to unpack some stuff, so update all ptrs/cnts */
    buffer->from_ptr = (void*)((char*)src + num_bytes);
    buffer->toend = mem_left; /* closer to the end */
    buffer->len   -= num_bytes; /* and less data left */

    /* return the number of values unpacked */
    *max_num_vals = num_vals;
    return rc;
}


int orte_dps_unpack_nobuffer(void *dst, void *src, size_t num_vals,
                             orte_data_type_t type,
                             size_t *mem_left, size_t *num_bytes)
{
    int rc;
    size_t i;
    uint16_t * d16;
    uint32_t * d32;
    uint16_t * s16;
    uint32_t * s32;
    uint8_t* bool_src;
    bool *bool_dst;
    char **dstr;
    orte_process_name_t* dn;
    orte_process_name_t* sn;
    orte_byte_object_t* dbyteptr;
    uint32_t len;
    char *str, *sstr;
    void *sptr;

    /* defaults */
    rc = ORTE_SUCCESS;
    *num_bytes = 0;
    
    switch(type) {
       
        case ORTE_BYTE:
        case ORTE_INT8:
        case ORTE_UINT8:

            if (num_vals > *mem_left) {
                num_vals = *mem_left;
                rc = ORTE_UNPACK_READ_PAST_END_OF_BUFFER;
            }
            memcpy(dst, src, num_vals);
            *num_bytes = num_vals * sizeof(uint8_t);
            break;
            
        case ORTE_INT16:
        case ORTE_UINT16:
       
            if(num_vals * sizeof(uint16_t) > *mem_left) {
                num_vals = *mem_left / sizeof(uint16_t);
                rc = ORTE_UNPACK_READ_PAST_END_OF_BUFFER;
            }
            s16 = (uint16_t *) src;
            d16 = (uint16_t *) dst;
            for(i=0; i<num_vals; i++) {
                /* convert the network order to host order */
                *d16 = ntohs(*s16);
                 d16++; s16++;
            }
            *num_bytes = num_vals * sizeof(uint16_t);
            break;
            
        case ORTE_INT32:
        case ORTE_UINT32:

            if(num_vals * sizeof(uint32_t) > *mem_left) {
                num_vals = *mem_left / sizeof(uint32_t);
                rc = ORTE_UNPACK_READ_PAST_END_OF_BUFFER;
            }
            s32 = (uint32_t *) src;
            d32 = (uint32_t *) dst;
            for(i=0; i<num_vals; i++) {
                /* convert the network order to host order */
                *d32 = ntohl(*s32);
                 d32++; s32++;
            }
            *num_bytes = num_vals * sizeof(uint32_t);
            break;
        
        case ORTE_INT64:
        case ORTE_UINT64:
            return ORTE_ERR_NOT_IMPLEMENTED;
            break;
                                                                                                            
        case ORTE_FLOAT:
        case ORTE_FLOAT4:
        case ORTE_FLOAT8:
        case ORTE_FLOAT12:
        case ORTE_FLOAT16:
        case ORTE_DOUBLE:
        case ORTE_LONG_DOUBLE:
            return ORTE_ERR_NOT_IMPLEMENTED;
            break;

        case ORTE_BOOL:

            if(num_vals * sizeof(uint8_t) > *mem_left) {
                num_vals = *mem_left / sizeof(uint8_t);
                rc = ORTE_UNPACK_READ_PAST_END_OF_BUFFER;
            }
            bool_src = (uint8_t *) src;
            bool_dst = (bool *) dst;
            for(i=0; i<num_vals; i++) {
                /* convert packed uint8_t to native bool */
                *bool_dst = (*bool_src) ? true : false;
                 bool_dst++; bool_src++;
            }
            *num_bytes = num_vals * sizeof(uint8_t);
            break;

        case ORTE_NAME:

            dn = (orte_process_name_t*) dst;
            sn = (orte_process_name_t*) src;
            for (i=0; i<num_vals; i++) {
                dn->cellid = ntohl(sn->cellid);
                dn->jobid = ntohl(sn->jobid);
                dn->vpid = ntohl(sn->vpid);
                dn++; sn++;
            }
            *num_bytes = num_vals * sizeof(orte_process_name_t);
            break;
        
        case ORTE_STRING:
 
            dstr = (char**)dst;
            sstr = (char *) src;
            for(i=0; i<num_vals; i++) {
                if(*mem_left < sizeof(uint32_t)) {
                    return ORTE_UNPACK_READ_PAST_END_OF_BUFFER;
                }
                d32 = (uint32_t*)sstr;
                len = ntohl(*d32);
                d32++;
                sstr= (char*)d32;
                *num_bytes += sizeof(uint32_t);
                *mem_left -= sizeof(uint32_t);
                if(*mem_left < len) {
                    return ORTE_UNPACK_READ_PAST_END_OF_BUFFER;
                }
                if(NULL == (str = malloc(len+1)))
                    return ORTE_ERR_OUT_OF_RESOURCE;
                memcpy(str,sstr,len);
                str[len] = '\0';
                dstr[i] = str;
                sstr = (char*)(sstr + len);
                *mem_left -= len;
                *num_bytes += len;
            }
            return ORTE_SUCCESS;
            break;

        case ORTE_BYTE_OBJECT:
 
            dbyteptr = (orte_byte_object_t*)dst;
            sptr = src; /* iterate from start of buffer */
            for(i=0; i<num_vals; i++) {
                if(*mem_left < sizeof(uint32_t)) {
                    return ORTE_UNPACK_READ_PAST_END_OF_BUFFER;
                }
                d32 = (uint32_t*)sptr;
                dbyteptr->size = (size_t)ntohl(*d32);
                d32++;
                sptr = (void*)d32;
                *mem_left -= sizeof(uint32_t);
                *num_bytes += sizeof(uint32_t);
                if(*mem_left < dbyteptr->size) {
                    return ORTE_UNPACK_READ_PAST_END_OF_BUFFER;
                }
                if(NULL == (dbyteptr->bytes = malloc(dbyteptr->size)))
                    return ORTE_ERR_OUT_OF_RESOURCE;
                memcpy(dbyteptr->bytes,sptr,dbyteptr->size);
                sptr = (void*)((uint8_t*)sptr + dbyteptr->size);
                *mem_left -= dbyteptr->size;
                *num_bytes += dbyteptr->size;
                dbyteptr++;
            }
            return ORTE_SUCCESS;
            break;

        case ORTE_NULL:
            break;

        default:
            return ORTE_ERROR;
    }
    
    *mem_left -= *num_bytes;
    return ORTE_SUCCESS;
}
