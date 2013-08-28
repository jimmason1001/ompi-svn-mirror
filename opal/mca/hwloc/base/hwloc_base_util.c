/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2006 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2011-2012 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2012-2013 Los Alamos National Security, LLC.
 *                         All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */


#include "opal_config.h"

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "opal/runtime/opal.h"
#include "opal/constants.h"
#include "opal/util/argv.h"
#include "opal/util/output.h"
#include "opal/util/show_help.h"
#include "opal/threads/tsd.h"

#include "opal/mca/hwloc/hwloc.h"
#include "opal/mca/hwloc/base/base.h"

/*
 * Provide the hwloc object that corresponds to the given
 * LOGICAL processor id.  Remember: "processor" here [usually] means "core" --
 * except that on some platforms, hwloc won't find any cores; it'll
 * only find PUs (!).  On such platforms, then do the same calculation
 * but with PUs instead of COREs.
 */
static hwloc_obj_t get_pu(hwloc_topology_t topo,  int lid)
{
    hwloc_obj_type_t obj_type = HWLOC_OBJ_CORE;
    hwloc_obj_t obj;

    /* hwloc isn't able to find cores on all platforms.  Example:
       PPC64 running RHEL 5.4 (linux kernel 2.6.18) only reports NUMA
       nodes and PU's.  Fine.  

       However, note that hwloc_get_obj_by_type() will return NULL in
       2 (effectively) different cases:

       - no objects of the requested type were found
       - the Nth object of the requested type was not found

       So first we have to see if we can find *any* cores by looking
       for the 0th core.  If we find it, then try to find the Nth
       core.  Otherwise, try to find the Nth PU. */
    if (NULL == hwloc_get_obj_by_type(topo, HWLOC_OBJ_CORE, 0)) {
        obj_type = HWLOC_OBJ_PU;
    }

    /* Now do the actual lookup. */
    obj = hwloc_get_obj_by_type(topo, obj_type, lid);
    if (NULL == obj) {
        opal_show_help("help-opal-hwloc-base.txt",
                       "logical-cpu-not-found", true,
                       opal_hwloc_base_cpu_set);
        return NULL;
    }

    /* Found the right core (or PU). Return the object */
    return obj;
}

/* determine the node-level available cpuset based on
 * online vs allowed vs user-specified cpus
 */
int opal_hwloc_base_filter_cpus(hwloc_topology_t topo)
{
    hwloc_obj_t root, pu;
    hwloc_cpuset_t avail = NULL, pucpus, res;
    opal_hwloc_topo_data_t *sum;
    char **ranges=NULL, **range=NULL;
    int idx, cpu, start, end;

    root = hwloc_get_root_obj(topo);

    if (NULL == root->userdata) {
        root->userdata = (void*)OBJ_NEW(opal_hwloc_topo_data_t);
    }
    sum = (opal_hwloc_topo_data_t*)root->userdata;

    /* should only ever enter here once, but check anyway */
    if (NULL != sum->available) {
        OPAL_OUTPUT_VERBOSE((5, opal_hwloc_base_framework.framework_output,
                             "hwloc:base:filter_cpus specified - already done"));
        return OPAL_SUCCESS;
    }

    /* process any specified default cpu set against this topology */
    if (NULL == opal_hwloc_base_cpu_set) {
        /* get the root available cpuset */
        avail = hwloc_bitmap_alloc();
        hwloc_bitmap_and(avail, root->online_cpuset, root->allowed_cpuset);
        OPAL_OUTPUT_VERBOSE((5, opal_hwloc_base_framework.framework_output,
                             "hwloc:base: no cpus specified - using root available cpuset"));
    } else {
        OPAL_OUTPUT_VERBOSE((5, opal_hwloc_base_framework.framework_output,
                             "hwloc:base: filtering cpuset"));
        /* find the specified logical cpus */
        ranges = opal_argv_split(opal_hwloc_base_cpu_set, ',');
        avail = hwloc_bitmap_alloc();
        hwloc_bitmap_zero(avail);
        res = hwloc_bitmap_alloc();
        pucpus = hwloc_bitmap_alloc();
        for (idx=0; idx < opal_argv_count(ranges); idx++) {
            range = opal_argv_split(ranges[idx], '-');
            switch (opal_argv_count(range)) {
            case 1:
                /* only one cpu given - get that object */
                cpu = strtoul(range[0], NULL, 10);
                if (NULL == (pu = get_pu(topo, cpu))) {
                    opal_argv_free(ranges);
                    opal_argv_free(range);
                    return OPAL_ERROR;
                }
                hwloc_bitmap_and(pucpus, pu->online_cpuset, pu->allowed_cpuset);
                hwloc_bitmap_or(res, avail, pucpus);
                hwloc_bitmap_copy(avail, res);
                break;
            case 2:
                /* range given */
                start = strtoul(range[0], NULL, 10);
                end = strtoul(range[1], NULL, 10);
                for (cpu=start; cpu <= end; cpu++) {
                    if (NULL == (pu = get_pu(topo, cpu))) {
                        opal_argv_free(ranges);
                        opal_argv_free(range);
                        hwloc_bitmap_free(avail);
                        return OPAL_ERROR;
                    }
                    hwloc_bitmap_and(pucpus, pu->online_cpuset, pu->allowed_cpuset);
                    hwloc_bitmap_or(res, avail, pucpus);
                    hwloc_bitmap_copy(avail, res);
                }
                break;
            default:
                return OPAL_ERR_BAD_PARAM;
            }
            opal_argv_free(range);
        }
        if (NULL != ranges) {
            opal_argv_free(ranges);
        }
        hwloc_bitmap_free(res);
        hwloc_bitmap_free(pucpus);
    }

    /* cache this info */
    sum->available = avail;

    return OPAL_SUCCESS;
}

static void fill_cache_line_size(void)
{
    int i = 0;
    unsigned size;
    hwloc_obj_t obj;
    bool found = false;

    /* Look for the smallest L2 cache size */
    size = 4096;
    while (1) {
        obj = opal_hwloc_base_get_obj_by_type(opal_hwloc_topology,
                                              HWLOC_OBJ_CACHE, 2,
                                              i, OPAL_HWLOC_LOGICAL);
        if (NULL == obj) {
            break;
        } else { 
            found = true;
            if (NULL != obj->attr &&
                size > obj->attr->cache.linesize) {
                size = obj->attr->cache.linesize;
            }
        }
        ++i;
    }

    /* If we found an L2 cache size in the hwloc data, save it in
       opal_cache_line_size.  Otherwise, we'll leave whatever default
       was set in opal_init.c */
    if (found) {
        opal_cache_line_size = (int) size;
    }
}

int opal_hwloc_base_get_topology(void)
{
    int rc;

    OPAL_OUTPUT_VERBOSE((5, opal_hwloc_base_framework.framework_output,
                         "hwloc:base:get_topology"));

    if (0 != hwloc_topology_init(&opal_hwloc_topology) ||
        0 != hwloc_topology_set_flags(opal_hwloc_topology, 
                                      (HWLOC_TOPOLOGY_FLAG_WHOLE_SYSTEM |
                                       HWLOC_TOPOLOGY_FLAG_IO_DEVICES)) ||
        0 != hwloc_topology_load(opal_hwloc_topology)) {
        return OPAL_ERR_NOT_SUPPORTED;
    }

    /* filter the cpus thru any default cpu set */
    rc = opal_hwloc_base_filter_cpus(opal_hwloc_topology);
    if (OPAL_SUCCESS != rc) {
        return rc;
    }

    /* fill opal_cache_line_size global with the smallest L1 cache
       line size */
    fill_cache_line_size();
    return rc;
}

