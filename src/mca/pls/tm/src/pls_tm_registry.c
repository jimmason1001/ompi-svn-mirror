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


#include "orte_config.h"

#include "include/orte_constants.h"
#include "util/output.h"
#include "mca/mca.h"
#include "mca/base/base.h"
#include "mca/pls/base/base.h"
#include "mca/ns/ns.h"
#include "mca/gpr/gpr.h"
#include "mca/soh/soh_types.h"
#include "mca/errmgr/errmgr.h"
#include "pls_tm.h"


#define TID_KEY "orte-pls-tm-tid"


/**
 * Save the TID of a given process
 */
int orte_pls_tm_put_tid(const orte_process_name_t* name,
                        tm_task_id tid, int state)
{
    orte_gpr_value_t* values[1];
    orte_gpr_value_t value;
    orte_gpr_keyval_t kv_tid = {{OBJ_CLASS(orte_gpr_keyval_t),0},TID_KEY,ORTE_UINT32};
    orte_gpr_keyval_t kv_state = {{OBJ_CLASS(orte_gpr_keyval_t),0},ORTE_PROC_STATE_KEY,ORTE_PROC_STATE};
    orte_gpr_keyval_t* keyvals[2];
    int i, rc;

    if (ORTE_SUCCESS != (rc = orte_schema.get_job_segment_name(&value.segment, name->jobid))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }

    if (ORTE_SUCCESS != (rc = orte_schema.get_proc_tokens(&value.tokens, &value.num_tokens, (orte_process_name_t*) name))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }

    kv_tid.value.ui32 = tid;
    kv_state.value.proc_state = state;
    keyvals[0] = &kv_tid;
    keyvals[1] = &kv_state;
    
    value.keyvals = keyvals;
    value.cnt = 2;
    value.addr_mode = ORTE_GPR_OVERWRITE;
    values[0] = &value;
    
    rc = orte_gpr.put(1, values);
    free(value.segment);
    for(i=0; i<value.num_tokens; i++) {
        free(value.tokens[i]);
    }
    free(value.tokens);
    return rc;
}


/**
 * Retreive all process tids for the specified job.
 */
extern int orte_gpr_replica_dump_segments(int output_id);

int orte_pls_tm_get_tids(orte_jobid_t jobid, tm_task_id **tids, 
                         orte_process_name_t **names, size_t* size)
{
    char *segment;
    char *keys[3];
    orte_gpr_value_t** values = NULL;
    int i, num_values = 0;
    int rc;

    /* Zero out in case of error */

    *tids = NULL;
    *names = NULL;
    *size = 0;

    /* query the job segment on the registry */
    if (ORTE_SUCCESS != 
        (rc = orte_schema.get_job_segment_name(&segment, jobid))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }

    keys[0] = TID_KEY;
    keys[1] = ORTE_PROC_NAME_KEY;
    keys[2] = NULL;

    rc = orte_gpr.get(
        ORTE_GPR_KEYS_OR|ORTE_GPR_TOKENS_OR,
        segment,
        NULL,
        keys,
        &num_values,
        &values
        );
    ompi_output(orte_pls_base.pls_output,
                "pls:tm:reg:get_tids: got %d values", num_values);
    if (rc != ORTE_SUCCESS) {
        free(segment);
        return rc;
    }

    if (0 == num_values) {
        rc = ORTE_ERR_NOT_FOUND;
        ORTE_ERROR_LOG(rc);
        goto cleanup;
    }

    *tids = malloc(sizeof(tm_task_id) * num_values);
    *names = malloc(sizeof(orte_process_name_t *) * num_values);
    if (NULL == *tids || NULL == *names) {
        rc = ORTE_ERR_OUT_OF_RESOURCE;
        ORTE_ERROR_LOG(rc);
        goto cleanup;
    }
    for (i = 0; i < num_values; ++i) {
        (*tids)[i] = values[i]->keyvals[0]->value.ui32;
        (*names)[i] = values[i]->keyvals[1]->value.proc;
    }
    *size = num_values;

cleanup:
    if (NULL != values) {
        for (i = 0; i < num_values; ++i) {
            OBJ_RELEASE(values[i]);
        }
        free(values);
    }
    free(segment);
    return rc;
}
