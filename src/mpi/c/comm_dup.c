/*
 * $HEADERS$
 */
#include "lam_config.h"
#include <stdio.h>

#include "mpi.h"
#include "mpi/c/bindings.h"

#if LAM_HAVE_WEAK_SYMBOLS && LAM_PROFILING_DEFINES
#pragma weak MPI_Comm_dup = PMPI_Comm_dup
#endif

int MPI_Comm_dup(MPI_Comm comm, MPI_Comm *newcomm) {
    return MPI_SUCCESS;
}
