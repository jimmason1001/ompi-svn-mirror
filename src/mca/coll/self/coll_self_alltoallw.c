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

#include "ompi_config.h"

#include "include/constants.h"
#include "datatype/datatype.h"
#include "coll_self.h"


/*
 *	alltoallw_intra
 *
 *	Function:	- MPI_Alltoallw
 *	Accepts:	- same as MPI_Alltoallw()
 *	Returns:	- MPI_SUCCESS or an MPI error code
 */
int mca_coll_self_alltoallw_intra(void *sbuf, int *scounts, int *sdisps,
                                  struct ompi_datatype_t **sdtypes, 
                                  void *rbuf, int *rcounts, int *rdisps,
                                  struct ompi_datatype_t **rdtypes, 
                                  struct ompi_communicator_t *comm)
{
    return ompi_ddt_sndrcv(((char *) sbuf) + sdisps[0], scounts[0], sdtypes[0],
                           ((char *) rbuf) + rdisps[0], rcounts[0], rdtypes[0]);
}