int opal_hwloc_base_set_topology(char *topofile)
{
    hwloc_obj_t obj;
    unsigned j, k;
    struct hwloc_topology_support *support;
    int rc;

     OPAL_OUTPUT_VERBOSE((5, opal_hwloc_base_framework.framework_output,
                         "hwloc:base:set_topology"));

   if (NULL != opal_hwloc_topology) {
        hwloc_topology_destroy(opal_hwloc_topology);
    }
    if (0 != hwloc_topology_init(&opal_hwloc_topology)) {
        return OPAL_ERR_NOT_SUPPORTED;
    }
    if (0 != hwloc_topology_set_xml(opal_hwloc_topology, topofile)) {
        hwloc_topology_destroy(opal_hwloc_topology);
        return OPAL_ERR_NOT_SUPPORTED;
    }
    /* since we are loading this from an external source, we have to
     * explicitly set a flag so hwloc sets things up correctly
     */
    if (0 != hwloc_topology_set_flags(opal_hwloc_topology,
                                      (HWLOC_TOPOLOGY_FLAG_IS_THISSYSTEM |
                                       HWLOC_TOPOLOGY_FLAG_WHOLE_SYSTEM |
                                       HWLOC_TOPOLOGY_FLAG_IO_DEVICES))) {
        hwloc_topology_destroy(opal_hwloc_topology);
        return OPAL_ERR_NOT_SUPPORTED;
    }
    if (0 != hwloc_topology_load(opal_hwloc_topology)) {
        hwloc_topology_destroy(opal_hwloc_topology);
        return OPAL_ERR_NOT_SUPPORTED;
    }
    /* remove the hostname from the topology. Unfortunately, hwloc
     * decided to add the source hostname to the "topology", thus
     * rendering it unusable as a pure topological description. So
     * we remove that information here.
     */
    obj = hwloc_get_root_obj(opal_hwloc_topology);
    for (k=0; k < obj->infos_count; k++) {
        if (NULL == obj->infos[k].name ||
            NULL == obj->infos[k].value) {
            continue;
        }
        if (0 == strncmp(obj->infos[k].name, "HostName", strlen("HostName"))) {
            free(obj->infos[k].name);
            free(obj->infos[k].value);
            /* left justify the array */
            for (j=k; j < obj->infos_count-1; j++) {
                obj->infos[j] = obj->infos[j+1];
            }
            obj->infos[obj->infos_count-1].name = NULL;
            obj->infos[obj->infos_count-1].value = NULL;
            obj->infos_count--;
            break;
        }
    }
    /* unfortunately, hwloc does not include support info in its
     * xml output :-(( We default to assuming it is present as
     * systems that use this option are likely to provide
     * binding support
     */
    support = (struct hwloc_topology_support*)hwloc_topology_get_support(opal_hwloc_topology);
    support->cpubind->set_thisproc_cpubind = true;
    support->membind->set_thisproc_membind = true;

    /* filter the cpus thru any default cpu set */
    rc = opal_hwloc_base_filter_cpus(opal_hwloc_topology);
    if (OPAL_SUCCESS != rc) {
        return rc;
    }

    /* fill opal_cache_line_size global with the smallest L1 cache
       line size */
    fill_cache_line_size();

    /* all done */
    return OPAL_SUCCESS;
}

static void free_object(hwloc_obj_t obj)
{
    opal_hwloc_obj_data_t *data;
    unsigned k;

    /* free any data hanging on this object */
    if (NULL != obj->userdata) {
        data = (opal_hwloc_obj_data_t*)obj->userdata;
        OBJ_RELEASE(data);
        obj->userdata = NULL;
    }

    /* loop thru our children */
    for (k=0; k < obj->arity; k++) {
        free_object(obj->children[k]);
    }
}

void opal_hwloc_base_free_topology(hwloc_topology_t topo)
{
    hwloc_obj_t obj;
    opal_hwloc_topo_data_t *rdata;
    unsigned k;

    obj = hwloc_get_root_obj(topo);
    /* release the root-level userdata */
    if (NULL != obj->userdata) {
        rdata = (opal_hwloc_topo_data_t*)obj->userdata;
        OBJ_RELEASE(rdata);
        obj->userdata = NULL;
    }
    /* now recursively descend and release userdata
     * in the rest of the objects
     */
    for (k=0; k < obj->arity; k++) {
        free_object(obj->children[k]);
    }
    hwloc_topology_destroy(topo);
}

void opal_hwloc_base_get_local_cpuset(void)
{
    hwloc_obj_t root;
    hwloc_cpuset_t base_cpus;

    if (NULL != opal_hwloc_topology) {
        if (NULL == opal_hwloc_my_cpuset) {
            opal_hwloc_my_cpuset = hwloc_bitmap_alloc();
        }

        /* get the cpus we are bound to */
        if (hwloc_get_cpubind(opal_hwloc_topology, 
                              opal_hwloc_my_cpuset, 
                              HWLOC_CPUBIND_PROCESS) < 0) {
            /* we are not bound - use the root's available cpuset */
            root = hwloc_get_root_obj(opal_hwloc_topology);
            base_cpus = opal_hwloc_base_get_available_cpus(opal_hwloc_topology, root);
            hwloc_bitmap_copy(opal_hwloc_my_cpuset, base_cpus);
        }
    }
}

int opal_hwloc_base_report_bind_failure(const char *file,
                                        int line,
                                        const char *msg, int rc)
{
    static int already_reported = 0;

    if (!already_reported &&
        OPAL_HWLOC_BASE_MBFA_SILENT != opal_hwloc_base_mbfa) {
        char hostname[64];
        gethostname(hostname, sizeof(hostname));

        opal_show_help("help-opal-hwloc-base.txt", "mbind failure", true,
                       hostname, getpid(), file, line, msg,
                       (OPAL_HWLOC_BASE_MBFA_WARN == opal_hwloc_base_mbfa) ?
                       "Warning -- your job will continue, but possibly with degraded performance" :
                       "ERROR -- your job may abort or behave erraticly");
        already_reported = 1;
        return rc;
    }

    return OPAL_SUCCESS;
}

hwloc_cpuset_t opal_hwloc_base_get_available_cpus(hwloc_topology_t topo,
                                                  hwloc_obj_t obj)
{
    hwloc_obj_t root;
    hwloc_cpuset_t avail, specd=NULL;
    opal_hwloc_topo_data_t *rdata;
    opal_hwloc_obj_data_t *data;

    OPAL_OUTPUT_VERBOSE((5, opal_hwloc_base_framework.framework_output,
                         "hwloc:base: get available cpus"));

    /* get the node-level information */
    root = hwloc_get_root_obj(topo);
    rdata = (opal_hwloc_topo_data_t*)root->userdata;
    /* bozo check */
    if (NULL == rdata) {
        rdata = OBJ_NEW(opal_hwloc_topo_data_t);
        root->userdata = (void*)rdata;
        OPAL_OUTPUT_VERBOSE((5, opal_hwloc_base_framework.framework_output,
                             "hwloc:base:get_available_cpus first time - filtering cpus"));
    }
    /* ensure the topo-level cpuset was prepared */
    opal_hwloc_base_filter_cpus(topo);

    /* are we asking about the root object? */
    if (obj == root) {
        OPAL_OUTPUT_VERBOSE((5, opal_hwloc_base_framework.framework_output,
                             "hwloc:base:get_available_cpus root object"));
        return rdata->available;
    }

    /* some hwloc object types don't have cpus */
    if (NULL == obj->online_cpuset || NULL == obj->allowed_cpuset) {
        return NULL;
    }

    /* see if we already have this info */
    if (NULL == (data = (opal_hwloc_obj_data_t*)obj->userdata)) {
        /* nope - create the object */
        data = OBJ_NEW(opal_hwloc_obj_data_t);
        obj->userdata = (void*)data;
    }

    /* do we have the cpuset */
    if (NULL != data->available) {
        return data->available;
    }

    /* find the available processors on this object */
    avail = hwloc_bitmap_alloc();
    hwloc_bitmap_and(avail, obj->online_cpuset, obj->allowed_cpuset);

    /* filter this against the node-available processors */
    if (NULL == rdata->available) {
        hwloc_bitmap_free(avail);
        return NULL;
    }
    specd = hwloc_bitmap_alloc();
    hwloc_bitmap_and(specd, avail, rdata->available);

    /* cache the info */
    data->available = specd;

    /* cleanup */
    hwloc_bitmap_free(avail);
    return specd;
}

static void df_search_cores(hwloc_obj_t obj, unsigned int *cnt)
{
    unsigned k;

    if (HWLOC_OBJ_CORE == obj->type) {
        *cnt += 1;
        return;
    }

    for (k=0; k < obj->arity; k++) {
        df_search_cores(obj->children[k], cnt);
    }
    return;
}

