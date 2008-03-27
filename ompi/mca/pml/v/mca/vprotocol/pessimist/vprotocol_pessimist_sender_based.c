/*
 * Copyright (c) 2004-2007 The Trustees of the University of Tennessee.
 *                         All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "ompi_config.h"
#include "vprotocol_pessimist_sender_based.h"
#include <sys/types.h>
#if defined(HAVE_SYS_MMAN_H)
#include <sys/mman.h>
#endif  /* defined(HAVE_SYS_MMAN_H) */
#if defined(HAVE_UNISTD_H)
#include <unistd.h>
#endif
#include <string.h>
#include <errno.h>
#include "ompi/datatype/datatype_memcpy.h"
#include <fcntl.h>

#include "orte/util/proc_info.h"

#define sb mca_vprotocol_pessimist.sender_based

static int sb_mmap_file_open(const char *path)
{
#if defined(__WINDOWS__)
    sb.sb_fd = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, 
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if(INVALID_HANDLE_VALUE == sb.sb_fd) 
    { 
        V_OUTPUT_ERR("pml_v: vprotocol_pessimist: sender_based_init: open (%s): %s", 
                     path, GetLastError());
        return OPAL_ERR_FILE_OPEN_FAILURE;
    }
    return OPAL_SUCCESS;
#else    
    sb.sb_fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
    if(-1 == sb.sb_fd)
    {
        V_OUTPUT_ERR("pml_v: vprotocol_pessimist: sender_based_init: open (%s): %s", 
                     path, strerror(errno));
        return OPAL_ERR_FILE_OPEN_FAILURE;
    }
    return OPAL_SUCCESS;
#endif
}

static void sb_mmap_file_close(void)
{
#if defined(__WINDOWS__)    
    CloseHandle(sb.sb_fd);
#else
    int ret = close(sb.sb_fd);
    if(-1 == ret)
        V_OUTPUT_ERR("pml_v: protocol_pessimist: sender_based_finalize: close (%d): %s", 
                     sb.sb_fd, strerror(errno));
#endif
}
    
static void sb_mmap_alloc(void)
{
#if defined(__WINDOWS__)
    sb.sb_map = CreateFileMapping(sb.sb_fd, NULL, PAGE_READWRITE, 0, 
                                  (DWORD)sb.sb_offset + sb.sb_length,  NULL); 
    if(NULL == sb.sb_map) 
    {
        V_OUTPUT_ERR("pml_v: vprotocol_pessimist: sender_based_alloc: CreateFileMapping : %s", 
                     GetLastError());
        ompi_mpi_abort(MPI_COMM_NULL, MPI_ERR_NO_SPACE, false);
    }
    
    sb.sb_addr = (uintptr_t) MapViewOfFile(sb.sb_map, FILE_MAP_ALL_ACCESS, 0, 
                                           sb.sb_offset, sb.sb_length); 
    if(NULL == (void*)sb.sb_addr) 
    {
        V_OUTPUT_ERR("pml_v: vprotocol_pessimist: sender_based_alloc: mmap: %s", 
                     GetLastError());
        CloseHandle(sb.sb_map);
        CloseHandle(sb.sb_fd);
        ompi_mpi_abort(MPI_COMM_NULL, MPI_ERR_NO_SPACE, false);    
    }
#else    
#ifndef MAP_NOCACHE
#   define MAP_NOCACHE 0
#endif
    if(-1 == ftruncate(sb.sb_fd, sb.sb_offset + sb.sb_length))
    {
        V_OUTPUT_ERR("pml_v: vprotocol_pessimist: sender_based_alloc: ftruncate: %s", 
                     strerror(errno));
        close(sb.sb_fd);
        ompi_mpi_abort(MPI_COMM_NULL, MPI_ERR_NO_SPACE, false);
    }
    sb.sb_addr = (uintptr_t) mmap((void *) sb.sb_addr, sb.sb_length, 
                                  PROT_WRITE | PROT_READ, 
                                  MAP_PRIVATE | MAP_NOCACHE, sb.sb_fd, 
                                  sb.sb_offset);
    if(((uintptr_t) -1) == sb.sb_addr)
    {
        V_OUTPUT_ERR("pml_v: vprotocol_pessimist: sender_based_alloc: mmap: %s", 
                     strerror(errno));
        close(sb.sb_fd);
        ompi_mpi_abort(MPI_COMM_NULL, MPI_ERR_NO_SPACE, false);
    }
#endif
}

