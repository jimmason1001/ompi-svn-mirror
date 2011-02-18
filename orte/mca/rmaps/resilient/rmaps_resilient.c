/*
 * Copyright (c) 2009      Cisco Systems, Inc.  All rights reserved. 
 * Copyright (c) 2009-2010 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 *
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#include "orte_config.h"
#include "orte/constants.h"
#include "orte/types.h"

#include <errno.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif  /* HAVE_UNISTD_H */
#ifdef HAVE_STRING_H
#include <string.h>
#endif  /* HAVE_STRING_H */
#include <stdio.h>

#include "opal/mca/base/mca_base_param.h"
#include "opal/util/argv.h"
#include "opal/class/opal_pointer_array.h"

#include "orte/util/show_help.h"
#include "orte/mca/errmgr/errmgr.h"

#include "orte/mca/rmaps/base/rmaps_private.h"
#include "orte/mca/rmaps/base/base.h"
#include "rmaps_resilient.h"


/*
 * Local variable
 */
static char *orte_getline(FILE *fp);
static bool have_ftgrps=false;

static int construct_ftgrps(void);
static int get_ftgrp_target(orte_proc_t *proc,
                            orte_rmaps_res_ftgrp_t **target,
                            orte_node_t **nd);
static int get_new_node(orte_proc_t *proc,
                        orte_app_context_t *app,
                        orte_job_map_t *map,
                        orte_node_t **ndret);
static int map_to_ftgrps(orte_job_t *jdata);

/*
 * Loadbalance the cluster
 */
