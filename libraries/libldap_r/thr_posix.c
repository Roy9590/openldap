/*
 * Copyright 1998,1999 The OpenLDAP Foundation, Redwood City, California, USA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted only
 * as authorized by the OpenLDAP Public License.  A copy of this
 * license is available at http://www.OpenLDAP.org/license.html or
 * in file LICENSE in the top-level directory of the distribution.
 */

/* thr_posix.c - wrapper around posix and posixish thread implementations.
 */

#include "portable.h"
#include "ldap_pvt_thread.h"

#if defined( HAVE_PTHREADS )

int
ldap_pvt_thread_initialize( void )
{
#if defined( LDAP_THREAD_CONCURRENCY ) && HAVE_PTHREAD_SETCONCURRENCY
	ldap_pvt_thread_set_concurrency( LDAP_THREAD_CONCURRENCY );
#endif
	return 0;
}

#ifdef HAVE_PTHREAD_SETCONCURRENCY
int
ldap_pvt_thread_set_concurrency(int n)
{
#ifdef HAVE_PTHREAD_SETCONCURRENCY
	return pthread_setconcurrency( n );
#elif HAVE_THR_SETCONCURRENCY
	return pthread_setconcurrency( n );
#else
	return 0;
#endif
}
#endif

#ifdef HAVE_PTHREAD_GETCONCURRENCY
int
ldap_pvt_thread_get_concurrency(void)
{
#ifdef HAVE_PTHREAD_GETCONCURRENCY
	return pthread_getconcurrency();
#elif HAVE_THR_GETCONCURRENCY
	return pthread_getconcurrency();
#else
	return 0;
#endif
}
#endif

int 
ldap_pvt_thread_create( ldap_pvt_thread_t * thread,
	int detach,
	void *(*start_routine)( void * ),
	void *arg)
{
	int rtn = pthread_create( thread, NULL, start_routine, arg );

	if( detach ) {
#ifdef HAVE_PTHREADS_FINAL
		pthread_detach( *thread );
#else
		pthread_detach( thread );
#endif
	}
	return rtn;
}

void 
ldap_pvt_thread_exit( void *retval )
{
	pthread_exit( retval );
}

int 
ldap_pvt_thread_join( ldap_pvt_thread_t thread, void **thread_return )
{
#if !defined( HAVE_PTHREADS_FINAL )
	void *dummy;
	if (thread_return==NULL)
	  thread_return=&dummy;
#endif	
	return pthread_join( thread, thread_return );
}

int 
ldap_pvt_thread_kill( ldap_pvt_thread_t thread, int signo )
{
#ifdef HAVE_PTHREAD_KILL
	return pthread_kill( thread, signo );
#else
	/* pthread package with DCE */
	if (kill( getpid(), sig )<0)
		return errno;
	return 0;
#endif
}

int 
ldap_pvt_thread_yield( void )
{
#ifdef HAVE_SCHED_YIELD
	return sched_yield();
#elif HAVE_PTHREAD_YIELD
	return pthread_yield();
#elif HAVE_THR_YIELD
	return thr_yield();
#else
	return 0;
#endif   
}

int 
ldap_pvt_thread_cond_init( ldap_pvt_thread_cond_t *cond )
{
	return pthread_cond_init( cond, NULL );
}

int 
ldap_pvt_thread_cond_destroy( ldap_pvt_thread_cond_t *cond )
{
	return pthread_cond_destroy( cond );
}
	
int 
ldap_pvt_thread_cond_signal( ldap_pvt_thread_cond_t *cond )
{
	return pthread_cond_signal( cond );
}

int
ldap_pvt_thread_cond_broadcast( ldap_pvt_thread_cond_t *cond )
{
	return pthread_cond_broadcast( cond );
}

int 
ldap_pvt_thread_cond_wait( ldap_pvt_thread_cond_t *cond, 
		      ldap_pvt_thread_mutex_t *mutex )
{
	return pthread_cond_wait( cond, mutex );
}

int 
ldap_pvt_thread_mutex_init( ldap_pvt_thread_mutex_t *mutex )
{
	return pthread_mutex_init( mutex, NULL );
}

int 
ldap_pvt_thread_mutex_destroy( ldap_pvt_thread_mutex_t *mutex )
{
	return pthread_mutex_destroy( mutex );
}

int 
ldap_pvt_thread_mutex_lock( ldap_pvt_thread_mutex_t *mutex )
{
	return pthread_mutex_lock( mutex );
}

int 
ldap_pvt_thread_mutex_unlock( ldap_pvt_thread_mutex_t *mutex )
{
	return pthread_mutex_unlock( mutex );
}

#endif /* HAVE_PTHREADS */

