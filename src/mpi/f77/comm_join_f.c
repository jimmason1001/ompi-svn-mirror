/*
 * $HEADER$
 */

#include "lam_config.h"

#include <stdio.h>

#include "mpi.h"
#include "mpi/f77/bindings.h"

#if LAM_HAVE_WEAK_SYMBOLS && LAM_PROFILE_LAYER
#pragma weak PMPI_COMM_JOIN = mpi_comm_join_f
#pragma weak pmpi_comm_join = mpi_comm_join_f
#pragma weak pmpi_comm_join_ = mpi_comm_join_f
#pragma weak pmpi_comm_join__ = mpi_comm_join_f
#elif LAM_PROFILE_LAYER
LAM_GENERATE_F77_BINDINGS (PMPI_COMM_JOIN,
                           pmpi_comm_join,
                           pmpi_comm_join_,
                           pmpi_comm_join__,
                           pmpi_comm_join_f,
                           (MPI_Fint *fd, MPI_Fint *intercomm, MPI_Fint *ierr),
                           (fd, intercomm, ierr) )
#endif

#if LAM_HAVE_WEAK_SYMBOLS
#pragma weak MPI_COMM_JOIN = mpi_comm_join_f
#pragma weak mpi_comm_join = mpi_comm_join_f
#pragma weak mpi_comm_join_ = mpi_comm_join_f
#pragma weak mpi_comm_join__ = mpi_comm_join_f
#endif

#if ! LAM_HAVE_WEAK_SYMBOLS && ! LAM_PROFILE_LAYER
LAM_GENERATE_F77_BINDINGS (MPI_COMM_JOIN,
                           mpi_comm_join,
                           mpi_comm_join_,
                           mpi_comm_join__,
                           mpi_comm_join_f,
                           (MPI_Fint *fd, MPI_Fint *intercomm, MPI_Fint *ierr),
                           (fd, intercomm, ierr) )
#endif

void mpi_comm_join_f(MPI_Fint *fd, MPI_Fint *intercomm, MPI_Fint *ierr)
{

}
