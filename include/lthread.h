/* lthread.h - ldap threads header file */

#ifndef _LTHREAD_H
#define _LTHREAD_H

#include "portable.h"

#if defined( HAVE_PTHREADS )
/**********************************
 *                                *
 * definitions for POSIX Threads  *
 *                                *
 **********************************/

#include <pthread.h>
#ifdef HAVE_SCHED_H
#include <sched.h>
#endif

LDAP_BEGIN_DECL

#if !defined( HAVE_PTHREAD_ATTR_INIT ) && \
	defined( HAVE_PTHREAD_ATTR_CREATE )
#define pthread_attr_init( a )		pthread_attr_create( a )
#endif

#if !defined( HAVE_PTHREAD_ATTR_DESTROY ) && \
	defined( HAVE_PTHREAD_ATTR_DELETE )
#define pthread_attr_destroy( a )	pthread_attr_delete( a )
#endif

#if !defined( HAVE_PTHREAD_ATTR_SETDETACHSTATE ) && \
	defined( HAVE_PTHREAD_ATTR_SETDETACH_NP )
#define pthread_attr_setdetachstate( a, b ) \
					pthread_attr_setdetach_np( a, b )
#endif

#ifndef HAVE_PTHREAD_KILL
/* missing pthread_kill(), define prototype */
LDAP_F void pthread_kill LDAP_P(( pthread_t tid, int sig ));
#endif

#ifndef HAVE_PTHREADS_D4
#define pthread_mutexattr_default	NULL
#define pthread_condattr_default	NULL

#ifdef HAVE_SCHED_YIELD
#define pthread_yield sched_yield
#endif
#endif

LDAP_END_DECL

#elif defined ( HAVE_MACH_CTHREADS )
/**********************************
 *                                *
 * definitions for Mach CThreads  *
 *                                *
 **********************************/

#include <mach/cthreads.h>

LDAP_BEGIN_DECL

typedef cthread_fn_t	VFP;
typedef int		pthread_attr_t;
typedef cthread_t	pthread_t;

/* default attr states */
#define pthread_mutexattr_default	NULL
#define pthread_condattr_default	NULL

/* thread state - joinable or not */
#define PTHREAD_CREATE_JOINABLE	0
#define PTHREAD_CREATE_DETACHED	1
/* thread scope - who is in scheduling pool */
#define PTHREAD_SCOPE_PROCESS	0
#define PTHREAD_SCOPE_SYSTEM	1

/* mutex attributes and mutex type */
typedef int	pthread_mutexattr_t;
typedef struct mutex pthread_mutex_t;

/* mutex and condition variable scope - process or system */
#define PTHREAD_SHARE_PRIVATE	0
#define PTHREAD_SHARE_PROCESS	1

/* condition variable attributes and condition variable type */
typedef int	pthread_condattr_t;
typedef struct condition pthread_cond_t;

LDAP_END_DECL

#elif defined( HAVE_THR )
/**************************************
 *                                    *
 * thread definitions for Solaris LWP *
 *                                    *
 **************************************/

#include <thread.h>
#include <synch.h>

LDAP_BEGIN_DECL

typedef void	*(*VFP)();

/* default attr states */
#define pthread_mutexattr_default	NULL
#define pthread_condattr_default	NULL

/* thread state - joinable or not */
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED THR_DETACHED
/* thread scope - who is in scheduling pool */
#define PTHREAD_SCOPE_PROCESS   0
#define PTHREAD_SCOPE_SYSTEM    THR_BOUND
/* mutex and condition variable scope - process or system */
#define PTHREAD_SHARE_PRIVATE   USYNC_THREAD
#define PTHREAD_SHARE_PROCESS   USYNC_PROCESS

/* thread attributes and thread type */
typedef int		pthread_attr_t;
typedef thread_t	pthread_t;

/* mutex attributes and mutex type */
typedef int	pthread_mutexattr_t;
typedef mutex_t	pthread_mutex_t;

