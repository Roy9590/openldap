/*
 * Copyright 1998,1999 The OpenLDAP Foundation, Redwood City, California, USA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted only
 * as authorized by the OpenLDAP Public License.  A copy of this
 * license is available at http://www.OpenLDAP.org/license.html or
 * in file LICENSE in the top-level directory of the distribution.
 */

/* thr_stub.c - stubs for the threads */

#include "portable.h"
#include "ldap_pvt_thread.h"

#if defined( NO_THREADS )

/***********************************************************************
 *                                                                     *
 * no threads package defined for this system - fake ok returns from   *
 * all threads routines (making it single-threaded).                   *
 *                                                                     *
 ***********************************************************************/

int
ldap_pvt_thread_initialize( void )
{
	return 0;
}

static void* ldap_int_status = NULL;

int 
ldap_pvt_thread_create( ldap_pvt_thread_t * thread, 
	int detach,
	void *(*start_routine)(void *),
	void *arg)
{
	if( ! detach ) ldap_int_status = NULL;
	start_routine( arg );
	return 0;
}

void 
ldap_pvt_thread_exit( void *retval )
{
	if( retval != NULL ) {
		ldap_int_status = retval;
	}
	return;
}

int 
ldap_pvt_thread_join( ldap_pvt_thread_t thread, void **status )
{
	if(status != NULL) *status = ldap_int_status;
	return 0;
}

int 
ldap_pvt_thread_kill( ldap_pvt_thread_t thread, int signo )
{
	return 0;
}

int 
ldap_pvt_thread_yield( void )
{
	return 0;
}

int 
ldap_pvt_thread_cond_init( ldap_pvt_thread_cond_t *cond )
{
	return 0;
}

int 
ldap_pvt_thread_cond_destroy( ldap_pvt_thread_cond_t *cond )
{
	return 0;
}

int 
ldap_pvt_thread_cond_signal( ldap_pvt_thread_cond_t *cond )
{
	return 0;
}

int 
ldap_pvt_thread_cond_broadcast( ldap_pvt_thread_cond_t *cond )
{
	return 0;
}

int 
ldap_pvt_thread_cond_wait( ldap_pvt_thread_cond_t *cond,
			  ldap_pvt_thread_mutex_t *mutex )
{
	return 0;
}

int 
ldap_pvt_thread_mutex_init( ldap_pvt_thread_mutex_t *mutex )
{
	return 0;
}

int 
ldap_pvt_thread_mutex_destroy( ldap_pvt_thread_mutex_t *mutex )
{
	return 0;
}

int 
ldap_pvt_thread_mutex_lock( ldap_pvt_thread_mutex_t *mutex )
{
	return 0;
}

int 
ldap_pvt_thread_mutex_unlock( ldap_pvt_thread_mutex_t *mutex )
{
	return 0;
}

#endif /* NO_THREADS */