static int orte_rmaps_resilient_map(orte_job_t *jdata)
{
    orte_app_context_t *app;
    int i;
    int rc = ORTE_SUCCESS;
    orte_node_t *nd=NULL, *oldnode, *node;
    orte_rmaps_res_ftgrp_t *target = NULL;
    orte_proc_t *proc;
    orte_vpid_t totprocs;
    opal_list_t node_list;
    orte_std_cntr_t num_slots;
    opal_list_item_t *item;

    if (0 < jdata->map->mapper && ORTE_RMAPS_RESILIENT != jdata->map->mapper) {
        opal_output_verbose(5, orte_rmaps_base.rmaps_output,
                            "mca:rmaps:resilient: cannot map job %s - other mapper specified",
                            ORTE_JOBID_PRINT(jdata->jobid));
        return ORTE_ERR_TAKE_NEXT_OPTION;
    }
    if (ORTE_JOB_STATE_INIT == jdata->state &&
        NULL == mca_rmaps_resilient_component.fault_group_file) {
        opal_output_verbose(5, orte_rmaps_base.rmaps_output,
                            "mca:rmaps:resilient: cannot perform initial map of job %s",
                            ORTE_JOBID_PRINT(jdata->jobid));
        return ORTE_ERR_TAKE_NEXT_OPTION;
    }

    opal_output_verbose(5, orte_rmaps_base.rmaps_output,
                        "mca:rmaps:resilient: mapping job %s",
                        ORTE_JOBID_PRINT(jdata->jobid));
 
    /* flag that I did the mapping */
    jdata->map->mapper = ORTE_RMAPS_RESILIENT;

    /* have we already constructed the fault group list? */
    if (!have_ftgrps) {
        construct_ftgrps();
    }

    if (ORTE_JOB_STATE_INIT == jdata->state) {
        /* this is an initial map - let the fault group mapper
         * handle it
         */
        return map_to_ftgrps(jdata);
    }

    /*
     * NOTE: if a proc is being ADDED to an existing job, then its
     * node field will be NULL.
     */
    OPAL_OUTPUT_VERBOSE((1, orte_rmaps_base.rmaps_output,
                         "%s rmaps:resilient: remapping job %s",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_JOBID_PRINT(jdata->jobid)));

    /* cycle through all the procs in this job to find the one(s) that failed */
    for (i=0; i < jdata->procs->size; i++) {
        /* get the proc object */
        if (NULL == (proc = (orte_proc_t*)opal_pointer_array_get_item(jdata->procs, i))) {
            continue;
        }
        /* is this proc to be restarted? */
        if (proc->state != ORTE_PROC_STATE_RESTART) {
            continue;
        }
        /* save the current node */
        oldnode = proc->node;
        /* point to the app */
        app = (orte_app_context_t*)opal_pointer_array_get_item(jdata->apps, proc->app_idx);
        if( NULL == app ) {
            ORTE_ERROR_LOG(ORTE_ERR_FAILED_TO_MAP);
            rc = ORTE_ERR_FAILED_TO_MAP;
            goto error;
        }

        if (NULL == oldnode) {
            /* this proc was not previously running - likely it is being added
             * to the job. So place it on the node with the fewest procs to
             * balance the load
             */
            OBJ_CONSTRUCT(&node_list, opal_list_t);
            if (ORTE_SUCCESS != (rc = orte_rmaps_base_get_target_nodes(&node_list,
                                                                       &num_slots,
                                                                       app,
                                                                       jdata->map->policy))) {
                ORTE_ERROR_LOG(rc);
                goto error;
            }
            if (0 == opal_list_get_size(&node_list)) {
                ORTE_ERROR_LOG(ORTE_ERROR);
                rc = ORTE_ERROR;
                goto error;
            }
            totprocs = 1000000;
            nd = NULL;
            while (NULL != (item = opal_list_remove_first(&node_list))) {
                node = (orte_node_t*)item;
                if (node->num_procs < totprocs) {
                    nd = node;
                    totprocs = node->num_procs;
                }
                OBJ_RELEASE(item); /* maintain accounting */
            }
            OBJ_DESTRUCT(&node_list);
            /* we already checked to ensure there was at least one node,
             * so we couldn't have come out of the loop with nd=NULL
             */
            OPAL_OUTPUT_VERBOSE((1, orte_rmaps_base.rmaps_output,
                                 "%s rmaps:resilient: Placing new process on node %s daemon %s (no ftgrp)",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                 nd->name, ORTE_NAME_PRINT((&nd->daemon->name))));
        } else {

            OPAL_OUTPUT_VERBOSE((1, orte_rmaps_base.rmaps_output,
                                 "%s rmaps:resilient: proc %s from node %s is to be restarted",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                 ORTE_NAME_PRINT(&proc->name),
                                 (NULL == proc->node) ? "NULL" : proc->node->name));

            /* if we have fault groups, use them */
            if (have_ftgrps) {
                if (ORTE_SUCCESS != (rc = get_ftgrp_target(proc, &target, &nd))) {
                    ORTE_ERROR_LOG(rc);
                    goto error;
                }
                OPAL_OUTPUT_VERBOSE((1, orte_rmaps_base.rmaps_output,
                                     "%s rmaps:resilient: placing proc %s into fault group %d node %s",
                                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                     ORTE_NAME_PRINT(&proc->name), target->ftgrp, nd->name));
            } else {
                if (ORTE_SUCCESS != (rc = get_new_node(proc, app, jdata->map, &nd))) {
                    ORTE_ERROR_LOG(rc);
                    return rc;
                }
            }
        }
        /*
         * Put the process on the found node (add it if not already in the map)
         */
        if (ORTE_SUCCESS != (rc = orte_rmaps_base_claim_slot(jdata,
                                                             nd,
                                                             jdata->map->cpus_per_rank,
                                                             proc->app_idx,
                                                             NULL,
                                                             jdata->map->oversubscribe,
                                                             false,
                                                             &proc))) {
            /** if the code is ORTE_ERR_NODE_FULLY_USED, then we know this
             * really isn't an error
             */
            if (ORTE_ERR_NODE_FULLY_USED != rc) {
                ORTE_ERROR_LOG(rc);
                goto error;
            }
        }

        /* flag the proc state as non-launched so we'll know to launch it */
        proc->state = ORTE_PROC_STATE_INIT;

        /* update the node and local ranks so static ports can
         * be properly selected if active
         */
        orte_rmaps_base_update_local_ranks(jdata, oldnode, nd, proc);
    }
    /* define the daemons that we will use for this job */
    if (ORTE_SUCCESS != (rc = orte_rmaps_base_define_daemons(jdata))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }

 error:
    return rc;
}

orte_rmaps_base_module_t orte_rmaps_resilient_module = {
    orte_rmaps_resilient_map
};

static char *orte_getline(FILE *fp)
{
    char *ret, *buff;
    char input[1024];
    
    ret = fgets(input, 1024, fp);
    if (NULL != ret) {
        input[strlen(input)-1] = '\0';  /* remove newline */
        buff = strdup(input);
        return buff;
    }
    
    return NULL;
}