/* condition variable attributes and condition variable type */
typedef int     pthread_condattr_t;
typedef cond_t	pthread_cond_t;

LDAP_END_DECL

#elif defined( HAVE_LWP )
/*************************************
 *                                   *
 * thread definitions for SunOS LWP  *
 *                                   *
 *************************************/

#include <lwp/lwp.h>
#include <lwp/stackdep.h>

LDAP_BEGIN_DECL

stkalign_t *get_stack( int *stacknop );
void free_stack( int *stackno );

typedef void	*(*VFP)();

/* thread attributes and thread type */
typedef int		pthread_attr_t;
typedef thread_t	pthread_t;

/* default attr states */
#define pthread_mutexattr_default	NULL
#define pthread_condattr_default	NULL

/* thread state - joinable or not */
#define PTHREAD_CREATE_JOINABLE	0
#define PTHREAD_CREATE_DETACHED	1
/* thread scope - who is in scheduling pool */
#define PTHREAD_SCOPE_PROCESS	0
#define PTHREAD_SCOPE_SYSTEM	1

/* mutex attributes and mutex type */
typedef int	pthread_mutexattr_t;
typedef mon_t	pthread_mutex_t;

/* mutex and condition variable scope - process or system */
#define PTHREAD_SHARE_PRIVATE	0
#define PTHREAD_SHARE_PROCESS	1

/* condition variable attributes and condition variable type */
typedef int	pthread_condattr_t;
typedef struct lwpcv {
	int		lcv_created;
	cv_t		lcv_cv;
} pthread_cond_t;

LDAP_END_DECL

#elif HAVE_NT_THREADS

#include <windows.h>
#include <process.h>

typedef void	(*VFP)(void*);

/* thread attributes and thread type */
typedef int		pthread_attr_t;
typedef HANDLE	pthread_t;

/* default attr states */
#define pthread_mutexattr_default	NULL
#define pthread_condattr_default	NULL

/* thread state - joinable or not */
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 0
/* thread scope - who is in scheduling pool */
#define PTHREAD_SCOPE_PROCESS   0
#define PTHREAD_SCOPE_SYSTEM    0

/* mutex attributes and mutex type */
typedef int    pthread_mutexattr_t;
typedef HANDLE pthread_mutex_t;

/* mutex and condition variable scope - process or system */
#define PTHREAD_SHARE_PRIVATE   USYNC_THREAD
#define PTHREAD_SHARE_PROCESS   USYNC_PROCESS

/* condition variable attributes and condition variable type */
typedef int     pthread_condattr_t;
typedef HANDLE  pthread_cond_t;
typedef int     any_t;


#else

/***********************************
 *                                 *
 * thread definitions for no       *
 * underlying library support      *
 *                                 *
 ***********************************/

LDAP_BEGIN_DECL

#ifndef NO_THREADS
#define NO_THREADS 1
#endif

typedef void	*(*VFP)();

/* thread attributes and thread type */
typedef int	pthread_attr_t;
typedef int	pthread_t;

/* default attr states */
#define pthread_mutexattr_default	NULL
#define pthread_condattr_default	NULL

/* thread state - joinable or not */
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 0
/* thread scope - who is in scheduling pool */
#define PTHREAD_SCOPE_PROCESS   0
#define PTHREAD_SCOPE_SYSTEM    0

/* mutex attributes and mutex type */
typedef int	pthread_mutexattr_t;
typedef int	pthread_mutex_t;

/* mutex and condition variable scope - process or system */
#define PTHREAD_SHARE_PRIVATE   0
#define PTHREAD_SHARE_PROCESS   0

/* condition variable attributes and condition variable type */
typedef int     pthread_condattr_t;
typedef int	pthread_cond_t;

LDAP_END_DECL

#endif /* no threads support */
#endif /* _LTHREAD_H */
