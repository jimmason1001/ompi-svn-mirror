/*
 * Copyright (c) 2004-2007 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2008 The Trustees of Indiana University.
 *                         All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 *
 * These symbols are in a file by themselves to provide nice linker
 * semantics.  Since linkers generally pull in symbols by object
 * files, keeping these symbols as the only symbols in this file
 * prevents utility programs such as "ompi_info" from having to import
 * entire components just to query their version and parameters.
 */

#include "orte_config.h"
#include "orte/constants.h"

#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <ctype.h>

#include "opal/util/opal_environ.h"
#include "opal/util/argv.h"
#include "opal/util/path.h"
#include "opal/util/basename.h"
#include "opal/mca/base/mca_base_param.h"


#include "orte/mca/plm/plm.h"
#include "orte/mca/plm/base/plm_private.h"
#include "orte/mca/plm/submit/plm_submit.h"

/*
 * Local function
 */
static char **search(const char* agent_list);

/*
 * Public string showing the plm ompi_submit component version number
 */
const char *mca_plm_submit_component_version_string =
  "Open MPI submit plm MCA component version " ORTE_VERSION;


/*
 * Instantiate the public struct with all of our public information
 * and pointers to our public functions in it
 */

orte_plm_submit_component_t mca_plm_submit_component = {
    {
    /* First, the mca_component_t struct containing meta information
       about the component itself */

    {
        ORTE_PLM_BASE_VERSION_2_0_0,

        /* Component name and version */
        "submit",
        ORTE_MAJOR_VERSION,
        ORTE_MINOR_VERSION,
        ORTE_RELEASE_VERSION,

        /* Component open and close functions */
        orte_plm_submit_component_open,
        orte_plm_submit_component_close,
        orte_plm_submit_component_query
    },
    {
        /* The component is checkpoint ready */
        MCA_BASE_METADATA_PARAM_CHECKPOINT
    }
    }
};



int orte_plm_submit_component_open(void)
{
    int tmp, value;
    mca_base_component_t *c = &mca_plm_submit_component.super.base_version;

    /* initialize globals */
    OBJ_CONSTRUCT(&mca_plm_submit_component.lock, opal_mutex_t);
    OBJ_CONSTRUCT(&mca_plm_submit_component.cond, opal_condition_t);
    mca_plm_submit_component.num_children = 0;
    mca_plm_submit_component.agent_argv = NULL;
    mca_plm_submit_component.agent_argc = 0;
    mca_plm_submit_component.agent_path = NULL;

    /* lookup parameters */
    mca_base_param_reg_int(c, "debug",
                           "Whether or not to enable debugging output for the submit plm component (0 or 1)",
                           false, false, false, &tmp);
    mca_plm_submit_component.debug = OPAL_INT_TO_BOOL(tmp);
    mca_base_param_reg_int(c, "num_concurrent",
                           "How many plm_submit_agent instances to invoke concurrently (must be > 0)",
                           false, false, 128, &tmp);
    if (tmp <= 0) {
        orte_show_help("help-plm-submit.txt", "concurrency-less-than-zero",
                       true, tmp);
        tmp = 1;
    }
    mca_plm_submit_component.num_concurrent = tmp;

    if (mca_plm_submit_component.debug == 0) {
        mca_base_param_reg_int_name("orte", "debug",
                                    "Whether or not to enable debugging output for all ORTE components (0 or 1)",
                                    false, false, false, &tmp);
        mca_plm_submit_component.debug = OPAL_INT_TO_BOOL(tmp);
    }
    mca_base_param_reg_int_name("orte", "debug_daemons",
                                "Whether or not to enable debugging of daemons (0 or 1)",
                                false, false, false, &tmp);
    mca_plm_submit_component.debug_daemons = OPAL_INT_TO_BOOL(tmp);
    
    tmp = mca_base_param_reg_int_name("orte", "timing",
                                      "Request that critical timing loops be measured",
                                      false, false, 0, &value);
    if (value != 0) {
        mca_plm_submit_component.timing = true;
    } else {
        mca_plm_submit_component.timing = false;
    }

    mca_base_param_reg_string(c, "orted",
                              "The command name that the submit plm component will invoke for the ORTE daemon",
                              false, false, "orted", 
                              &mca_plm_submit_component.orted);
    
    mca_base_param_reg_int(c, "priority",
                           "Priority of the submit plm component",
                           false, false, 10,
                           &mca_plm_submit_component.priority);
    mca_base_param_reg_int(c, "delay",
                           "Delay (in seconds) between invocations of the remote agent, but only used when the \"debug\" MCA parameter is true, or the top-level MCA debugging is enabled (otherwise this value is ignored)",
                           false, false, 1,
                           &mca_plm_submit_component.delay);
    mca_base_param_reg_int(c, "assume_same_shell",
                           "If set to 1, assume that the shell on the remote node is the same as the shell on the local node.  Otherwise, probe for what the remote shell.",
                           false, false, 1, &tmp);
    mca_plm_submit_component.assume_same_shell = OPAL_INT_TO_BOOL(tmp);

    mca_base_param_reg_string(c, "agent",
                              "The command used to launch executables on remote nodes (typically either \"ssh\" or \"submit\")",
                              false, false, "ssh : submit",
                              &mca_plm_submit_component.agent_param);

    return ORTE_SUCCESS;
}