/* determine if there is a single cpu in a bitmap */
bool opal_hwloc_base_single_cpu(hwloc_cpuset_t cpuset)
{
    int i;
    bool one=false;

    /* count the number of bits that are set - there is
     * one bit for each available pu. We could just
     * subtract the first and last indices, but there
     * may be "holes" in the bitmap corresponding to
     * offline or unallowed cpus - so we have to
     * search for them. Return false if we anything
     * other than one
     */
    for (i=hwloc_bitmap_first(cpuset);
         i <= hwloc_bitmap_last(cpuset);
         i++) {
        if (hwloc_bitmap_isset(cpuset, i)) {
            if (one) {
                return false;
            }
            one = true;
        }
    }

    return one;
}

/* get the number of pu's under a given hwloc object */
unsigned int opal_hwloc_base_get_npus(hwloc_topology_t topo,
                                      hwloc_obj_t obj)
{
    opal_hwloc_obj_data_t *data;
    int i;
    unsigned int cnt;
    hwloc_cpuset_t cpuset;

    /* if the object is a hwthread (i.e., HWLOC_OBJ_PU),
     * then the answer is always 1 since there isn't
     * anything underneath it
     */
    if (HWLOC_OBJ_PU == obj->type) {
        return 1;
    }

    /* if the object is a core (i.e., HWLOC_OBJ_CORE) and
     * we are NOT treating hwthreads as independent cpus,
     * then the answer is also 1 since we don't allow
     * you to use the underlying hwthreads as separate
     * entities
     */
    if (HWLOC_OBJ_CORE == obj->type &&
        !opal_hwloc_use_hwthreads_as_cpus) {
        return 1;
    }

    data = (opal_hwloc_obj_data_t*)obj->userdata;

    if (NULL == data || 0 == data->npus) {
        if (!opal_hwloc_use_hwthreads_as_cpus) {
            /* if we are treating cores as cpus, then we really
             * want to know how many cores are in this object.
             * hwloc sets a bit for each "pu", so we can't just
             * count bits in this case as there may be more than
             * one hwthread/core. Instead, find the number of cores
             * in the system
             *
             * NOTE: remember, hwloc can't find "cores" in all
             * environments. So first check to see if it found
             * "core" at all.
             */
            if (NULL != hwloc_get_obj_by_type(topo, HWLOC_OBJ_CORE, 0)) {
                /* starting at the incoming obj, do a down-first search
                 * and count the number of cores under it
                 */
                cnt = 0;
                df_search_cores(obj, &cnt);
            }
        } else {

            /* if we are treating cores as cpus, or the system can't detect
             * "cores", then get the available cpuset for this object - this will
             * create and store the data
             */
            if (NULL == (cpuset = opal_hwloc_base_get_available_cpus(topo, obj))) {
                return 0;
            }
            /* count the number of bits that are set - there is
             * one bit for each available pu. We could just
             * subtract the first and last indices, but there
             * may be "holes" in the bitmap corresponding to
             * offline or unallowed cpus - so we have to
             * search for them
             */
            for (i=hwloc_bitmap_first(cpuset), cnt=0;
                 i <= hwloc_bitmap_last(cpuset);
                 i++) {
                if (hwloc_bitmap_isset(cpuset, i)) {
                    cnt++;
                }
            }
        }
        /* cache the info */
        if (NULL == data) {
            data = OBJ_NEW(opal_hwloc_obj_data_t);
            obj->userdata = (void*)data;
        }
        data->npus = cnt;
    }

    return data->npus;
}

unsigned int opal_hwloc_base_get_obj_idx(hwloc_topology_t topo,
                                         hwloc_obj_t obj,
                                         opal_hwloc_resource_type_t rtype)
{
    unsigned cache_level=0;
    opal_hwloc_obj_data_t *data;
    hwloc_obj_t ptr;
    unsigned int nobjs, i;

    OPAL_OUTPUT_VERBOSE((5, opal_hwloc_base_framework.framework_output,
                         "hwloc:base:get_idx"));

    /* see if we already have the info */
    data = (opal_hwloc_obj_data_t*)obj->userdata;

    if (NULL == data) {
        data = OBJ_NEW(opal_hwloc_obj_data_t);
        obj->userdata = (void*)data;
    }

    if (data->idx < UINT_MAX) {
        OPAL_OUTPUT_VERBOSE((5, opal_hwloc_base_framework.framework_output,
                             "hwloc:base:get_idx already have data: %u",
                             data->idx));
        return data->idx;
    }

    /* determine the number of objects of this type */
    if (HWLOC_OBJ_CACHE == obj->type) {
        cache_level = obj->attr->cache.depth;
    }
    nobjs = opal_hwloc_base_get_nbobjs_by_type(topo, obj->type, cache_level, rtype);

    OPAL_OUTPUT_VERBOSE((5, opal_hwloc_base_framework.framework_output,
                         "hwloc:base:get_idx found %u objects of type %s:%u",
                         nobjs, hwloc_obj_type_string(obj->type), cache_level));

    /* find this object */
    for (i=0; i < nobjs; i++) {
        ptr = opal_hwloc_base_get_obj_by_type(topo, obj->type, cache_level, i, rtype);
        if (ptr == obj) {
            data->idx = i;
            return i;
        }
    }
    /* if we get here, it wasn't found */
    opal_show_help("help-opal-hwloc-base.txt",
                   "obj-idx-failed", true,
                   hwloc_obj_type_string(obj->type), cache_level);
    return UINT_MAX;
}

/* hwloc treats cache objects as special
 * cases. Instead of having a unique type for each cache level,
 * there is a single cache object type, and the level is encoded
 * in an attribute union. So looking for cache objects involves
 * a multi-step test :-(
 *
 * And, of course, we make things even worse because we don't
 * always care about what is physically or logically present,
 * but rather what is available to us. For example, we don't
 * want to map or bind to a cpu that is offline, or one that
 * we aren't allowed by use by the OS. So we have to also filter
 * the search to avoid those objects that don't have any cpus
 * we can use :-((
 */
static hwloc_obj_t df_search(hwloc_topology_t topo,
                             hwloc_obj_t start,
                             hwloc_obj_type_t target,
                             unsigned cache_level,
                             unsigned int nobj,
                             opal_hwloc_resource_type_t rtype,
                             unsigned int *idx,
                             unsigned int *num_objs)
{
    unsigned k;
    hwloc_obj_t obj;
    opal_hwloc_obj_data_t *data;

    if (target == start->type) {
        if (HWLOC_OBJ_CACHE == start->type && cache_level != start->attr->cache.depth) {
            goto notfound;
        }
        if (OPAL_HWLOC_LOGICAL == rtype) {
            /* the hwloc tree is composed of LOGICAL objects, so the only
             * time we come here is when we are looking for logical caches
             */
            if (NULL != num_objs) {
                /* we are counting the number of caches at this level */
                *num_objs += 1;
            } else if (*idx == nobj) {
                /* found the specific instance of the cache level being sought */
                return start;
            }
            *idx += 1;
            return NULL;
        }
        if (OPAL_HWLOC_PHYSICAL == rtype) {
            /* the PHYSICAL object number is stored as the os_index. When
             * counting physical objects, we can't just count the number
             * that are in the hwloc tree as the only entries in the tree
             * are LOGICAL objects - i.e., any physical gaps won't show. So
             * we instead return the MAX os_index, as this is the best we
             * can do to tell you how many PHYSICAL objects are in the system.
             *
             * NOTE: if the last PHYSICAL object is not present (e.g., the last
             * socket on the node is empty), then the count we return will
             * be wrong!
             */
            if (NULL != num_objs) {
                /* we are counting the number of these objects */
                if (*num_objs < (unsigned int)start->os_index) {
                    *num_objs = (unsigned int)start->os_index;
                }
            } else if (*idx == nobj) {
                /* found the specific instance of the cache level being sought */
                return start;
            }
            *idx += 1;
            return NULL;
        }
        if (OPAL_HWLOC_AVAILABLE == rtype) {
            /* check - do we already know the index of this object */
            data = (opal_hwloc_obj_data_t*)start->userdata;
            if (NULL == data) {
                data = OBJ_NEW(opal_hwloc_obj_data_t);
                start->userdata = (void*)data;
            }
            /* if we already know our location and it matches,
             * then we are good
             */
            if (UINT_MAX != data->idx && data->idx == nobj) {
                return start;
            }
            /* see if we already know our available cpuset */
            if (NULL == data->available) {
                data->available = opal_hwloc_base_get_available_cpus(topo, start);
            }
            if (NULL != data->available && !hwloc_bitmap_iszero(data->available)) {
                if (NULL != num_objs) {
                    *num_objs += 1;
                } else if (*idx == nobj) {
                    /* cache the location */
                    data->idx = *idx;
                    return start;
                }
                *idx += 1;
            }
            return NULL;
        }
        /* if it wasn't one of the above, then we are lost */
        return NULL;
    }

 notfound:
    for (k=0; k < start->arity; k++) {
        obj = df_search(topo, start->children[k], target, cache_level, nobj, rtype, idx, num_objs);
        if (NULL != obj) {
            return obj;
        }
    }
    
    return NULL;
}

