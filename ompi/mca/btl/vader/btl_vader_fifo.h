/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2004-2007 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2009 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2006-2007 Voltaire. All rights reserved.
 * Copyright (c) 2009-2010 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2010-2013 Los Alamos National Security, LLC.  
 *                         All rights reserved. 
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */
/**
 * @file
 */
#ifndef MCA_BTL_VADER_FIFO_H
#define MCA_BTL_VADER_FIFO_H

#include "btl_vader.h"
#include "btl_vader_endpoint.h"
#include "btl_vader_frag.h"

#define VADER_FIFO_FREE  ((int64_t)-2)

/*
 * Shared Memory FIFOs
 *
 * The FIFO is implemented as a linked list of frag headers. The fifo has multiple
 * producers and a single consumer (in the single thread case) so the tail needs
 * to be modified by an atomic or protected by a atomic lock.
 *
 * Since the frags live in shared memory that is mapped differently into
 * each address space, the head and tail pointers are relative (each process must
 * add its own offset).
 *
 * We introduce some padding at the end of the structure but it is probably unnecessary.
 */

/* lock free fifo */
typedef struct vader_fifo_t {
    volatile int64_t fifo_head;
    volatile int64_t fifo_tail;
} vader_fifo_t;

/* large enough to ensure the fifo is on its own cache line */
#define MCA_BTL_VADER_FIFO_SIZE 128

static inline mca_btl_vader_hdr_t *vader_fifo_read (vader_fifo_t *fifo)
{
    mca_btl_vader_hdr_t *hdr;
    int64_t value;

    opal_atomic_rmb ();

    value = opal_atomic_swap_64 (&fifo->fifo_head, VADER_FIFO_FREE);
    if (VADER_FIFO_FREE == value) {
        /* fifo is empty or we lost the race with another thread */
        return NULL;
    }

    hdr = (mca_btl_vader_hdr_t *) relative2virtual (value);

    assert (hdr->next != value);

    if (OPAL_UNLIKELY(VADER_FIFO_FREE == hdr->next)) {
        opal_atomic_rmb();

        if (!opal_atomic_cmpset_ptr (&fifo->fifo_tail, (void *)value,
                                     (void *)VADER_FIFO_FREE)) {
            while (VADER_FIFO_FREE == hdr->next) {
                opal_atomic_rmb ();
            }

            fifo->fifo_head = hdr->next;
        }
    } else {
        fifo->fifo_head = hdr->next;
    }

    opal_atomic_wmb ();

    return hdr; 
}

static inline int vader_fifo_init (vader_fifo_t *fifo)
{
    fifo->fifo_head = fifo->fifo_tail = VADER_FIFO_FREE;
    mca_btl_vader_component.my_fifo = fifo;

    return OMPI_SUCCESS;
}

static inline void vader_fifo_write (vader_fifo_t *fifo, int64_t value)
{
    int64_t prev;

    opal_atomic_wmb ();
    prev = opal_atomic_swap_64 (&fifo->fifo_tail, value);
    opal_atomic_rmb ();

    assert (prev != value);

    if (OPAL_LIKELY(VADER_FIFO_FREE != prev)) {
        mca_btl_vader_hdr_t *hdr = (mca_btl_vader_hdr_t *) relative2virtual (prev);
        hdr->next = value;
    } else {
        fifo->fifo_head = value;
    }

    opal_atomic_wmb ();
}

/* write a frag (relative to this process' base) to another rank's fifo */
static inline void vader_fifo_write_ep (mca_btl_vader_hdr_t *hdr, struct mca_btl_base_endpoint_t *ep)
{
    hdr->next = VADER_FIFO_FREE;
    vader_fifo_write (ep->fifo, virtual2relative ((char *) hdr));
}

/* write a frag (relative to the remote process' base) to the remote fifo. note the remote peer must own hdr */
static inline void vader_fifo_write_back (mca_btl_vader_hdr_t *hdr, struct mca_btl_base_endpoint_t *ep)
{
    hdr->next = VADER_FIFO_FREE;
    vader_fifo_write(ep->fifo, virtual2relativepeer (ep, (char *) hdr));
}

#endif /* MCA_BTL_VADER_FIFO_H */
