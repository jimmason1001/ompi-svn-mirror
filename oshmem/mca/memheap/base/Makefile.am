# Copyright (c) 2013      Mellanox Technologies, Inc.
#                         All rights reserved.
# $COPYRIGHT$
# 
# Additional copyrights may follow
# 
# $HEADER$
#

AM_CPPFLAGS = $(oshmem_verbs_CPPFLAGS)

dist_ompidata_DATA += base/help-shmem-mca.txt 

headers += \
        base/base.h 

libmca_memheap_la_SOURCES += \
	base/memheap_base_frame.c \
	base/memheap_base_select.c \
	base/memheap_base_alloc.c \
	base/memheap_base_static.c \
	base/memheap_base_register.c \
	base/memheap_base_mkey.c

libmca_memheap_la_LDFLAGS += -module -avoid-version $(oshmem_verbs_LDFLAGS) 
libmca_memheap_la_LIBADD  += $(oshmem_verbs_LIBS)