unsigned int opal_hwloc_base_get_nbobjs_by_type(hwloc_topology_t topo,
                                                hwloc_obj_type_t target,
                                                unsigned cache_level,
                                                opal_hwloc_resource_type_t rtype)
{
    unsigned int num_objs, idx;
    hwloc_obj_t obj;
    opal_list_item_t *item;
    opal_hwloc_summary_t *sum;
    opal_hwloc_topo_data_t *data;
    int rc;

    /* bozo check */
    if (NULL == topo) {
        OPAL_OUTPUT_VERBOSE((5, opal_hwloc_base_framework.framework_output,
                             "hwloc:base:get_nbobjs NULL topology"));
        return 0;
    }

    /* if we want the number of LOGICAL objects, we can just
     * use the hwloc accessor to get it, unless it is a CACHE
     * as these are treated as special cases
     */
    if (OPAL_HWLOC_LOGICAL == rtype && HWLOC_OBJ_CACHE != target) {
        /* we should not get an error back, but just in case... */
        if (0 > (rc = hwloc_get_nbobjs_by_type(topo, target))) {
            opal_output(0, "UNKNOWN HWLOC ERROR");
            return 0;
        }
        return rc;
    }

    /* for everything else, we have to do some work */
    num_objs = 0;
    idx = 0;
    obj = hwloc_get_root_obj(topo);

    /* first see if the topology already has this summary */
    data = (opal_hwloc_topo_data_t*)obj->userdata;
    if (NULL == data) {
        data = OBJ_NEW(opal_hwloc_topo_data_t);
        obj->userdata = (void*)data;
    } else {
        for (item = opal_list_get_first(&data->summaries);
             item != opal_list_get_end(&data->summaries);
             item = opal_list_get_next(item)) {
            sum = (opal_hwloc_summary_t*)item;
            if (target == sum->type &&
                cache_level == sum->cache_level &&
                rtype == sum->rtype) {
                /* yep - return the value */
                OPAL_OUTPUT_VERBOSE((5, opal_hwloc_base_framework.framework_output,
                                     "hwloc:base:get_nbojbs pre-existing data %u of %s:%u",
                                     sum->num_objs, hwloc_obj_type_string(target), cache_level));
                return sum->num_objs;
            }
        }
    }

    /* don't already know it - go get it */
    df_search(topo, obj, target, cache_level, 0, rtype, &idx, &num_objs);

    /* cache the results for later */
    sum = OBJ_NEW(opal_hwloc_summary_t);
    sum->type = target;
    sum->cache_level = cache_level;
    sum->num_objs = num_objs;
    sum->rtype = rtype;
    opal_list_append(&data->summaries, &sum->super);

    OPAL_OUTPUT_VERBOSE((5, opal_hwloc_base_framework.framework_output,
                         "hwloc:base:get_nbojbs computed data %u of %s:%u",
                         num_objs, hwloc_obj_type_string(target), cache_level));

    return num_objs;
}

static hwloc_obj_t df_search_min_bound(hwloc_topology_t topo,
                                       hwloc_obj_t start,
                                       hwloc_obj_type_t target,
                                       unsigned cache_level,
                                       unsigned int *min_bound)
{
    unsigned k;
    hwloc_obj_t obj, save=NULL;
    opal_hwloc_obj_data_t *data;

    if (target == start->type) {
        if (HWLOC_OBJ_CACHE == start->type && cache_level != start->attr->cache.depth) {
            goto notfound;
        }
        /* see how many procs are bound to us */
        data = (opal_hwloc_obj_data_t*)start->userdata;
        if (NULL == data) {
            data = OBJ_NEW(opal_hwloc_obj_data_t);
            start->userdata = data;
        }
        OPAL_OUTPUT_VERBOSE((5, opal_hwloc_base_framework.framework_output,
                             "hwloc:base:min_bound_under_obj object %s:%u nbound %u min %u",
                             hwloc_obj_type_string(target), start->logical_index,
                             data->num_bound, *min_bound));
        if (data->num_bound < *min_bound) {
            *min_bound = data->num_bound;
            return start;
        }
        /* if we have more procs bound to us than the min, return NULL */
        return NULL;
    }

 notfound:
    for (k=0; k < start->arity; k++) {
        obj = df_search_min_bound(topo, start->children[k], target, cache_level, min_bound);
        if (NULL != obj) {
            save = obj;
        }
        /* if the target level is HWTHREAD and we are NOT treating
         * hwthreads as separate cpus, then we can only consider
         * the 0th hwthread on a core
         */
        if (HWLOC_OBJ_CORE == start->type && HWLOC_OBJ_PU == target &&
            !opal_hwloc_use_hwthreads_as_cpus) {
            break;
        }
    }
    
    return save;
}

hwloc_obj_t opal_hwloc_base_find_min_bound_target_under_obj(hwloc_topology_t topo,
                                                            hwloc_obj_t obj,
                                                            hwloc_obj_type_t target,
                                                            unsigned cache_level)
{
    unsigned int min_bound;
    hwloc_obj_t loc;

    /* bozo check */
    if (NULL == topo || NULL == obj) {
        OPAL_OUTPUT_VERBOSE((5, opal_hwloc_base_framework.framework_output,
                             "hwloc:base:find_min_bound_under_obj NULL %s",
                             (NULL == topo) ? "topology" : "object"));
        return NULL;
    }


    /* if the object and target is the same type, then there is
     * nothing under it, so just return itself
     */
    if (target == obj->type) {
        /* again, we have to treat caches differently as
         * the levels distinguish them
         */
        if (HWLOC_OBJ_CACHE == target &&
            cache_level < obj->attr->cache.depth) {
            goto moveon;
        }
        return obj;
    }

 moveon:
    /* the hwloc accessors all report at the topo level,
     * so we have to do some work
     */
    min_bound = UINT_MAX;

    loc = df_search_min_bound(topo, obj, target, cache_level, &min_bound);

    if (NULL != loc) {
        if (HWLOC_OBJ_CACHE == target) {
            OPAL_OUTPUT_VERBOSE((5, opal_hwloc_base_framework.framework_output,
                        "hwloc:base:min_bound_under_obj found min bound of %u on %s:%u:%u",
                        min_bound, hwloc_obj_type_string(target),
                        cache_level, loc->logical_index));
        } else {
            OPAL_OUTPUT_VERBOSE((5, opal_hwloc_base_framework.framework_output,
                        "hwloc:base:min_bound_under_obj found min bound of %u on %s:%u",
                        min_bound, hwloc_obj_type_string(target), loc->logical_index));
        }
    }

    return loc;
}

/* as above, only return the Nth instance of the specified object
 * type from inside the topology
 */
