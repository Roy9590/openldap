/*
 * Copyright 1998,1999 The OpenLDAP Foundation, Redwood City, California, USA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted only
 * as authorized by the OpenLDAP Public License.  A copy of this
 * license is available at http://www.OpenLDAP.org/license.html or
 * in file LICENSE in the top-level directory of the distribution.
 */

#ifndef _LTHREAD_RDWR_H
#define _LTHREAD_RDWR_H 1

/********************************************************
 * An example source module to accompany...
 *
 * "Using POSIX Threads: Programming with Pthreads"
 *     by Brad nichols, Dick Buttlar, Jackie Farrell
 *     O'Reilly & Associates, Inc.
 *
 ********************************************************
 * 
 * Include file for reader/writer locks
 * 
 */

#include <ldap_cdefs.h>

LDAP_BEGIN_DECL

typedef struct rdwr_var {
	int readers_reading;
	int writer_writing;
	pthread_mutex_t mutex;
	pthread_cond_t lock_free;
} pthread_rdwr_t;

typedef void * pthread_rdwrattr_t;

#define pthread_rdwrattr_default NULL;

LDAP_F int pthread_rdwr_init_np LDAP_P((pthread_rdwr_t *rdwrp, pthread_rdwrattr_t *attrp));
LDAP_F int pthread_rdwr_rlock_np LDAP_P((pthread_rdwr_t *rdwrp));
LDAP_F int pthread_rdwr_runlock_np LDAP_P((pthread_rdwr_t *rdwrp));
LDAP_F int pthread_rdwr_wlock_np LDAP_P((pthread_rdwr_t *rdwrp));
LDAP_F int pthread_rdwr_wunlock_np LDAP_P((pthread_rdwr_t *rdwrp));

#ifdef LDAP_DEBUG
LDAP_F int pthread_rdwr_rchk_np LDAP_P((pthread_rdwr_t *rdwrp));
LDAP_F int pthread_rdwr_wchk_np LDAP_P((pthread_rdwr_t *rdwrp));
LDAP_F int pthread_rdwr_rwchk_np LDAP_P((pthread_rdwr_t *rdwrp));
#endif /* LDAP_DEBUG */

LDAP_END_DECL

#endif /* _LTHREAD_RDWR_H */
