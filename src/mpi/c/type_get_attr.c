/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University.
 *                         All rights reserved.
 * Copyright (c) 2004-2005 The Trustees of the University of Tennessee.
 *                         All rights reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#include "ompi_config.h"

#include "mpi.h"
#include "mpi/c/bindings.h"
#include "attribute/attribute.h"
#include "communicator/communicator.h"
#include "datatype/datatype.h"

#if OMPI_HAVE_WEAK_SYMBOLS && OMPI_PROFILING_DEFINES
#pragma weak MPI_Type_get_attr = PMPI_Type_get_attr
#endif

#if OMPI_PROFILING_DEFINES
#include "mpi/c/profile/defines.h"
#endif

static const char FUNC_NAME[] = "MPI_Type_get_attr";


int MPI_Type_get_attr (MPI_Datatype type,
                       int type_keyval,
                       void *attribute_val,
                       int *flag)
{
    int ret;

    if (MPI_PARAM_CHECK) {
        OMPI_ERR_INIT_FINALIZE(FUNC_NAME);
        if (NULL == type || MPI_DATATYPE_NULL == type) {
          return OMPI_ERRHANDLER_INVOKE(MPI_COMM_WORLD, MPI_ERR_TYPE,
                                        FUNC_NAME );
        } else if ((NULL == attribute_val) || (NULL == flag)) {
          return OMPI_ERRHANDLER_INVOKE(MPI_COMM_WORLD, 
                                        MPI_ERR_ARG, 
                                        FUNC_NAME);
	}
    }

    /* This stuff is very confusing.  Be sure to see
       src/attribute/attribute.c for a lengthy comment explaining Open
       MPI attribute behavior. */

    ret = ompi_attr_get_c(type->d_keyhash, type_keyval, 
                          attribute_val, flag);
    OMPI_ERRHANDLER_RETURN(ret, MPI_COMM_WORLD,
                           MPI_ERR_OTHER, FUNC_NAME);  
}