hwloc_obj_t opal_hwloc_base_get_obj_by_type(hwloc_topology_t topo,
                                            hwloc_obj_type_t target,
                                            unsigned cache_level,
                                            unsigned int instance,
                                            opal_hwloc_resource_type_t rtype)
{
    unsigned int idx;
    hwloc_obj_t obj;

    /* bozo check */
    if (NULL == topo) {
        return NULL;
    }

    /* if we want the nth LOGICAL object, we can just
     * use the hwloc accessor to get it, unless it is a CACHE
     * as these are treated as special cases
     */
    if (OPAL_HWLOC_LOGICAL == rtype && HWLOC_OBJ_CACHE != target) {
        return hwloc_get_obj_by_type(topo, target, instance);
    }

    /* for everything else, we have to do some work */
    idx = 0;
    obj = hwloc_get_root_obj(topo);
    return df_search(topo, obj, target, cache_level, instance, rtype, &idx, NULL);
}

static void df_clear(hwloc_topology_t topo,
                     hwloc_obj_t start)
{
    unsigned k;
    opal_hwloc_obj_data_t *data;

    /* see how many procs are bound to us */
    data = (opal_hwloc_obj_data_t*)start->userdata;
    if (NULL != data) {
        data->num_bound = 0;
    }

    for (k=0; k < start->arity; k++) {
        df_clear(topo, start->children[k]);
    }
}

void opal_hwloc_base_clear_usage(hwloc_topology_t topo)
{
    hwloc_obj_t root;
    unsigned k;

    /* bozo check */
    if (NULL == topo) {
        OPAL_OUTPUT_VERBOSE((5, opal_hwloc_base_framework.framework_output,
                             "hwloc:base:clear_usage: NULL topology"));
        return;
    }

    root = hwloc_get_root_obj(topo);
    /* must not start at root as the root object has
     * a different userdata attached to it
     */
    for (k=0; k < root->arity; k++) {
        df_clear(topo, root->children[k]);
    }
}

/* The current slot_list notation only goes to the core level - i.e., the location
 * is specified as socket:core. Thus, the code below assumes that all locations
 * are to be parsed under that notation.
 */

static int socket_to_cpu_set(char *cpus,
                             hwloc_topology_t topo,
                             hwloc_bitmap_t cpumask)
{
    char **range;
    int range_cnt;
    int lower_range, upper_range;
    int socket_id;
    hwloc_obj_t obj;
    hwloc_bitmap_t res;

    if ('*' == cpus[0]) {
        /* requesting cpumask for ALL sockets */
        obj = hwloc_get_root_obj(topo);
        /* set to all available logical processors - essentially,
         * this specification equates to unbound
         */
        res = opal_hwloc_base_get_available_cpus(topo, obj);
        hwloc_bitmap_or(cpumask, cpumask, res);
        return OPAL_SUCCESS;
    }

    range = opal_argv_split(cpus,'-');
    range_cnt = opal_argv_count(range);
    switch (range_cnt) {
    case 1:  /* no range was present, so just one socket given */
        socket_id = atoi(range[0]);
        obj = opal_hwloc_base_get_obj_by_type(topo, HWLOC_OBJ_SOCKET, 0, socket_id, OPAL_HWLOC_LOGICAL);
        /* get the available logical cpus for this socket */
        res = opal_hwloc_base_get_available_cpus(topo, obj);
        hwloc_bitmap_or(cpumask, cpumask, res);
        break;
                
    case 2:  /* range of sockets was given */
        lower_range = atoi(range[0]);
        upper_range = atoi(range[1]);
        /* cycle across the range of sockets */
        for (socket_id=lower_range; socket_id<=upper_range; socket_id++) {
            obj = opal_hwloc_base_get_obj_by_type(topo, HWLOC_OBJ_SOCKET, 0, socket_id, OPAL_HWLOC_LOGICAL);
            /* get the available logical cpus for this socket */
            res = opal_hwloc_base_get_available_cpus(topo, obj);
            /* set the corresponding bits in the bitmask */
            hwloc_bitmap_or(cpumask, cpumask, res);
        }
        break;
    default:
        opal_argv_free(range);
        return OPAL_ERROR;
    }
    opal_argv_free(range);

    return OPAL_SUCCESS;
}

static int socket_core_to_cpu_set(char *socket_core_list,
                                  hwloc_topology_t topo,
                                  hwloc_bitmap_t cpumask)
{
    int rc=OPAL_SUCCESS, i;
    char **socket_core, *corestr;
    char **range;
    int range_cnt;
    int lower_range, upper_range;
    int socket_id, core_id;
    hwloc_obj_t socket, core;
    hwloc_cpuset_t res;
    unsigned int idx;
    hwloc_obj_type_t obj_type = HWLOC_OBJ_CORE;

    socket_core = opal_argv_split(socket_core_list, ':');
    socket_id = atoi(socket_core[0]);
    
    /* get the object for this socket id */
    if (NULL == (socket = opal_hwloc_base_get_obj_by_type(topo, HWLOC_OBJ_SOCKET, 0,
                                                          socket_id, OPAL_HWLOC_LOGICAL))) {
        opal_argv_free(socket_core);
        return OPAL_ERR_NOT_FOUND;
    }

    /* as described in comment near top of file, hwloc isn't able
     * to find cores on all platforms. Adjust the type here if
     * required
     */
    if (NULL == hwloc_get_obj_by_type(topo, HWLOC_OBJ_CORE, 0)) {
        obj_type = HWLOC_OBJ_PU;
    }

    for (i=1; NULL != socket_core[i]; i++) {
        if ('C' == socket_core[i][0] ||
            'c' == socket_core[i][0]) {
            corestr = &socket_core[i][1];
        } else {
            corestr = socket_core[i];
        }
        if ('*' == corestr[0]) {
            /* set to all available logical cpus on this socket */
            res = opal_hwloc_base_get_available_cpus(topo, socket);
            hwloc_bitmap_or(cpumask, cpumask, res);
            /* we are done - already assigned all cores! */
            rc = OPAL_SUCCESS;
            break;
        } else {
            range = opal_argv_split(corestr, '-');
            range_cnt = opal_argv_count(range);
            /* see if a range was set or not */
            switch (range_cnt) {
            case 1:  /* only one core specified */
                core_id = atoi(range[0]);
                /* get that object */
                idx = 0;
                if (NULL == (core = df_search(topo, socket, obj_type, 0,
                                              core_id, OPAL_HWLOC_AVAILABLE,
                                              &idx, NULL))) {
                    return OPAL_ERR_NOT_FOUND;
                }
                /* get the cpus */
                res = opal_hwloc_base_get_available_cpus(topo, core);
                hwloc_bitmap_or(cpumask, cpumask, res);
                break;
                
            case 2:  /* range of core id's was given */
                opal_output_verbose(5, opal_hwloc_base_framework.framework_output,
                                    "range of cores given: start %s stop %s",
                                    range[0], range[1]);
                lower_range = atoi(range[0]);
                upper_range = atoi(range[1]);
                for (core_id=lower_range; core_id <= upper_range; core_id++) {
                    /* get that object */
                    idx = 0;
                    if (NULL == (core = df_search(topo, socket, obj_type, 0,
                                                  core_id, OPAL_HWLOC_AVAILABLE,
                                                  &idx, NULL))) {
                        return OPAL_ERR_NOT_FOUND;
                    }
                    /* get the cpus */
                    res = opal_hwloc_base_get_available_cpus(topo, core);
                    /* add them into the result */
                    hwloc_bitmap_or(cpumask, cpumask, res);
                }
                break;
                
            default:
                opal_argv_free(range);
                opal_argv_free(socket_core);
                return OPAL_ERROR;
            }
            opal_argv_free(range);
        }
    }
    opal_argv_free(socket_core);

    return rc;
}

