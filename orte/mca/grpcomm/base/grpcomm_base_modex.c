/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2007      Sun Microsystems, Inc.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "orte_config.h"
#include "orte/constants.h"
#include "orte/types.h"

#include <string.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif  /* HAVE_SYS_TIME_H */

#include "opal/threads/condition.h"
#include "orte/util/show_help.h"
#include "opal/util/bit_ops.h"

#include "opal/class/opal_hash_table.h"
#include "orte/util/proc_info.h"
#include "opal/dss/dss.h"
#include "orte/mca/errmgr/errmgr.h"
#include "orte/mca/odls/odls_types.h"
#include "orte/mca/rml/rml.h"
#include "orte/runtime/orte_globals.h"
#include "orte/util/name_fns.h"
#include "orte/orted/orted.h"
#include "orte/runtime/orte_wait.h"

#include "orte/mca/grpcomm/base/base.h"


/***************  MODEX SECTION **************/

/**
 * MODEX DESIGN
 *
 * Modex data is always associated with a given orte process name, in
 * an opal hash table. The hash table is necessary because modex data is
 * received for entire jobids and when working with
 * dynamic processes, it is possible we will receive data for a
 * process not yet in the ompi_proc_all() list of processes. This
 * information must be kept for later use, because if accept/connect
 * causes the proc to be added to the ompi_proc_all() list, it could
 * cause a connection storm.  Therefore, we use an
 * opal_hast_table_t backing store to contain all modex information.
 *
 * While we could add the now discovered proc into the ompi_proc_all()
 * list, this has some problems, in that we don't have the
 * architecture and hostname information needed to properly fill in
 * the ompi_proc_t structure and we don't want to cause RML
 * communication to get it when we don't really need to know anything
 * about the remote proc.
 *
 * All data put into the modex (or received from the modex) is
 * associated with a given proc,attr_name pair.  The data structures
 * to maintain this data look something like:
 *
 * opal_hash_table_t modex_data -> list of attr_proc_t objects
 * 
 * +-----------------------------+
 * | modex_proc_data_t           |
 * |  - opal_list_item_t         |
 * +-----------------------------+
 * | opal_mutex_t modex_lock     |
 * | bool modex_received_data    |     1
 * | opal_list_t modules         |     ---------+
 * +-----------------------------+              |
 *                                      *       |
 * +--------------------------------+  <--------+
 * | modex_module_data_t            |
 * |  - opal_list_item_t            |
 * +--------------------------------+
 * | mca_base_component_t component |
 * | void *module_data              |
 * | size_t module_data_size        |
 * +--------------------------------+
 *
 */

/* Local "globals" */
static orte_std_cntr_t num_entries;
static opal_buffer_t *modex_buffer;
static opal_hash_table_t *modex_data;
static opal_mutex_t mutex;
static opal_condition_t cond;

/**
 * Modex data for a particular orte process
 *
 * Locking infrastructure and list of module data for a given orte
 * process name.  The name association is maintained in the
 * modex_data hash table.
 */
struct modex_proc_data_t {
    /** Structure can be put on lists (including in hash tables) */
    opal_list_item_t super;
    /* Lock held whenever the modex data for this proc is being
       modified */
    opal_mutex_t modex_lock;
    /* True if modex data has ever been received from this process,
       false otherwise. */
    bool modex_received_data;
    /* List of modex_module_data_t structures containing all data
       received from this process, sorted by component name. */
    opal_list_t modex_module_data;
};
typedef struct modex_proc_data_t modex_proc_data_t;

static void
modex_construct(modex_proc_data_t * modex)
{
    OBJ_CONSTRUCT(&modex->modex_lock, opal_mutex_t);
    modex->modex_received_data = false;
    OBJ_CONSTRUCT(&modex->modex_module_data, opal_list_t);
}

static void
modex_destruct(modex_proc_data_t * modex)
{
    OBJ_DESTRUCT(&modex->modex_module_data);
    OBJ_DESTRUCT(&modex->modex_lock);
}

