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
 * args.c - process command-line arguments, and set appropriate globals.
 */

#include "portable.h"

#include <stdio.h>
#include <stdlib.h>

#include <ac/string.h>
#include <ac/time.h>
#include <ac/unistd.h>

#include <lber.h>
#include <ldap.h>

#include "slurp.h"
#include "globals.h"


static void
usage( char *name )
{
    fprintf( stderr, "usage: %s\t[-d debug-level] [-s syslog-level]\n", name );
    fprintf( stderr, "\t\t[-f slapd-config-file] [-r replication-log-file]\n" );
#ifdef HAVE_KERBEROS
    fprintf( stderr, "\t\t[-t tmp-dir] [-o] [-k srvtab-file]\n" );
#else /* HAVE_KERBEROS */
    fprintf( stderr, "\t\t[-t tmp-dir] [-o]\n" );
#endif /* HAVE_KERBEROS */
}



/*
 * Interpret argv, and fill in any appropriate globals.
 */
int
doargs(
    int		argc,
    char	**argv,
    Globals	*g
)
{
    int		i;
    int		rflag = 0;

    if ( (g->myname = strrchr( argv[0], '/' )) == NULL ) {
	g->myname = strdup( argv[0] );
    } else {
	g->myname = strdup( g->myname + 1 );
    }

    while ( (i = getopt( argc, argv, "hd:f:r:t:k:o" )) != EOF ) {
	switch ( i ) {
	case 'd':	/* turn on debugging */
#ifdef LDAP_DEBUG
	    if ( optarg[0] == '?' ) {
		printf( "Debug levels:\n" );
		printf( "\tLDAP_DEBUG_TRACE\t%d\n",
			LDAP_DEBUG_TRACE );
		printf( "\tLDAP_DEBUG_PACKETS\t%d\n",
			LDAP_DEBUG_PACKETS );
		printf( "\tLDAP_DEBUG_ARGS\t\t%d\n",
			LDAP_DEBUG_ARGS );
		printf( "\tLDAP_DEBUG_CONNS\t%d\n",
			LDAP_DEBUG_CONNS );
		printf( "\tLDAP_DEBUG_BER\t\t%d\n",
			LDAP_DEBUG_BER );
		printf( "\tLDAP_DEBUG_FILTER\t%d\n",
			LDAP_DEBUG_FILTER );
		printf( "\tLDAP_DEBUG_CONFIG\t%d\n",
			LDAP_DEBUG_CONFIG );
		printf( "\tLDAP_DEBUG_ACL\t\t%d\n",
			LDAP_DEBUG_ACL );
		printf( "\tLDAP_DEBUG_ANY\t\t%d\n",
			LDAP_DEBUG_ANY );
		return( -1 );
	    } else {
		ldap_debug |= atoi( optarg );
	    }
#else /* LDAP_DEBUG */
		/* can't enable debugging - not built with debug code */
	    fprintf( stderr, "must compile with LDAP_DEBUG for debugging\n" );
#endif /* LDAP_DEBUG */
	    break;
	case 'f':	/* slapd config file */
	    g->slapd_configfile = strdup( optarg );
	    break;
	case 'r':	/* slapd replog file */
	    strcpy( g->slapd_replogfile, optarg );
	    rflag++;
	    break;
	case 't':	/* dir to use for our copies of replogs */
	    g->slurpd_rdir = strdup( optarg );
	    break;
	case 'k':	/* name of kerberos srvtab file */
#ifdef HAVE_KERBEROS
	    g->default_srvtab = strdup( optarg );
#else /* HAVE_KERBEROS */
	    fprintf( stderr, "must compile with KERBEROS to use -k option\n" );
#endif /* HAVE_KERBEROS */
	    break;
	case 'h':
	    usage( g->myname );
	    return( -1 );
	case 'o':
	    g->one_shot_mode = 1;
	    break;
	default:
	    usage( g->myname );
	    return( -1 );
	}
    }

    if ( g->one_shot_mode && !rflag ) {
	fprintf( stderr, "If -o flag is given, -r flag must also be given.\n" );
	usage( g->myname );
	return( -1 );
    }

    /* Set location/name of our private copy of the slapd replog file */
    sprintf( g->slurpd_replogfile, "%s/%s", g->slurpd_rdir,
	    DEFAULT_SLURPD_REPLOGFILE );

    /* Set location/name of the slurpd status file */
    sprintf( g->slurpd_status_file, "%s/%s", g->slurpd_rdir,
	    DEFAULT_SLURPD_STATUS_FILE );

	ber_set_option(NULL, LBER_OPT_DEBUG_LEVEL, &ldap_debug);
	ldap_set_option(NULL, LDAP_OPT_DEBUG_LEVEL, &ldap_debug);
	ldif_debug = ldap_debug;

#ifdef LOG_LOCAL4
    openlog( g->myname, OPENLOG_OPTIONS, LOG_LOCAL4 );
#else
    openlog( g->myname, OPENLOG_OPTIONS );
#endif

    return 0;

}