int opal_hwloc_base_slot_list_parse(const char *slot_str,
                                    hwloc_topology_t topo,
                                    hwloc_cpuset_t cpumask)
{
    char **item;
    int rc, i;
    hwloc_obj_t pu;
    hwloc_cpuset_t pucpus;
    char **range;
    size_t range_cnt;
    int core_id, lower_range, upper_range;

    /* bozo checks */
    if (NULL == opal_hwloc_topology) {
        return OPAL_ERR_NOT_SUPPORTED;
    }
    if (NULL == slot_str || 0 == strlen(slot_str)) {
        return OPAL_ERR_BAD_PARAM;
    }
    
    opal_output_verbose(5, opal_hwloc_base_framework.framework_output,
                        "slot assignment: slot_list == %s",
                        slot_str);
    
    /* split at ';' */
    item = opal_argv_split(slot_str, ';');

    /* start with a clean mask */
    hwloc_bitmap_zero(cpumask);
    /* loop across the items and accumulate the mask */
    for (i=0; NULL != item[i]; i++) {
        opal_output_verbose(5, opal_hwloc_base_framework.framework_output,
                            "working assignment %s",
                            item[i]);
        /* if they specified "socket" by starting with an S/s,
         * or if they use socket:core notation, then parse the
         * socket/core info
         */
        if ('S' == item[i][0] ||
            's' == item[i][0] ||
            NULL != strchr(item[i], ':')) {
            /* specified a socket */
            if (NULL == strchr(item[i], ':')) {
                /* binding just to the socket level, though
                 * it could specify multiple sockets
                 */
                if (OPAL_SUCCESS != (rc = socket_to_cpu_set(&item[i][1],  /* skip the 'S' */
                                                            topo, cpumask))) {
                    opal_argv_free(item);
                    return rc;
                }
            } else {
                /* binding to a socket/whatever specification */
                if ('S' == item[i][0] ||
                    's' == item[i][0]) {
                    if (OPAL_SUCCESS != (rc = socket_core_to_cpu_set(&item[i][1],  /* skip the 'S' */
                                                                     topo, cpumask))) {
                        opal_argv_free(item);
                        return rc;
                    }
                } else {
                    if (OPAL_SUCCESS != (rc = socket_core_to_cpu_set(item[i],
                                                                     topo, cpumask))) {
                        opal_argv_free(item);
                        return rc;
                    }
                }
            }
        } else {
            /* just a core specification - see if one or a range was given */
            range = opal_argv_split(item[i], '-');
            range_cnt = opal_argv_count(range);
            /* see if a range was set or not */
            switch (range_cnt) {
            case 1:  /* only one core specified */
                core_id = atoi(range[0]);
                /* find the specified logical available cpu */
                if (NULL == (pu = get_pu(topo, core_id))) {
                    opal_argv_free(range);
                    opal_argv_free(item);
                    return OPAL_ERROR;
                }
                /* get the available cpus for that object */
                pucpus = opal_hwloc_base_get_available_cpus(topo, pu);
                 /* set that in the mask */
                hwloc_bitmap_or(cpumask, cpumask, pucpus);
                break;
                    
            case 2:  /* range of core id's was given */
                lower_range = atoi(range[0]);
                upper_range = atoi(range[1]);
                for (core_id=lower_range; core_id <= upper_range; core_id++) {
                    /* find the specified logical available cpu */
                    if (NULL == (pu = get_pu(topo, core_id))) {
                        opal_argv_free(range);
                        opal_argv_free(item);
                        return OPAL_ERROR;
                    }
                    /* get the available cpus for that object */
                    pucpus = opal_hwloc_base_get_available_cpus(topo, pu);
                    /* set that in the mask */
                    hwloc_bitmap_or(cpumask, cpumask, pucpus);
                }
                break;
                
            default:
                opal_argv_free(range);
                opal_argv_free(item);
                return OPAL_ERROR;
            }
        }
    }
    opal_argv_free(item);
    return OPAL_SUCCESS;
}

opal_hwloc_locality_t opal_hwloc_base_get_relative_locality(hwloc_topology_t topo,
                                                            char *cpuset1, char *cpuset2)
{
    opal_hwloc_locality_t locality;
    hwloc_obj_t obj;
    unsigned depth, d, width, w;
    hwloc_cpuset_t avail;
    bool shared;
    hwloc_obj_type_t type;
    int sect1, sect2;
    hwloc_cpuset_t loc1, loc2;

    /* start with what we know - they share a node on a cluster
     * NOTE: we may alter that latter part as hwloc's ability to
     * sense multi-cu, multi-cluster systems grows
     */
    locality = OPAL_PROC_ON_CLUSTER | OPAL_PROC_ON_CU | OPAL_PROC_ON_NODE | OPAL_PROC_ON_BOARD;

    /* if either cpuset is NULL, then that isn't bound */
    if (NULL == cpuset1 || NULL == cpuset2) {
        return locality;
    }

    /* get the max depth of the topology */
    depth = hwloc_topology_get_depth(topo);

    /* convert the strings to cpusets */
    loc1 = hwloc_bitmap_alloc();
    hwloc_bitmap_list_sscanf(loc1, cpuset1);
    loc2 = hwloc_bitmap_alloc();
    hwloc_bitmap_list_sscanf(loc2, cpuset2);

    /* start at the first depth below the top machine level */
    for (d=1; d < depth; d++) {
        shared = false;
        /* get the object type at this depth */
        type = hwloc_get_depth_type(topo, d);
        /* if it isn't one of interest, then ignore it */
        if (HWLOC_OBJ_NODE != type &&
            HWLOC_OBJ_SOCKET != type &&
            HWLOC_OBJ_CACHE != type &&
            HWLOC_OBJ_CORE != type &&
            HWLOC_OBJ_PU != type) {
            continue;
        }
        /* get the width of the topology at this depth */
        width = hwloc_get_nbobjs_by_depth(topo, d);

        /* scan all objects at this depth to see if
         * our locations overlap with them
         */
        for (w=0; w < width; w++) {
            /* get the object at this depth/index */
            obj = hwloc_get_obj_by_depth(topo, d, w);
            /* get the available cpuset for this obj */
            avail = opal_hwloc_base_get_available_cpus(topo, obj);
            /* see if our locations intersect with it */
            sect1 = hwloc_bitmap_intersects(avail, loc1);
            sect2 = hwloc_bitmap_intersects(avail, loc2);
            /* if both intersect, then we share this level */
            if (sect1 && sect2) {
                shared = true;
                switch(obj->type) {
                case HWLOC_OBJ_NODE:
                    locality |= OPAL_PROC_ON_NUMA;
                    break;
                case HWLOC_OBJ_SOCKET:
                    locality |= OPAL_PROC_ON_SOCKET;
                    break;
                case HWLOC_OBJ_CACHE:
                    if (3 == obj->attr->cache.depth) {
                        locality |= OPAL_PROC_ON_L3CACHE;
                    } else if (2 == obj->attr->cache.depth) {
                        locality |= OPAL_PROC_ON_L2CACHE;
                    } else {
                        locality |= OPAL_PROC_ON_L1CACHE;
                    }
                    break;
                case HWLOC_OBJ_CORE:
                    locality |= OPAL_PROC_ON_CORE;
                    break;
                case HWLOC_OBJ_PU:
                    locality |= OPAL_PROC_ON_HWTHREAD;
                    break;
                default:
                    /* just ignore it */
                    break;
                }
                break;
            }
            /* otherwise, we don't share this
             * object - but we still might share another object
             * on this level, so we have to keep searching
             */
        }
        /* if we spanned the entire width without finding
         * a point of intersection, then no need to go
         * deeper
         */
        if (!shared) {
            break;
        }
    }

    opal_output_verbose(5, opal_hwloc_base_framework.framework_output,
                        "locality: %s",
                        opal_hwloc_base_print_locality(locality));

    return locality;
}