OBJ_CLASS_INSTANCE(modex_proc_data_t, opal_object_t,
                   modex_construct, modex_destruct);



/**
 * Data for a particular attribute
 *
 * Container for data for a particular module,attribute pair.  This
 * structure should be contained in the modex_module_data list in an
 * modex_proc_data_t structure to maintain an association with a
 * given proc.  The list is then searched for a matching attribute
 * name.
 *
 * While searching the list or reading from (or writing to) this
 * structure, the lock in the proc_data_t should be held.
 */
struct modex_attr_data_t {
    /** Structure can be put on lists */
    opal_list_item_t super;
    /** Attribute name */
    char * attr_name;
    /** Binary blob of data associated with this proc,component pair */
    void *attr_data;
    /** Size (in bytes) of module_data */
    size_t attr_data_size;
};
typedef struct modex_attr_data_t modex_attr_data_t;

static void
modex_attr_construct(modex_attr_data_t * module)
{
    module->attr_name = NULL;
    module->attr_data = NULL;
    module->attr_data_size = 0;
}

static void
modex_attr_destruct(modex_attr_data_t * module)
{
    if (NULL != module->attr_name) {
        free(module->attr_name);
    }
    if (NULL != module->attr_data) {
        free(module->attr_data);
    }
}

OBJ_CLASS_INSTANCE(modex_attr_data_t,
                   opal_list_item_t,
                   modex_attr_construct,
                   modex_attr_destruct);


/**
 * Find data for a given attribute in a given modex_proc_data_t
 * container.
 *
 * The proc_data's modex_lock must be held during this
 * search.
 */
static modex_attr_data_t *
modex_lookup_attr_data(modex_proc_data_t *proc_data,
                       const char *attr_name,
                       bool create_if_not_found)
{
    modex_attr_data_t *attr_data = NULL;
    for (attr_data = (modex_attr_data_t *) opal_list_get_first(&proc_data->modex_module_data);
         attr_data != (modex_attr_data_t *) opal_list_get_end(&proc_data->modex_module_data);
         attr_data = (modex_attr_data_t *) opal_list_get_next(attr_data)) {
        if (0 == strcmp(attr_name, attr_data->attr_name)) {
            return attr_data;
        }
    }
    
    if (create_if_not_found) {
        attr_data = OBJ_NEW(modex_attr_data_t);
        if (NULL == attr_data) return NULL;
        
        attr_data->attr_name = strdup(attr_name);
        opal_list_append(&proc_data->modex_module_data, &attr_data->super);
        
        return attr_data;
    }
    
    return NULL;
}


/**
* Find modex_proc_data_t container associated with given
 * orte_process_name_t.
 *
 * The global lock should *NOT* be held when
 * calling this function.
 */
static modex_proc_data_t*
modex_lookup_orte_proc(const orte_process_name_t *orte_proc)
{
    modex_proc_data_t *proc_data = NULL;
    
    OPAL_THREAD_LOCK(&mutex);
    opal_hash_table_get_value_uint64(modex_data, 
                    orte_util_hash_name(orte_proc), (void**)&proc_data);
    if (NULL == proc_data) {
        /* The proc clearly exists, so create a modex structure
        for it */
        proc_data = OBJ_NEW(modex_proc_data_t);
        if (NULL == proc_data) {
            opal_output(0, "grpcomm_basic_modex_lookup_orte_proc: unable to allocate modex_proc_data_t\n");
            OPAL_THREAD_UNLOCK(&mutex);
            return NULL;
        }
        opal_hash_table_set_value_uint64(modex_data, 
                    orte_util_hash_name(orte_proc), proc_data);
    }
    OPAL_THREAD_UNLOCK(&mutex);
    
    return proc_data;
}

int orte_grpcomm_base_modex_init(void)
{
    OBJ_CONSTRUCT(&mutex, opal_mutex_t);
    OBJ_CONSTRUCT(&cond, opal_condition_t);
    
    modex_data = OBJ_NEW(opal_hash_table_t);    
    opal_hash_table_init(modex_data, 256);
    num_entries = 0;
    modex_buffer = OBJ_NEW(opal_buffer_t);
    
    return ORTE_SUCCESS;
}