static int construct_ftgrps(void)
{
    orte_rmaps_res_ftgrp_t *ftgrp;
    orte_node_t *node;
    FILE *fp;
    char *ftinput;
    int grp;
    char **nodes;
    bool found;
    int i, k;

    /* flag that we did this */
    have_ftgrps = true;

    if (NULL == mca_rmaps_resilient_component.fault_group_file) {
        /* nothing to build */
        return ORTE_SUCCESS;
    }

    /* construct it */
    OPAL_OUTPUT_VERBOSE((1, orte_rmaps_base.rmaps_output,
                         "%s rmaps:resilient: constructing fault groups",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
    fp = fopen(mca_rmaps_resilient_component.fault_group_file, "r");
    if (NULL == fp) { /* not found */
        orte_show_help("help-orte-rmaps-resilient.txt", "orte-rmaps-resilient:file-not-found",
                       true, mca_rmaps_resilient_component.fault_group_file);
        return ORTE_ERR_FAILED_TO_MAP;
    }

    /* build list of fault groups */
    grp = 0;
    while (NULL != (ftinput = orte_getline(fp))) {
        ftgrp = OBJ_NEW(orte_rmaps_res_ftgrp_t);
        ftgrp->ftgrp = grp++;
        nodes = opal_argv_split(ftinput, ',');
        /* find the referenced nodes */
        for (k=0; k < opal_argv_count(nodes); k++) {
            found = false;
            for (i=0; i < orte_node_pool->size && !found; i++) {
                if (NULL == (node = opal_pointer_array_get_item(orte_node_pool, i))) {
                    continue;
                }
                if (0 == strcmp(node->name, nodes[k])) {
                    OBJ_RETAIN(node);
                    OPAL_OUTPUT_VERBOSE((1, orte_rmaps_base.rmaps_output,
                                         "%s rmaps:resilient: adding node %s to fault group %d",
                                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                         node->name, ftgrp->ftgrp));
                    opal_pointer_array_add(&ftgrp->nodes, node);
                    found = true;
                    break;
                }
            }
        }
        opal_list_append(&mca_rmaps_resilient_component.fault_grps, &ftgrp->super);
        opal_argv_free(nodes);
        free(ftinput);
    }
    fclose(fp);

    return ORTE_SUCCESS;
}

static int get_ftgrp_target(orte_proc_t *proc,
                            orte_rmaps_res_ftgrp_t **tgt,
                            orte_node_t **ndret)
{
    opal_list_item_t *item;
    int k, totnodes;
    orte_node_t *node, *nd;
    orte_rmaps_res_ftgrp_t *target, *ftgrp;
    float avgload, minload;
    orte_vpid_t totprocs, lowprocs;

    /* set defaults */
    *tgt = NULL;
    *ndret = NULL;

    /* flag all the fault groups that
     * include this node so we don't reuse them
     */
    minload = 1000000.0;
    target = NULL;
    for (item = opal_list_get_first(&mca_rmaps_resilient_component.fault_grps);
         item != opal_list_get_end(&mca_rmaps_resilient_component.fault_grps);
         item = opal_list_get_next(item)) {
        ftgrp = (orte_rmaps_res_ftgrp_t*)item;
        /* see if the node is in this fault group */
        ftgrp->included = true;
        ftgrp->used = false;
        for (k=0; k < ftgrp->nodes.size; k++) {
            if (NULL == (node = (orte_node_t*)opal_pointer_array_get_item(&ftgrp->nodes, k))) {
                continue;
            }
            if (NULL != proc->node && 0 == strcmp(node->name, proc->node->name)) {
                /* yes - mark it to not be included */
                OPAL_OUTPUT_VERBOSE((1, orte_rmaps_base.rmaps_output,
                                     "%s rmaps:resilient: node %s is in fault group %d, which will be excluded",
                                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                     proc->node->name, ftgrp->ftgrp));
                ftgrp->included = false;
                break;
            }
        }
        /* if this ftgrp is not included, then skip it */
        if (!ftgrp->included) {
            continue;
        }
        /* compute the load average on this fault group */
        totprocs = 0;
        totnodes = 0;
        for (k=0; k < ftgrp->nodes.size; k++) {
            if (NULL == (node = (orte_node_t*)opal_pointer_array_get_item(&ftgrp->nodes, k))) {
                continue;
            }
            totnodes++;
            totprocs += node->num_procs;
        }
        avgload = (float)totprocs / (float)totnodes;
        /* now find the lightest loaded of the included fault groups */
        if (avgload < minload) {
            minload = avgload;
            target = ftgrp;
            OPAL_OUTPUT_VERBOSE((2, orte_rmaps_base.rmaps_output,
                                 "%s rmaps:resilient: found new min load ftgrp %d",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                 ftgrp->ftgrp));
        }
    }
 
    if (NULL == target) {
        /* nothing found */
        return ORTE_ERR_NOT_FOUND;
    }

    /* if we did find a target, re-map the proc to the lightest loaded
     * node in that group
     */
    lowprocs = 1000000;
    nd = NULL;
    for (k=0; k < target->nodes.size; k++) {
        if (NULL == (node = (orte_node_t*)opal_pointer_array_get_item(&target->nodes, k))) {
            continue;
        }
        if (node->num_procs < lowprocs) {
            lowprocs = node->num_procs;
            nd = node;
        }
    }

    /* return the results */
    *tgt = target;
    *ndret = nd;

    return ORTE_SUCCESS;
}

static int get_new_node(orte_proc_t *proc,
                        orte_app_context_t *app,
                        orte_job_map_t *map,
                        orte_node_t **ndret)
{
    orte_node_t *nd, *oldnode, *node;
    int rc;
    opal_list_t node_list;
    opal_list_item_t *item, *next;
    orte_std_cntr_t num_slots;

    /* if no ftgrps are available, then just put it on the next node
     * on the list - obviously, this is a rather unintelligent decision.
     * However, we want to ensure  that we don't just keep bouncing
     * back/forth between the same two nodes.
     *
     * Note: if the list only has oldnode on it, then this installs
     * the proc back on its original node - this is better than not
     * restarting at all
     */
    *ndret = NULL;
    nd = NULL;
    oldnode = proc->node;

    /*
     * Get a list of all nodes
     */
    OBJ_CONSTRUCT(&node_list, opal_list_t);
    if (ORTE_SUCCESS != (rc = orte_rmaps_base_get_target_nodes(&node_list,
                                                               &num_slots,
                                                               app,
                                                               map->policy))) {
        ORTE_ERROR_LOG(rc);
        goto error;
    }
    if (0 == opal_list_get_size(&node_list)) {
        ORTE_ERROR_LOG(ORTE_ERR_OUT_OF_RESOURCE);
        rc = ORTE_ERR_OUT_OF_RESOURCE;
        goto error;
    }

    /*
     * Cycle thru the list to find the current node
     * 
     */
    item = opal_list_get_first(&node_list);
    while (item != opal_list_get_end(&node_list)) {
        next = opal_list_get_next(item);
        node = (orte_node_t*)item;
        OPAL_OUTPUT_VERBOSE((7, orte_rmaps_base.rmaps_output,
                             "%s CHECKING NODE %s[%s] AGAINST NODE %s[%s]",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                             node->name,
                             (NULL == node->daemon) ? "?" : ORTE_VPID_PRINT(node->daemon->name.vpid),
                             oldnode->name,
                             (NULL == oldnode->daemon) ? "?" : ORTE_VPID_PRINT(oldnode->daemon->name.vpid)));
        if (node == oldnode) {
            if (next == opal_list_get_end(&node_list)) {
                nd = (orte_node_t*)opal_list_get_first(&node_list);
            } else {
                nd = (orte_node_t*)next;
            }
            break;
        }
    }
    OPAL_OUTPUT_VERBOSE((1, orte_rmaps_base.rmaps_output,
                         "%s rmaps:resilient: Placing process on node %s daemon %s (no ftgrp)",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         (nd == oldnode) ? "OLDNODE" : nd->name,
                         ORTE_NAME_PRINT((&nd->daemon->name))));

 error:
    while (NULL != (item = opal_list_remove_first(&node_list))) {
        OBJ_RELEASE(item);
    }
    OBJ_DESTRUCT(&node_list);

    *ndret = nd;
    return rc;
}

static void flag_nodes(opal_list_t *node_list)
{
    opal_list_item_t *item, *nitem;
    orte_node_t *node, *nd;
    orte_rmaps_res_ftgrp_t *ftgrp;
    int k;
    
    for (item = opal_list_get_first(&mca_rmaps_resilient_component.fault_grps);
         item != opal_list_get_end(&mca_rmaps_resilient_component.fault_grps);
         item = opal_list_get_next(item)) {
        ftgrp = (orte_rmaps_res_ftgrp_t*)item;
        /* reset the flags */
        ftgrp->used = false;
        ftgrp->included = false;
        /* if at least one node in our list is included in this
         * ftgrp, then flag it as included
         */
        for (nitem = opal_list_get_first(node_list);
             !ftgrp->included && nitem != opal_list_get_end(node_list);
             nitem = opal_list_get_next(nitem)) {
            node = (orte_node_t*)nitem;
            for (k=0; k < ftgrp->nodes.size; k++) {
                if (NULL == (nd = (orte_node_t*)opal_pointer_array_get_item(&ftgrp->nodes, k))) {
                    continue;
                }
                if (0 == strcmp(nd->name, node->name)) {
                    ftgrp->included = true;
                    break;
                }
            }
        }
    }
}

static int map_to_ftgrps(orte_job_t *jdata)
{
    orte_job_map_t *map;
    orte_app_context_t *app;
    int i, j, k, totnodes;
    opal_list_t node_list;
    opal_list_item_t *item, *next, *curitem;
    orte_std_cntr_t num_slots;
    int rc = ORTE_SUCCESS;
    float avgload, minload;
    orte_node_t *node, *nd=NULL;
    orte_rmaps_res_ftgrp_t *ftgrp, *target = NULL;
    orte_vpid_t totprocs, num_assigned;
    orte_proc_t *proc;

    OPAL_OUTPUT_VERBOSE((1, orte_rmaps_base.rmaps_output,
                         "%s rmaps:resilient: creating initial map for job %s",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_JOBID_PRINT(jdata->jobid)));

    /* start at the beginning... */
    jdata->num_procs = 0;
    map = jdata->map;
    
    for (i=0; i < jdata->apps->size; i++) {
        /* get the app_context */
        if (NULL == (app = (orte_app_context_t*)opal_pointer_array_get_item(jdata->apps, i))) {
            continue;
        }
        /* you cannot use this mapper unless you specify the number of procs to
         * launch for each app
         */
        if (0 == app->num_procs) {
            orte_show_help("help-orte-rmaps-resilient.txt",
                           "orte-rmaps-resilient:num-procs",
                           true);
            return ORTE_ERR_SILENT;
        }
        num_assigned = 0;
        /* for each app_context, we have to get the list of nodes that it can
         * use since that can now be modified with a hostfile and/or -host
         * option
         */
        OBJ_CONSTRUCT(&node_list, opal_list_t);
        if (ORTE_SUCCESS != (rc = orte_rmaps_base_get_target_nodes(&node_list, &num_slots, app,
                                                                   map->policy))) {
            ORTE_ERROR_LOG(rc);
            return rc;
        }
        /* remove all nodes that are not "up" or do not have a running daemon on them */
        item = opal_list_get_first(&node_list);
        while (item != opal_list_get_end(&node_list)) {
            next = opal_list_get_next(item);
            node = (orte_node_t*)item;
            if (ORTE_NODE_STATE_UP != node->state ||
                NULL == node->daemon ||
                ORTE_PROC_STATE_RUNNING != node->daemon->state) {
                opal_list_remove_item(&node_list, item);
                OBJ_RELEASE(item);
            }
            item = next;
        }
        curitem = opal_list_get_first(&node_list);

        /* flag the fault groups included by these nodes */
        flag_nodes(&node_list);
        /* map each copy to a different fault group - if more copies are
         * specified than fault groups, then overlap in a round-robin fashion
         */
        for (j=0; j < app->num_procs; j++) {
            /* find unused included fault group with lowest average load - if none
             * found, then break
             */
            target = NULL;
            minload = 1000000000.0;
            for (item = opal_list_get_first(&mca_rmaps_resilient_component.fault_grps);
                 item != opal_list_get_end(&mca_rmaps_resilient_component.fault_grps);
                 item = opal_list_get_next(item)) {
                ftgrp = (orte_rmaps_res_ftgrp_t*)item;
                OPAL_OUTPUT_VERBOSE((2, orte_rmaps_base.rmaps_output,
                                     "%s rmaps:resilient: fault group %d used: %s included %s",
                                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                     ftgrp->ftgrp,
                                     ftgrp->used ? "YES" : "NO",
                                     ftgrp->included ? "YES" : "NO" ));
                /* if this ftgrp has already been used or is not included, then
                 * skip it
                 */
                if (ftgrp->used || !ftgrp->included) {
                    continue;
                }
                /* compute the load average on this fault group */
                totprocs = 0;
                totnodes = 0;
                for (k=0; k < ftgrp->nodes.size; k++) {
                    if (NULL == (node = (orte_node_t*)opal_pointer_array_get_item(&ftgrp->nodes, k))) {
                        continue;
                    }
                    totnodes++;
                    totprocs += node->num_procs;
                }
                avgload = (float)totprocs / (float)totnodes;
                if (avgload < minload) {
                    minload = avgload;
                    target = ftgrp;
                    OPAL_OUTPUT_VERBOSE((2, orte_rmaps_base.rmaps_output,
                                         "%s rmaps:resilient: found new min load ftgrp %d",
                                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                         ftgrp->ftgrp));
                }
            }
            /* if we have more procs than fault groups, then we simply
             * map the remaining procs on available nodes in a round-robin
             * fashion - it doesn't matter where they go as they will not
             * be contributing to fault tolerance by definition
             */
            if (NULL == target) {
                OPAL_OUTPUT_VERBOSE((2, orte_rmaps_base.rmaps_output,
                                     "%s rmaps:resilient: more procs than fault groups - mapping excess rr",
                                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
                nd = (orte_node_t*)curitem;
                curitem = opal_list_get_next(curitem);
                if (curitem == opal_list_get_end(&node_list)) {
                    curitem = opal_list_get_first(&node_list);
                }
            } else {
                /* pick node with lowest load from within that group */
                totprocs = 1000000;
                for (k=0; k < target->nodes.size; k++) {
                    if (NULL == (node = (orte_node_t*)opal_pointer_array_get_item(&target->nodes, k))) {
                        continue;
                    }
                    if (node->num_procs < totprocs) {
                        totprocs = node->num_procs;
                        nd = node;
                    }
                }
            }
            OPAL_OUTPUT_VERBOSE((1, orte_rmaps_base.rmaps_output,
                                 "%s rmaps:resilient: placing proc into fault group %d node %s",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                 (NULL == target) ? -1 : target->ftgrp, nd->name));
            /* put proc on that node */
            proc=NULL;
            if (ORTE_SUCCESS != (rc = orte_rmaps_base_claim_slot(jdata, nd, jdata->map->cpus_per_rank, app->idx,
                                                                 &node_list, jdata->map->oversubscribe, false, &proc))) {
                /** if the code is ORTE_ERR_NODE_FULLY_USED, then we know this
                 * really isn't an error
                 */
                if (ORTE_ERR_NODE_FULLY_USED != rc) {
                    ORTE_ERROR_LOG(rc);
                    return rc;
                }
            }
            /* flag the proc as ready for launch */
            proc->state = ORTE_PROC_STATE_INIT;

            /* track number of procs mapped */
            num_assigned++;
                
            /* flag this fault group as used */
            if (NULL != target) {
                target->used = true;
            }
        }

        /* track number of procs */
        jdata->num_procs += app->num_procs;
        
        /* compute vpids and add proc objects to the job - this has to be
         * done after each app_context is mapped in order to keep the
         * vpids contiguous within an app_context
         */
        if (ORTE_SUCCESS != (rc = orte_rmaps_base_compute_vpids(jdata))) {
            ORTE_ERROR_LOG(rc);
            return rc;
        }

        /* cleanup the node list - it can differ from one app_context
         * to another, so we have to get it every time
         */
        while (NULL != (item = opal_list_remove_first(&node_list))) {
            OBJ_RELEASE(item);
        }
        OBJ_DESTRUCT(&node_list);
    }

    /* compute and save local ranks */
    if (ORTE_SUCCESS != (rc = orte_rmaps_base_compute_local_ranks(jdata))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }

    /* define the daemons that we will use for this job */
    if (ORTE_SUCCESS != (rc = orte_rmaps_base_define_daemons(jdata))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    
    return ORTE_SUCCESS;
}