char* opal_hwloc_base_print_binding(opal_binding_policy_t binding)
{
    char *ret, *bind;
    opal_hwloc_print_buffers_t *ptr;

    switch(OPAL_GET_BINDING_POLICY(binding)) {
    case OPAL_BIND_TO_NONE:
        bind = "NONE";
        break;
    case OPAL_BIND_TO_BOARD:
        bind = "BOARD";
        break;
    case OPAL_BIND_TO_NUMA:
        bind = "NUMA";
        break;
    case OPAL_BIND_TO_SOCKET:
        bind = "SOCKET";
        break;
    case OPAL_BIND_TO_L3CACHE:
        bind = "L3CACHE";
        break;
    case OPAL_BIND_TO_L2CACHE:
        bind = "L2CACHE";
        break;
    case OPAL_BIND_TO_L1CACHE:
        bind = "L1CACHE";
        break;
    case OPAL_BIND_TO_CORE:
        bind = "CORE";
        break;
    case OPAL_BIND_TO_HWTHREAD:
        bind = "HWTHREAD";
        break;
    case OPAL_BIND_TO_CPUSET:
        bind = "CPUSET";
        break;
    default:
        bind = "UNKNOWN";
    }
    ptr = opal_hwloc_get_print_buffer();
    if (NULL == ptr) {
        return opal_hwloc_print_null;
    }
    /* cycle around the ring */
    if (OPAL_HWLOC_PRINT_NUM_BUFS == ptr->cntr) {
        ptr->cntr = 0;
    }
    if (!OPAL_BINDING_REQUIRED(binding) &&
        OPAL_BIND_OVERLOAD_ALLOWED(binding)) {
        snprintf(ptr->buffers[ptr->cntr], OPAL_HWLOC_PRINT_MAX_SIZE,
                 "%s:IF-SUPPORTED:OVERLOAD-ALLOWED", bind);
    } else if (OPAL_BIND_OVERLOAD_ALLOWED(binding)) {
        snprintf(ptr->buffers[ptr->cntr], OPAL_HWLOC_PRINT_MAX_SIZE,
                 "%s:OVERLOAD-ALLOWED", bind);
    } else if (!OPAL_BINDING_REQUIRED(binding)) {
        snprintf(ptr->buffers[ptr->cntr], OPAL_HWLOC_PRINT_MAX_SIZE,
                 "%s:IF-SUPPORTED", bind);
    } else {
        snprintf(ptr->buffers[ptr->cntr], OPAL_HWLOC_PRINT_MAX_SIZE, "%s", bind);
    }
    ret = ptr->buffers[ptr->cntr];
    ptr->cntr++;

    return ret;
}

/*
 * Turn an int bitmap to a "a-b,c" range kind of string
 */
static char *bitmap2rangestr(int bitmap)
{
    size_t i;
    int range_start, range_end;
    bool first, isset;
    char tmp[BUFSIZ];
    const int stmp = sizeof(tmp) - 1;
    static char ret[BUFSIZ];

    memset(ret, 0, sizeof(ret));

    first = true;
    range_start = -999;
    for (i = 0; i < sizeof(int) * 8; ++i) {
        isset = (bitmap & (1 << i));

        /* Do we have a running range? */
        if (range_start >= 0) {
            if (isset) {
                continue;
            } else {
                /* A range just ended; output it */
                if (!first) {
                    strncat(ret, ",", sizeof(ret) - strlen(ret));
                    first = false;
                }

                range_end = i - 1;
                if (range_start == range_end) {
                    snprintf(tmp, stmp, "%d", range_start);
                } else {
                    snprintf(tmp, stmp, "%d-%d", range_start, range_end);
                }
                strncat(ret, tmp, sizeof(ret) - strlen(ret));

                range_start = -999;
            }
        }

        /* No running range */
        else {
            if (isset) {
                range_start = i;
            }
        }
    }

    /* If we ended the bitmap with a range open, output it */
    if (range_start >= 0) {
        if (!first) {
            strncat(ret, ",", sizeof(ret) - strlen(ret));
            first = false;
        }

        range_end = i - 1;
        if (range_start == range_end) {
            snprintf(tmp, stmp, "%d", range_start);
        } else {
            snprintf(tmp, stmp, "%d-%d", range_start, range_end);
        }
        strncat(ret, tmp, sizeof(ret) - strlen(ret));
    }

    return ret;
}

/*
 * Make a map of socket/core/hwthread tuples
 */
static int build_map(int *num_sockets_arg, int *num_cores_arg, 
                     hwloc_cpuset_t cpuset, int ***map)
{
    static int num_sockets = -1, num_cores = -1;
    int socket_index, core_index, pu_index;
    hwloc_obj_t socket, core, pu;
    int **data;

    /* Find out how many sockets we have (cached so that we don't have
       to look this up every time) */
    if (num_sockets < 0) {
        num_sockets = hwloc_get_nbobjs_by_type(opal_hwloc_topology, HWLOC_OBJ_SOCKET);
        /* some systems (like the iMac) only have one
         * socket and so don't report a socket
         */
        if (0 == num_sockets) {
            num_sockets = 1;
        }
        /* Lazy: take the total number of cores that we have in the
           topology; that'll be more than the max number of cores
           under any given socket */
        num_cores = hwloc_get_nbobjs_by_type(opal_hwloc_topology, HWLOC_OBJ_CORE);
    }
    *num_sockets_arg = num_sockets;
    *num_cores_arg = num_cores;

    /* Alloc a 2D array: sockets x cores. */
    data = malloc(num_sockets * sizeof(int *));
    if (NULL == data) {
        return OPAL_ERR_OUT_OF_RESOURCE;
    }
    data[0] = calloc(num_sockets * num_cores, sizeof(int));
    if (NULL == data[0]) {
        free(data);
        return OPAL_ERR_OUT_OF_RESOURCE;
    }
    for (socket_index = 1; socket_index < num_sockets; ++socket_index) {
        data[socket_index] = data[socket_index - 1] + num_cores;
    }

    /* Iterate the PUs in this cpuset; fill in the data[][] array with
       the socket/core/pu triples */
    for (pu_index = 0,
             pu = hwloc_get_obj_inside_cpuset_by_type(opal_hwloc_topology,
                                                      cpuset, HWLOC_OBJ_PU, 
                                                      pu_index);
         NULL != pu;
         pu = hwloc_get_obj_inside_cpuset_by_type(opal_hwloc_topology,
                                                  cpuset, HWLOC_OBJ_PU, 
                                                  ++pu_index)) {
        /* Go upward and find the core this PU belongs to */
        core = pu;
        while (NULL != core && core->type != HWLOC_OBJ_CORE) {
            core = core->parent;
        }
        core_index = 0;
        if (NULL != core) {
            core_index = core->logical_index;
        }

        /* Go upward and find the socket this PU belongs to */
        socket = pu;
        while (NULL != socket && socket->type != HWLOC_OBJ_SOCKET) {
            socket = socket->parent;
        }
        socket_index = 0;
        if (NULL != socket) {
            socket_index = socket->logical_index;
        }

        /* Save this socket/core/pu combo.  LAZY: Assuming that we
           won't have more PU's per core than (sizeof(int)*8). */
        data[socket_index][core_index] |= (1 << pu->sibling_rank);
    }

    *map = data;
    return OPAL_SUCCESS;
}

/*
 * Make a prettyprint string for a hwloc_cpuset_t
 */
int opal_hwloc_base_cset2str(char *str, int len, hwloc_cpuset_t cpuset)
{
    bool first;
    int num_sockets, num_cores;
    int ret, socket_index, core_index;
    char tmp[BUFSIZ];
    const int stmp = sizeof(tmp) - 1;
    int **map;

    str[0] = tmp[stmp] = '\0';

    if (OPAL_SUCCESS != (ret = build_map(&num_sockets, &num_cores, cpuset, &map))) {
        return ret;
    }

    /* Iterate over the data matrix and build up the string */
    first = true;
    for (socket_index = 0; socket_index < num_sockets; ++socket_index) {
        for (core_index = 0; core_index < num_cores; ++core_index) {
            if (map[socket_index][core_index] > 0) {
                if (!first) {
                    strncat(str, ", ", len - strlen(str));
                }
                first = false;

                snprintf(tmp, stmp, "socket %d[core %d[hwt %s]]", 
                         socket_index, core_index,
                         bitmap2rangestr(map[socket_index][core_index]));
                strncat(str, tmp, len - strlen(str));
            }
        }
    }
    free(map[0]);
    free(map);

    return OPAL_SUCCESS;
}

/*
 * Make a prettyprint string for a cset in a map format.  
 * Example: [B./..]
 * Key:  [] - signifies socket
 *        / - divider between cores
 *        . - signifies PU a process not bound to
 *        B - signifies PU a process is bound to
 */
