/*
 * $HEADER$
 */

#include "lam_config.h"

#include <stdio.h>

#include "mpi.h"
#include "mpi/f77/bindings.h"

#if LAM_HAVE_WEAK_SYMBOLS && LAM_PROFILE_LAYER
#pragma weak PMPI_FILE_WRITE_AT = mpi_file_write_at_f
#pragma weak pmpi_file_write_at = mpi_file_write_at_f
#pragma weak pmpi_file_write_at_ = mpi_file_write_at_f
#pragma weak pmpi_file_write_at__ = mpi_file_write_at_f
#elif LAM_PROFILE_LAYER
LAM_GENERATE_F77_BINDINGS (PMPI_FILE_WRITE_AT,
                           pmpi_file_write_at,
                           pmpi_file_write_at_,
                           pmpi_file_write_at__,
                           pmpi_file_write_at_f,
                           (MPI_Fint *fh, MPI_Fint *offset, char *buf, MPI_Fint *count, MPI_Fint *datatype, MPI_Fint *status, MPI_Fint *ierr),
                           (fh, offset, buf, count, datatype, status, ierr) )
#endif

#if LAM_HAVE_WEAK_SYMBOLS
#pragma weak MPI_FILE_WRITE_AT = mpi_file_write_at_f
#pragma weak mpi_file_write_at = mpi_file_write_at_f
#pragma weak mpi_file_write_at_ = mpi_file_write_at_f
#pragma weak mpi_file_write_at__ = mpi_file_write_at_f
#endif

#if ! LAM_HAVE_WEAK_SYMBOLS && ! LAM_PROFILE_LAYER
LAM_GENERATE_F77_BINDINGS (MPI_FILE_WRITE_AT,
                           mpi_file_write_at,
                           mpi_file_write_at_,
                           mpi_file_write_at__,
                           mpi_file_write_at_f,
                           (MPI_Fint *fh, MPI_Fint *offset, char *buf, MPI_Fint *count, MPI_Fint *datatype, MPI_Fint *status, MPI_Fint *ierr),
                           (fh, offset, buf, count, datatype, status, ierr) )
#endif

void mpi_file_write_at_f(MPI_Fint *fh, MPI_Fint *offset, char *buf, MPI_Fint *count, MPI_Fint *datatype, MPI_Fint *status, MPI_Fint *ierr)
{

}