int orte_plm_submit_component_query(mca_base_module_t **module, int *priority)
{
    char *bname;
    size_t i;

    /* Take the string that was given to us by the pla_submit_agent MCA
       param and search for it */
    mca_plm_submit_component.agent_argv = 
        search(mca_plm_submit_component.agent_param);
    mca_plm_submit_component.agent_argc = 
        opal_argv_count(mca_plm_submit_component.agent_argv);
    mca_plm_submit_component.agent_path = NULL;
    if (mca_plm_submit_component.agent_argc > 0) {
        /* If the agent is ssh, and debug was not selected, then
           automatically add "-x" */

        bname = opal_basename(mca_plm_submit_component.agent_argv[0]);
        if (NULL != bname && 0 == strcmp(bname, "ssh") &&
            mca_plm_submit_component.debug == 0) {
            for (i = 1; NULL != mca_plm_submit_component.agent_argv[i]; ++i) {
                if (0 == strcasecmp("-x", 
                                    mca_plm_submit_component.agent_argv[i])) {
                    break;
                }
            }
            if (NULL == mca_plm_submit_component.agent_argv[i]) {
                opal_argv_append(&mca_plm_submit_component.agent_argc, 
                                 &mca_plm_submit_component.agent_argv, "-x");
            }
        }
        if (NULL != bname) {
            free(bname);
        }
    }

    /* If we didn't find the agent in the path, then don't use this
       component */
    if (NULL == mca_plm_submit_component.agent_argv || 
        NULL == mca_plm_submit_component.agent_argv[0]) {
        *module = NULL;
        return ORTE_ERROR:
    }
    mca_plm_submit_component.agent_path = 
        opal_path_findv(mca_plm_submit_component.agent_argv[0], X_OK,
                        environ, NULL);
    if (NULL == mca_plm_submit_component.agent_path) {
        *module = NULL;
        return ORTE_ERROR:
    }
    *priority = mca_plm_submit_component.priority;
    *module = (mca_base_module_t *) &orte_plm_submit_module;
    return ORTE_SUCCESS;
}


int orte_plm_submit_component_close(void)
{
    /* cleanup state */
    OBJ_DESTRUCT(&mca_plm_submit_component.lock);
    OBJ_DESTRUCT(&mca_plm_submit_component.cond);
    if (NULL != mca_plm_submit_component.orted) {
        free(mca_plm_submit_component.orted);
    }
    if (NULL != mca_plm_submit_component.agent_param) {
        free(mca_plm_submit_component.agent_param);
    }
    if (NULL != mca_plm_submit_component.agent_argv) {
        opal_argv_free(mca_plm_submit_component.agent_argv);
    }
    if (NULL != mca_plm_submit_component.agent_path) {
        free(mca_plm_submit_component.agent_path);
    }
    return ORTE_SUCCESS;
}


/*
 * Take a colon-delimited list of agents and locate the first one that
 * we are able to find in the PATH.  Split that one into argv and
 * return it.  If nothing found, then return NULL.
 */
static char **search(const char* agent_list)
{
    int i, j;
    char *line, **lines = opal_argv_split(agent_list, ':');
    char **tokens, *tmp;
    char cwd[PATH_MAX];

    getcwd(cwd, PATH_MAX);
    for (i = 0; NULL != lines[i]; ++i) {
        line = lines[i];

        /* Trim whitespace at the beginning and end of the line */
        for (j = 0; '\0' != line[j] && isspace(line[j]); ++line) {
            continue;
        }
        for (j = strlen(line) - 2; j > 0 && isspace(line[j]); ++j) {
            line[j] = '\0';
        }
        if (strlen(line) <= 0) {
            continue;
        }

        /* Split it */
        tokens = opal_argv_split(line, ' ');

        /* Look for the first token in the PATH */
        tmp = opal_path_findv(tokens[0], X_OK, environ, cwd);
        if (NULL != tmp) {
            free(tokens[0]);
            tokens[0] = tmp;
            opal_argv_free(lines);
            return tokens;
        }

        /* Didn't find it */
        opal_argv_free(tokens);
    }

    /* Doh -- didn't find anything */
    opal_argv_free(lines);
    return NULL;
}
