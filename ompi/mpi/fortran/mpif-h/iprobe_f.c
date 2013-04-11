/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2011-2012 Cisco Systems, Inc.  All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#include "ompi_config.h"

#include "ompi/mpi/fortran/mpif-h/bindings.h"
#include "ompi/mpi/fortran/base/constants.h"
#include "ompi/communicator/communicator.h"

#if OPAL_HAVE_WEAK_SYMBOLS && OMPI_PROFILE_LAYER
#pragma weak PMPI_IPROBE = ompi_iprobe_f
#pragma weak pmpi_iprobe = ompi_iprobe_f
#pragma weak pmpi_iprobe_ = ompi_iprobe_f
#pragma weak pmpi_iprobe__ = ompi_iprobe_f

#pragma weak PMPI_Iprobe_f = ompi_iprobe_f
#pragma weak PMPI_Iprobe_f08 = ompi_iprobe_f
#elif OMPI_PROFILE_LAYER
OMPI_GENERATE_F77_BINDINGS (PMPI_IPROBE,
                           pmpi_iprobe,
                           pmpi_iprobe_,
                           pmpi_iprobe__,
                           pompi_iprobe_f,
                           (MPI_Fint *source, MPI_Fint *tag, MPI_Fint *comm, ompi_fortran_logical_t *flag, MPI_Fint *status, MPI_Fint *ierr),
                           (source, tag, comm, flag, status, ierr) )
#endif

#if OPAL_HAVE_WEAK_SYMBOLS
#pragma weak MPI_IPROBE = ompi_iprobe_f
#pragma weak mpi_iprobe = ompi_iprobe_f
#pragma weak mpi_iprobe_ = ompi_iprobe_f
#pragma weak mpi_iprobe__ = ompi_iprobe_f

#pragma weak MPI_Iprobe_f = ompi_iprobe_f
#pragma weak MPI_Iprobe_f08 = ompi_iprobe_f
#endif

#if ! OPAL_HAVE_WEAK_SYMBOLS && ! OMPI_PROFILE_LAYER
OMPI_GENERATE_F77_BINDINGS (MPI_IPROBE,
                           mpi_iprobe,
                           mpi_iprobe_,
                           mpi_iprobe__,
                           ompi_iprobe_f,
                           (MPI_Fint *source, MPI_Fint *tag, MPI_Fint *comm, ompi_fortran_logical_t *flag, MPI_Fint *status, MPI_Fint *ierr),
                           (source, tag, comm, flag, status, ierr) )
#endif


#if OMPI_PROFILE_LAYER && ! OPAL_HAVE_WEAK_SYMBOLS
#include "ompi/mpi/fortran/mpif-h/profile/defines.h"
#endif

void ompi_iprobe_f(MPI_Fint *source, MPI_Fint *tag, MPI_Fint *comm,
                  ompi_fortran_logical_t *flag, MPI_Fint *status, MPI_Fint *ierr)
{
    int c_ierr;
    MPI_Status *c_status;
    MPI_Comm c_comm;
#if OMPI_SIZEOF_FORTRAN_INTEGER != SIZEOF_INT
    MPI_Status c_status2;
#endif
    OMPI_LOGICAL_NAME_DECL(flag);

    c_comm = MPI_Comm_f2c (*comm);

    /* See if we got MPI_STATUS_IGNORE */
    if (OMPI_IS_FORTRAN_STATUS_IGNORE(status)) {
        c_status = MPI_STATUS_IGNORE;
    } else {

        /* If sizeof(int) == sizeof(INTEGER), then there's no
           translation necessary -- let the underlying functions write
           directly into the Fortran status */

#if OMPI_SIZEOF_FORTRAN_INTEGER == SIZEOF_INT
        c_status = (MPI_Status *) status;
#else
        c_status = &c_status2;
#endif
    }

    c_ierr = MPI_Iprobe(OMPI_FINT_2_INT(*source),
                        OMPI_FINT_2_INT(*tag),
                        c_comm, OMPI_LOGICAL_SINGLE_NAME_CONVERT(flag),
                        c_status);
    if (NULL != ierr) *ierr = OMPI_INT_2_FINT(c_ierr);

    if (MPI_SUCCESS == c_ierr) {
        OMPI_SINGLE_INT_2_LOGICAL(flag);
#if OMPI_SIZEOF_FORTRAN_INTEGER != SIZEOF_INT
        if (MPI_STATUS_IGNORE != c_status) {
            MPI_Status_c2f(c_status, status);
        }
#endif
    }
}