static void sb_mmap_free(void)
{
#if    defined(__WINDOWS__)
    UnmapViewOfFile( (LPCVOID)sb.sb_addr);
    CloseHandle(sb.sb_map);
#else
    int ret = munmap((void *) sb.sb_addr, sb.sb_length);
    if(-1 == ret)
        V_OUTPUT_ERR("pml_v: protocol_pessimsit: sender_based_finalize: munmap (%p): %s", 
                     (void *) sb.sb_addr, strerror(errno));
#endif
}

int vprotocol_pessimist_sender_based_init(const char *mmapfile, size_t size) 
{
    char path[PATH_MAX];
#ifdef SB_USE_CONVERTOR_METHOD
    mca_pml_base_send_request_t pml_req;
    sb.sb_conv_to_pessimist_offset = VPROTOCOL_SEND_REQ(NULL) - 
            ((uintptr_t) & pml_req.req_base.req_convertor - 
             (uintptr_t) & pml_req);
    V_OUTPUT_VERBOSE(500, "pessimist: conv_to_pessimist_offset: %p", (void *) sb.sb_conv_to_pessimist_offset);
#endif
    sb.sb_offset = 0;
    sb.sb_length = size;
    sb.sb_pagesize = getpagesize();
    sb.sb_cursor = sb.sb_addr = (uintptr_t) NULL;
    sb.sb_available = 0;
    
    sprintf(path, "%s"OPAL_PATH_SEP"%s", orte_process_info.proc_session_dir, 
                mmapfile);
    if(OPAL_SUCCESS != sb_mmap_file_open(path))
        return OPAL_ERR_FILE_OPEN_FAILURE; 
    return OMPI_SUCCESS;
}

void vprotocol_pessimist_sender_based_finalize(void)
{
    if(((uintptr_t) NULL) != sb.sb_addr)
        sb_mmap_free();
    sb_mmap_file_close();
}


/** Manage mmap floating window, allocating enough memory for the message to be 
  * asynchronously copied to disk.
  */
void vprotocol_pessimist_sender_based_alloc(size_t len)
{
    if(((uintptr_t) NULL) != sb.sb_addr)
        sb_mmap_free();
#ifdef SB_USE_SELFCOMM_METHOD
    else
        ompi_comm_dup(MPI_COMM_SELF, &sb.sb_comm, 1);
#endif
    
    /* Take care of alignement of sb_offset                             */
    sb.sb_offset += sb.sb_cursor - sb.sb_addr;
    sb.sb_cursor = sb.sb_offset % sb.sb_pagesize;
    sb.sb_offset -= sb.sb_cursor; 

    /* Adjusting sb_length for the largest application message to fit   */
    len += sb.sb_cursor + sizeof(vprotocol_pessimist_sender_based_header_t);
    if(sb.sb_length < len)
        sb.sb_length = len;
    /* How much space left for application data */
    sb.sb_available = sb.sb_length - sb.sb_cursor;

    sb_mmap_alloc();
    
    sb.sb_cursor += sb.sb_addr; /* set absolute addr of sender_based buffer */
    V_OUTPUT_VERBOSE(30, "pessimist:\tsb\tgrow\toffset %llu\tlength %llu\tbase %p\tcursor %p", (unsigned long long) sb.sb_offset, (unsigned long long) sb.sb_length, (void *) sb.sb_addr, (void *) sb.sb_cursor);
}   

#ifdef SB_USE_CONVERTOR_METHOD
int32_t vprotocol_pessimist_sender_based_convertor_advance(ompi_convertor_t* pConvertor,
                                                            struct iovec* iov,
                                                            uint32_t* out_size,
                                                            size_t* max_data) {
    int ret;
    unsigned int i;
    size_t pending_length;
    mca_vprotocol_pessimist_send_request_t *ftreq;
    
    ftreq = VPESSIMIST_CONV_REQ(pConvertor);
    pConvertor->flags = ftreq->sb_conv_flags;
    pConvertor->fAdvance = ftreq->sb_conv_advance;
    ret = ompi_convertor_pack(pConvertor, iov, out_size, max_data);
    V_OUTPUT_VERBOSE(39, "pessimist:\tsb\tpack\t%"PRIsize_t, *max_data);

    for(i = 0, pending_length = *max_data; pending_length > 0; i++) {
        assert(i < *out_size);
        MEMCPY((void *) ftreq->sb_cursor, iov[i].iov_base, iov[i].iov_len);
        pending_length -= iov[i].iov_len;
        ftreq->sb_cursor += iov[i].iov_len;
    }
    assert(pending_length == 0)

    pConvertor->flags &= ~CONVERTOR_NO_OP;
    pConvertor->fAdvance = &vprotocol_pessimist_sender_based_convertor_advance;
    return ret;
}
#endif

#undef sb
