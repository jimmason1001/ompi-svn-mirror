/*
 * Copyright (c) 2004-2006 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2006 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
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
/** @file:
 */

#ifndef MCA_PLM_BASE_H
#define MCA_PLM_BASE_H

/*
 * includes
 */
#include "orte_config.h"

#include "opal/mca/mca.h"
#include "opal/class/opal_list.h"

#include "orte/mca/plm/plm.h"


BEGIN_C_DECLS

/*
 * MCA framework
 */
ORTE_DECLSPEC extern mca_base_framework_t orte_plm_base_framework;

#if !ORTE_DISABLE_FULL_SUPPORT

/**
 * Struct to hold data for public access
 */
typedef struct orte_plm_base_t {
    /** indicate a component has been selected */
    bool selected;
    /** selected component */
    orte_plm_base_component_t selected_component;
} orte_plm_base_t;

/**
 * Global instance of publicly-accessible PLM framework data
 */
ORTE_DECLSPEC extern orte_plm_base_t orte_plm_base;

/*
 * Select an available component.
 */
ORTE_DECLSPEC int orte_plm_base_select(void);

/**
 * Functions that other frameworks may need to call directly
 * Specifically, the ODLS needs to access some of these
 * to avoid recursive callbacks
 */
ORTE_DECLSPEC void orte_plm_base_app_report_launch(int fd, short event, void *data);
ORTE_DECLSPEC void orte_plm_base_receive_process_msg(int fd, short event, void *data);

ORTE_DECLSPEC void orte_plm_base_setup_job(int fd, short args, void *cbdata);
ORTE_DECLSPEC void orte_plm_base_setup_job_complete(int fd, short args, void *cbdata);
ORTE_DECLSPEC void orte_plm_base_complete_setup(int fd, short args, void *cbdata);
ORTE_DECLSPEC void orte_plm_base_daemons_reported(int fd, short args, void *cbdata);
ORTE_DECLSPEC void orte_plm_base_allocation_complete(int fd, short args, void *cbdata);
ORTE_DECLSPEC void orte_plm_base_daemons_launched(int fd, short args, void *cbdata);
ORTE_DECLSPEC void orte_plm_base_vm_ready(int fd, short args, void *cbdata);
ORTE_DECLSPEC void orte_plm_base_mapping_complete(int fd, short args, void *cbdata);
ORTE_DECLSPEC void orte_plm_base_launch_apps(int fd, short args, void *cbdata);
ORTE_DECLSPEC void orte_plm_base_post_launch(int fd, short args, void *cbdata);
ORTE_DECLSPEC void orte_plm_base_registered(int fd, short args, void *cbdata);

#endif /* ORTE_DISABLE_FULL_SUPPORT */

END_C_DECLS

#endif
