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
/** @file:
 *
 * The Open MPI general purpose registry - implementation.
 *
 */

/*
 * includes
 */

#include "orte_config.h"

#include "mca/ns/ns_types.h"

#include "gpr_replica_api.h"
#include "mca/gpr/replica/functional_layer/gpr_replica_fn.h"


int orte_gpr_replica_cleanup_job(orte_jobid_t jobid)
{
    int rc;
    
    OMPI_THREAD_LOCK(&orte_gpr_replica_mutex);
    rc = orte_gpr_replica_cleanup_job_fn(jobid);
    OMPI_THREAD_UNLOCK(&orte_gpr_replica_mutex);
    
    if (ORTE_SUCCESS != rc) {
        return rc;
    }
    
    return orte_gpr_replica_process_callbacks();
}


int orte_gpr_replica_cleanup_proc(bool purge, orte_process_name_t *proc)
{
    int rc;
    
    OMPI_THREAD_LOCK(&orte_gpr_replica_mutex);
    rc = orte_gpr_replica_cleanup_proc_fn(purge, proc);
    OMPI_THREAD_UNLOCK(&orte_gpr_replica_mutex);

    if (ORTE_SUCCESS != rc) {
        return rc;
    }
    
    return orte_gpr_replica_process_callbacks();
}
