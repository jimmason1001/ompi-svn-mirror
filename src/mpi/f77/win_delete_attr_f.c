/*
 * $HEADER$
 */

#include "lam_config.h"

#include <stdio.h>

#include "mpi.h"
#include "mpi/f77/bindings.h"

#if LAM_HAVE_WEAK_SYMBOLS && LAM_PROFILE_LAYER
#pragma weak PMPI_WIN_DELETE_ATTR = mpi_win_delete_attr_f
#pragma weak pmpi_win_delete_attr = mpi_win_delete_attr_f
#pragma weak pmpi_win_delete_attr_ = mpi_win_delete_attr_f
#pragma weak pmpi_win_delete_attr__ = mpi_win_delete_attr_f
#elif LAM_PROFILE_LAYER
LAM_GENERATE_F77_BINDINGS (PMPI_WIN_DELETE_ATTR,
                           pmpi_win_delete_attr,
                           pmpi_win_delete_attr_,
                           pmpi_win_delete_attr__,
                           pmpi_win_delete_attr_f,
                           (MPI_Fint *win, MPI_Fint *win_keyval, MPI_Fint *ierr),
                           (win, win_keyval, ierr) )
#endif

#if LAM_HAVE_WEAK_SYMBOLS
#pragma weak MPI_WIN_DELETE_ATTR = mpi_win_delete_attr_f
#pragma weak mpi_win_delete_attr = mpi_win_delete_attr_f
#pragma weak mpi_win_delete_attr_ = mpi_win_delete_attr_f
#pragma weak mpi_win_delete_attr__ = mpi_win_delete_attr_f
#endif

#if ! LAM_HAVE_WEAK_SYMBOLS && ! LAM_PROFILE_LAYER
LAM_GENERATE_F77_BINDINGS (MPI_WIN_DELETE_ATTR,
                           mpi_win_delete_attr,
                           mpi_win_delete_attr_,
                           mpi_win_delete_attr__,
                           mpi_win_delete_attr_f,
                           (MPI_Fint *win, MPI_Fint *win_keyval, MPI_Fint *ierr),
                           (win, win_keyval, ierr) )
#endif

void mpi_win_delete_attr_f(MPI_Fint *win, MPI_Fint *win_keyval, MPI_Fint *ierr)
{

}