int opal_hwloc_base_cset2mapstr(char *str, int len, hwloc_cpuset_t cpuset)
{
    char tmp[BUFSIZ];
    int core_index, pu_index;
    const int stmp = sizeof(tmp) - 1;
    hwloc_obj_t socket, core, pu;

    str[0] = tmp[stmp] = '\0';

    /* Iterate over all existing sockets */
    for (socket = hwloc_get_obj_by_type(opal_hwloc_topology, 
                                        HWLOC_OBJ_SOCKET, 0);
         NULL != socket; 
         socket = socket->next_cousin) {
        strncat(str, "[", len - strlen(str));

        /* Iterate over all existing cores in this socket */
        core_index = 0;
        for (core = hwloc_get_obj_inside_cpuset_by_type(opal_hwloc_topology,
                                                        socket->cpuset, 
                                                        HWLOC_OBJ_CORE, core_index);
             NULL != core; 
             core = hwloc_get_obj_inside_cpuset_by_type(opal_hwloc_topology,
                                                        socket->cpuset, 
                                                        HWLOC_OBJ_CORE, ++core_index)) {
            if (core_index > 0) {
                strncat(str, "/", len - strlen(str));
            }

            /* Iterate over all existing PUs in this core */
            pu_index = 0;
            for (pu = hwloc_get_obj_inside_cpuset_by_type(opal_hwloc_topology,
                                                          core->cpuset, 
                                                          HWLOC_OBJ_PU, pu_index);
                 NULL != pu; 
                 pu = hwloc_get_obj_inside_cpuset_by_type(opal_hwloc_topology,
                                                          core->cpuset, 
                                                          HWLOC_OBJ_PU, ++pu_index)) {

                /* Is this PU in the cpuset? */
                if (hwloc_bitmap_isset(cpuset, pu->os_index)) {
                    strncat(str, "B", len - strlen(str));
                } else {
                    strncat(str, ".", len - strlen(str));
                }
            }
        }
        strncat(str, "]", len - strlen(str));
    }

    return OPAL_SUCCESS;
}

static int dist_cmp_fn (opal_list_item_t **a, opal_list_item_t **b)
{
    orte_rmaps_numa_node_t *aitem = *((orte_rmaps_numa_node_t **) a);
    orte_rmaps_numa_node_t *bitem = *((orte_rmaps_numa_node_t **) b);

    if (bitem->dist_from_closed > aitem->dist_from_closed) {
        return 1;
    } else if( aitem->dist_from_closed == bitem->dist_from_closed ) {
        return 0;
    } else {
        return -1;
    }
}

static void sort_by_dist(hwloc_topology_t topo, char* device_name, opal_list_t *sorted_list)
{
    hwloc_obj_t device_obj = NULL;
    hwloc_obj_t obj = NULL, root = NULL;
    const struct hwloc_distances_s* distances;
    orte_rmaps_numa_node_t *numa_node;
    int close_node_index;
    float latency;
    unsigned int j;
    unsigned int depth;
    unsigned i;

    for (device_obj = hwloc_get_obj_by_type(topo, HWLOC_OBJ_OS_DEVICE, 0); device_obj; device_obj = hwloc_get_next_osdev(topo, device_obj)) {
        if (device_obj->attr->osdev.type == HWLOC_OBJ_OSDEV_OPENFABRICS
                || device_obj->attr->osdev.type == HWLOC_OBJ_OSDEV_NETWORK) {
            if (!strcmp(device_obj->name, device_name)) {
                /* find numa node containing this device */
                obj = device_obj->parent;
                while ((obj != NULL) && (obj->type != HWLOC_OBJ_NODE)) {
                    obj = obj->parent;
                }
                if (obj == NULL) {
                    opal_output_verbose(5, opal_hwloc_base_framework.framework_output,
                            "hwloc:base:get_sorted_numa_list: NUMA node closest to %s wasn't found.",
                            device_name);
                    return;
                } else {
                    close_node_index = obj->logical_index;
                }

                /* find distance matrix for all numa nodes */
                distances = hwloc_get_whole_distance_matrix_by_type(topo, HWLOC_OBJ_NODE);
                if (NULL ==  distances) {
                    /* we can try to find distances under group object. This info can be there. */
                    depth = hwloc_get_type_depth(topo, HWLOC_OBJ_NODE);
                    if (depth < 0) {
                        opal_output_verbose(5, opal_hwloc_base_framework.framework_output,
                                "hwloc:base:get_sorted_numa_list: There is no information about distances on the node.");
                        return;
                    }
                    root = hwloc_get_root_obj(topo);
                    for (i = 0; i < root->arity; i++) {
                        obj = root->children[i];
                        if (obj->distances_count > 0) {
                            for(j = 0; j < obj->distances_count; j++) {
                                if (obj->distances[j]->relative_depth + 1 == depth) {
                                    distances = obj->distances[j];
                                    break;
                                }
                            }
                        }
                    }
                }
                /* find all distances for our close node with logical index = close_node_index as close_node_index + nbobjs*j */
                if ((NULL == distances) || (0 == distances->nbobjs)) {
                    opal_output_verbose(5, opal_hwloc_base_framework.framework_output,
                            "hwloc:base:get_sorted_numa_list: There is no information about distances on the node.");
                    return;
                }
                /* fill list of numa nodes */
                for (j = 0; j < distances->nbobjs; j++) {
                    latency = distances->latency[close_node_index + distances->nbobjs * j];
                    numa_node = OBJ_NEW(orte_rmaps_numa_node_t);
                    numa_node->index = j;
                    numa_node->dist_from_closed = latency;
                    opal_list_append(sorted_list, &numa_node->super);
                }
                /* sort numa nodes by distance from the closest one to PCI */
                opal_list_sort(sorted_list, dist_cmp_fn);
                return;
            }
        }
    }
}

static int find_devices(hwloc_topology_t topo, char* device_name) 
{
    hwloc_obj_t device_obj = NULL;
    int count = 0;
    for (device_obj = hwloc_get_obj_by_type(topo, HWLOC_OBJ_OS_DEVICE, 0); device_obj; device_obj = hwloc_get_next_osdev(topo, device_obj)) {
        if (device_obj->attr->osdev.type == HWLOC_OBJ_OSDEV_OPENFABRICS) {
            count++;
            free(device_name);
            device_name = strdup(device_obj->name);
        }
    }
    return count;
}

int opal_hwloc_get_sorted_numa_list(hwloc_topology_t topo, char* device_name, opal_list_t *sorted_list)
{
    hwloc_obj_t obj;
    opal_list_item_t *item;
    opal_hwloc_summary_t *sum;
    opal_hwloc_topo_data_t *data;
    orte_rmaps_numa_node_t *numa, *copy_numa;
    int count;

    obj = hwloc_get_root_obj(topo);

    /* first see if the topology already has this info */
    /* we call opal_hwloc_base_get_nbobjs_by_type() before it to fill summary object so it should exist*/
    data = (opal_hwloc_topo_data_t*)obj->userdata;
    if (NULL != data) {
        for (item = opal_list_get_first(&data->summaries);
                item != opal_list_get_end(&data->summaries);
                item = opal_list_get_next(item)) {
            sum = (opal_hwloc_summary_t*)item;
            if (HWLOC_OBJ_NODE == sum->type) {
                if (opal_list_get_size(&sum->sorted_by_dist_list) > 0) { 
                    OPAL_LIST_FOREACH(numa, &(sum->sorted_by_dist_list), orte_rmaps_numa_node_t) {
                        copy_numa = OBJ_NEW(orte_rmaps_numa_node_t);
                        copy_numa->index = numa->index;
                        copy_numa->dist_from_closed = numa->dist_from_closed;
                        opal_list_append(sorted_list, &copy_numa->super);
                    }
                    return 0;
                }else {
                    /* don't already know it - go get it */
                    /* firstly we check if we need to autodetect OpenFabrics  devices or we have the specified one */
                    if (!strcmp(device_name, "auto")) {
                        count = find_devices(topo, device_name);
                       if (count > 1) {
                           return count;
                       }
                    }
                    if (!device_name || (strlen(device_name) == 0)) {
                        return 1;
                    }
                    sort_by_dist(topo, device_name, sorted_list);
                    /* store this info in summary object for later usage */
                    OPAL_LIST_FOREACH(numa, sorted_list, orte_rmaps_numa_node_t) {
                        copy_numa = OBJ_NEW(orte_rmaps_numa_node_t);
                        copy_numa->index = numa->index;
                        copy_numa->dist_from_closed = numa->dist_from_closed;
                        opal_list_append(&(sum->sorted_by_dist_list), &copy_numa->super);
                    }
                    return 0;
                }
            }
        }
    }
}
