/*
 * Copyright (c) 1996 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */


/* 
 * main.c - main routine for slurpd.
 */

#include "portable.h"

#include <stdio.h>

#include "slurp.h"
#include "globals.h"
#include "lutil.h"


extern int		doargs( int, char **, Globals * );
extern void		fm();
extern int		start_replica_thread( Ri * );
extern Globals		*init_globals();
extern int		sanity();

#if defined( HAVE_LWP )
extern void		start_lwp_scheduler();
#endif /* HAVE_LWP */

main(
    int		argc,
    char	**argv
)
{
    pthread_attr_t	attr;
    int			status;
    int			i;

#ifdef NO_THREADS
    /* Haven't yet written the non-threaded version */
    fprintf( stderr, "slurpd currently requires threads support\n" );
    exit( 1 );
#else

    /* 
     * Create and initialize globals.  init_globals() also initializes
     * the main replication queue.
     */
    if (( sglob = init_globals()) == NULL ) {
	fprintf( stderr, "Out of memory initializing globals\n" );
	exit( 1 );
    }

    /*
     * Process command-line args and fill in globals.
     */
    if ( doargs( argc, argv, sglob ) < 0 ) {
	exit( 1 );
    }

    /*
     * Read slapd config file and initialize Re (per-replica) structs.
     */
    if ( slurpd_read_config( sglob->slapd_configfile ) < 0 ) {
	fprintf( stderr,
		"Errors encountered while processing config file \"%s\"\n",
		sglob->slapd_configfile );
	exit( 1 );
    }

    /*
     * Get any saved state information off the disk.
     */
    if ( sglob->st->st_read( sglob->st )) {
	fprintf( stderr, "Malformed slurpd status file \"%s\"\n",
		sglob->slurpd_status_file, 0, 0 );
	exit( 1 );
    }

    /*
     * All readonly data should now be initialized. 
     * Check for any fatal error conditions before we get started
     */
     if ( sanity() < 0 ) {
	exit( 1 );
    }

    /*
     * Detach from the controlling terminal, if debug level = 0,
     * and if not in one-shot mode.
     */
#ifdef LDAP_DEBUG
    if (( ldap_debug == 0 )  && !sglob->one_shot_mode )
#else /* LDAP_DEBUG */
    if ( !sglob->one_shot_mode )
#endif /* LDAP_DEBUG */
	lutil_detach( 0, 0 );

#if defined( HAVE_LWP )
    /*
     * Need to start a scheduler thread under SunOS 4
     */
    start_lwp_scheduler();
#endif /* HAVE_LWP */


    /*
     * Start threads - one thread for each replica
     */
    for ( i = 0; sglob->replicas[ i ] != NULL; i++ ) {
	start_replica_thread( sglob->replicas[ i ]);
    }

    /*
     * Start the main file manager thread (in fm.c).
     */
    pthread_attr_init( &attr );

#if !defined(HAVE_PTHREADS_D4) && !defined(HAVE_DCE)
    /* POSIX_THREADS or compatible
     * This is a draft 10 or standard pthreads implementation
     */
    if ( pthread_create( &(sglob->fm_tid), &attr, fm, (void *) NULL )
	    != 0 ) {
	Debug( LDAP_DEBUG_ANY, "file manager pthread_create failed\n",
		0, 0, 0 );
	exit( 1 );

    }
#else /* !PTHREADS_FINAL */
    /*
     * This is a draft 4 or earlier pthreads implementation
     */
    if ( pthread_create( &(sglob->fm_tid), attr, fm, (void *) NULL )
	    != 0 ) {
	Debug( LDAP_DEBUG_ANY, "file manager pthread_create failed\n",
		0, 0, 0 );
	exit( 1 );

    }
#endif /* !PTHREADS_FINAL */

    pthread_attr_destroy( &attr );

    /*
     * Wait for the fm thread to finish.
     */
#ifdef HAVE_PTHREADS_FINAL
    pthread_join( sglob->fm_tid, (void *) NULL );
#else
    pthread_join( sglob->fm_tid, (void *) &status );
#endif
    /*
     * Wait for the replica threads to finish.
     */
    for ( i = 0; sglob->replicas[ i ] != NULL; i++ ) {
#ifdef HAVE_PTHREADS_FINAL
	pthread_join( sglob->replicas[ i ]->ri_tid, (void *) NULL );
#else
	pthread_join( sglob->replicas[ i ]->ri_tid, (void *) &status );
#endif
    }
    Debug( LDAP_DEBUG_ANY, "slurpd: terminating normally\n", 0, 0, 0 );
    sglob->slurpd_shutdown = 1;
    pthread_exit( 0 );

#endif /* !NO_THREADS */
}
