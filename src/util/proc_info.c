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

#include <stdio.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include "include/constants.h"
#include "util/proc_info.h"

orte_proc_info_t orte_process_info = {
    /*  .init =                 */   false,
    /*  .pid =                  */   0,
    /*  .seed =                 */   false,
    /*  .daemon =               */   false,
    /*  .ns_replica =           */   NULL,
    /*  .gpr_replica =          */   NULL,
    /*  .my_universe            */   NULL,
    /*  .tmpdir_base =          */   NULL,
    /*  .top_session_dir =      */   NULL,
    /*  .universe_session_dir = */   NULL,
    /*  .job_session_dir =      */   NULL,
    /*  .proc_session_dir =     */   NULL,
    /*  .sock_stdin =           */   NULL,
    /*  .sock_stdout =          */   NULL,
    /*  .sock_stderr =          */   NULL};


int orte_proc_info(void)
{

    /* local variable */
    int return_code=OMPI_SUCCESS;

    if (orte_process_info.init) {  /* already done this - don't do it again */
        return(OMPI_SUCCESS);
    }

    /* get the process id */
    orte_process_info.pid = getpid();


    /* set process to inited */
    orte_process_info.init = true;

    return return_code;
}