void orte_grpcomm_base_modex_finalize(void)
{
    OBJ_DESTRUCT(&mutex);
    OBJ_DESTRUCT(&cond);
    
    opal_hash_table_remove_all(modex_data);
    OBJ_RELEASE(modex_data);
    
    OBJ_RELEASE(modex_buffer);
}

int orte_grpcomm_base_set_proc_attr(const char *attr_name,
                                    const void *data,
                                    size_t size)
{
    int rc;
    
    OPAL_THREAD_LOCK(&mutex);
    
    OPAL_OUTPUT_VERBOSE((5, orte_grpcomm_base_output,
                         "%s grpcomm:set_proc_attr: setting attribute %s data size %lu",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         attr_name, size));

    /* Pack the attribute name information into the local buffer */
    if (ORTE_SUCCESS != (rc = opal_dss.pack(modex_buffer, &attr_name, 1, OPAL_STRING))) {
        ORTE_ERROR_LOG(rc);
        goto cleanup;
    }

    /* pack the size */
    if (ORTE_SUCCESS != (rc = opal_dss.pack(modex_buffer, &size, 1, OPAL_SIZE))) {
        ORTE_ERROR_LOG(rc);
        goto cleanup;
    }
    
    /* Pack the actual data into the buffer */
    if (0 != size) {
        if (ORTE_SUCCESS != (rc = opal_dss.pack(modex_buffer, (void *) data, size, OPAL_BYTE))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
    }
    
    /* track the number of entries */
    ++num_entries;
    
cleanup:
    OPAL_THREAD_UNLOCK(&mutex);
    
    return rc;
}

int orte_grpcomm_base_get_proc_attr(const orte_process_name_t proc,
                                    const char * attribute_name, void **val, 
                                    size_t *size)
{
    modex_proc_data_t *proc_data;
    modex_attr_data_t *attr_data;
    
    proc_data = modex_lookup_orte_proc(&proc);
    if (NULL == proc_data) return ORTE_ERR_NOT_FOUND;
    
    OPAL_THREAD_LOCK(&proc_data->modex_lock);
    
    /* look up attribute */
    attr_data = modex_lookup_attr_data(proc_data, attribute_name, false);
    
    /* copy the data out to the user */
    if ((NULL == attr_data) ||
        (attr_data->attr_data_size == 0)) {
        OPAL_OUTPUT_VERBOSE((5, orte_grpcomm_base_output,
                             "%s grpcomm:get_proc_attr: no attr avail or zero byte size",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
        *val = NULL;
        *size = 0;
    } else {
        void *copy = malloc(attr_data->attr_data_size);
        
        if (copy == NULL) {
            OPAL_THREAD_UNLOCK(&proc_data->modex_lock);
            return ORTE_ERR_OUT_OF_RESOURCE;
        }
        memcpy(copy, attr_data->attr_data, attr_data->attr_data_size);
        *val = copy;
        *size = attr_data->attr_data_size;
    }
    OPAL_THREAD_UNLOCK(&proc_data->modex_lock);
    
    return ORTE_SUCCESS;
} 


int orte_grpcomm_base_purge_proc_attrs(void)
{
    /*
     * Purge the attributes
     */
    opal_hash_table_remove_all(modex_data);
    OBJ_RELEASE(modex_data);
    modex_data = OBJ_NEW(opal_hash_table_t);    
    opal_hash_table_init(modex_data, 256);
    
    /*
     * Clear the modex buffer
     */
    OBJ_RELEASE(modex_buffer);
    num_entries = 0;
    modex_buffer = OBJ_NEW(opal_buffer_t);

    return ORTE_SUCCESS;
}

int orte_grpcomm_base_pack_modex_entries(opal_buffer_t *buf, bool *mdx_reqd)
{
    int rc;
    
    OPAL_OUTPUT_VERBOSE((5, orte_grpcomm_base_output,
                         "%s grpcomm:base:pack_modex: reporting %ld entries",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         (long)num_entries));
    
    /* put the number of entries into the buffer */
    OPAL_THREAD_LOCK(&mutex);
    if (ORTE_SUCCESS != (rc = opal_dss.pack(buf, &num_entries, 1, ORTE_STD_CNTR))) {
        ORTE_ERROR_LOG(rc);
        OPAL_THREAD_UNLOCK(&mutex);
        goto cleanup;
    }
    
    /* if there are entries, non-destructively copy the data across */
    if (0 < num_entries) {
        if (ORTE_SUCCESS != (opal_dss.copy_payload(buf, modex_buffer))) {
            ORTE_ERROR_LOG(rc);
            OPAL_THREAD_UNLOCK(&mutex);
            goto cleanup;
        }
    }
    *mdx_reqd = true;
    
cleanup:
    OPAL_THREAD_UNLOCK(&mutex);
    return rc;
}

int orte_grpcomm_base_update_modex_entries(orte_process_name_t *proc_name,
                                           opal_buffer_t *rbuf)
{
    modex_proc_data_t *proc_data;
    modex_attr_data_t *attr_data;
    int rc = ORTE_SUCCESS;
    orte_std_cntr_t num_recvd_entries;
    orte_std_cntr_t cnt;
    void *bytes = NULL;
    orte_std_cntr_t j;

    /* look up the modex data structure */
    proc_data = modex_lookup_orte_proc(proc_name);
    if (proc_data == NULL) {
        /* report the error */
        opal_output(0, "grpcomm:base:update_modex: received modex info for unknown proc %s\n",
                    ORTE_NAME_PRINT(proc_name));
        return ORTE_ERR_NOT_FOUND;
    }
    
    OPAL_THREAD_LOCK(&proc_data->modex_lock);
    
    /* unpack the number of entries for this proc */
    cnt=1;
    if (ORTE_SUCCESS != (rc = opal_dss.unpack(rbuf, &num_recvd_entries, &cnt, ORTE_STD_CNTR))) {
        ORTE_ERROR_LOG(rc);
        goto cleanup;
    }
    
    /*
     * Extract the attribute names and values
     */
    for (j = 0; j < num_recvd_entries; j++) {
        size_t num_bytes;
        char *attr_name;
        
        cnt = 1;
        if (ORTE_SUCCESS != (rc = opal_dss.unpack(rbuf, &attr_name, &cnt, OPAL_STRING))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        
        cnt = 1;
        if (ORTE_SUCCESS != (rc = opal_dss.unpack(rbuf, &num_bytes, &cnt, OPAL_SIZE))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        if (num_bytes != 0) {
            if (NULL == (bytes = malloc(num_bytes))) {
                ORTE_ERROR_LOG(ORTE_ERR_OUT_OF_RESOURCE);
                rc = ORTE_ERR_OUT_OF_RESOURCE;
                goto cleanup;
            }
            cnt = (orte_std_cntr_t) num_bytes;
            if (ORTE_SUCCESS != (rc = opal_dss.unpack(rbuf, bytes, &cnt, OPAL_BYTE))) {
                ORTE_ERROR_LOG(rc);
                goto cleanup;
            }
            num_bytes = cnt;
        } else {
            bytes = NULL;
        }
        
        /*
         * Lookup the corresponding modex structure
         */
        if (NULL == (attr_data = modex_lookup_attr_data(proc_data, 
                                                        attr_name, true))) {
            opal_output(0, "grpcomm:base:update_modex: modex_lookup_attr_data failed\n");
            rc = ORTE_ERR_NOT_FOUND;
            goto cleanup;
        }
        if (NULL != attr_data->attr_data) {
            /* some pre-existing value must be here - release it */
            free(attr_data->attr_data);
        }
        attr_data->attr_data = bytes;
        attr_data->attr_data_size = num_bytes;
        proc_data->modex_received_data = true;            
    }
    
cleanup:
    OPAL_THREAD_UNLOCK(&proc_data->modex_lock);
    return rc;
}
