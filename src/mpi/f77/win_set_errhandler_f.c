/*
 * $HEADER$
 */

#include "lam_config.h"

#include <stdio.h>

#include "mpi.h"
#include "mpi/f77/bindings.h"

#if LAM_HAVE_WEAK_SYMBOLS && LAM_PROFILE_LAYER
#pragma weak PMPI_WIN_SET_ERRHANDLER = mpi_win_set_errhandler_f
#pragma weak pmpi_win_set_errhandler = mpi_win_set_errhandler_f
#pragma weak pmpi_win_set_errhandler_ = mpi_win_set_errhandler_f
#pragma weak pmpi_win_set_errhandler__ = mpi_win_set_errhandler_f
#elif LAM_PROFILE_LAYER
LAM_GENERATE_F77_BINDINGS (PMPI_WIN_SET_ERRHANDLER,
                           pmpi_win_set_errhandler,
                           pmpi_win_set_errhandler_,
                           pmpi_win_set_errhandler__,
                           pmpi_win_set_errhandler_f,
                           (MPI_Fint *win, MPI_Fint *errhandler, MPI_Fint *ierr),
                           (win, errhandler, ierr) )
#endif

#if LAM_HAVE_WEAK_SYMBOLS
#pragma weak MPI_WIN_SET_ERRHANDLER = mpi_win_set_errhandler_f
#pragma weak mpi_win_set_errhandler = mpi_win_set_errhandler_f
#pragma weak mpi_win_set_errhandler_ = mpi_win_set_errhandler_f
#pragma weak mpi_win_set_errhandler__ = mpi_win_set_errhandler_f
#endif

#if ! LAM_HAVE_WEAK_SYMBOLS && ! LAM_PROFILE_LAYER
LAM_GENERATE_F77_BINDINGS (MPI_WIN_SET_ERRHANDLER,
                           mpi_win_set_errhandler,
                           mpi_win_set_errhandler_,
                           mpi_win_set_errhandler__,
                           mpi_win_set_errhandler_f,
                           (MPI_Fint *win, MPI_Fint *errhandler, MPI_Fint *ierr),
                           (win, errhandler, ierr) )
#endif

void mpi_win_set_errhandler_f(MPI_Fint *win, MPI_Fint *errhandler, MPI_Fint *ierr)
{

}
