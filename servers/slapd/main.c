#include "portable.h"

#include <stdio.h>

#include <ac/signal.h>
#include <ac/socket.h>
#include <ac/string.h>
#include <ac/time.h>
#include <ac/unistd.h>

#include "ldapconfig.h"
#include "slap.h"
#include "lutil.h"			/* Get lutil_detach() */

/*
 * when more than one slapd is running on one machine, each one might have
 * it's own LOCAL for syslogging and must have its own pid/args files
 */

#ifdef LOG_LOCAL4

#define DEFAULT_SYSLOG_USER  LOG_LOCAL4

typedef struct _str2intDispatch {
	char    *stringVal;
	int      abbr;
	int      intVal;
} STRDISP, *STRDISP_P;


/* table to compute syslog-options to integer */
static STRDISP  syslog_types[] = {
    { "LOCAL0",         6, LOG_LOCAL0 },
    { "LOCAL1",         6, LOG_LOCAL1 },
    { "LOCAL2",         6, LOG_LOCAL2 },
    { "LOCAL3",         6, LOG_LOCAL3 },
    { "LOCAL4",         6, LOG_LOCAL4 },
    { "LOCAL5",         6, LOG_LOCAL5 },
    { "LOCAL6",         6, LOG_LOCAL6 },
    { "LOCAL7",         6, LOG_LOCAL7 },
    NULL
};

static int   cnvt_str2int();

#endif  /* LOG_LOCAL4 */


static void
usage( char *name )
{
	fprintf( stderr, "usage: %s [-d ?|debuglevel] [-f configfile] [-p portnumber] [-s sysloglevel]", name );
#ifdef LOG_LOCAL4
    fprintf( stderr, " [-l sysloguser]" );
#endif
    fprintf( stderr, "\n" );
}

int
main( int argc, char **argv )
{
	int		i;
	int		inetd = 0;
	int		rc = 0;
	int		port;
	int		udp;
#ifdef LOG_LOCAL4
    int     syslogUser = DEFAULT_SYSLOG_USER;
#endif
	char		*configfile;
	char        *serverName;
	int         serverMode = SLAP_SERVER_MODE;

	configfile = SLAPD_DEFAULT_CONFIGFILE;
	port = LDAP_PORT;
	g_argc = argc;
	g_argv = argv;

#ifdef SLAPD_BDB2
	while ( (i = getopt( argc, argv, "d:f:ip:s:ut" )) != EOF ) {
#else
	while ( (i = getopt( argc, argv, "d:f:ip:s:u" )) != EOF ) {
#endif
		switch ( i ) {
#ifdef LDAP_DEBUG
		case 'd':	/* turn on debugging */
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
				printf( "\tLDAP_DEBUG_STATS\t%d\n",
				    LDAP_DEBUG_STATS );
				printf( "\tLDAP_DEBUG_STATS2\t%d\n",
				    LDAP_DEBUG_STATS2 );
				printf( "\tLDAP_DEBUG_SHELL\t%d\n",
				    LDAP_DEBUG_SHELL );
				printf( "\tLDAP_DEBUG_PARSE\t%d\n",
				    LDAP_DEBUG_PARSE );
				printf( "\tLDAP_DEBUG_ANY\t\t%d\n",
				    LDAP_DEBUG_ANY );
				exit( 0 );
			} else {
				slap_debug |= atoi( optarg );
			}
			break;
#else
		case 'd':	/* turn on debugging */
			fprintf( stderr,
			    "must compile with LDAP_DEBUG for debugging\n" );
			break;
#endif

		case 'f':	/* read config file */
			configfile = ch_strdup( optarg );
			break;

		case 'i':	/* run from inetd */
			inetd = 1;
			break;

		case 'p':	/* port on which to listen */
			port = atoi( optarg );
			break;

		case 's':	/* set syslog level */
			ldap_syslog = atoi( optarg );
			break;

#ifdef LOG_LOCAL4

		case 'l':	/* set syslog local user */
			syslogUser = cnvt_str2int( optarg, syslog_types,
                                           DEFAULT_SYSLOG_USER );
			break;

#endif

		case 'u':	/* do udp */
			udp = 1;
			break;

#ifdef SLAPD_BDB2
		case 't':  /* timed server */
			serverMode = SLAP_TIMEDSERVER_MODE;
			break;
#endif

		default:
			usage( argv[0] );
			exit( 1 );
		}
	}

	lber_set_option(NULL, LBER_OPT_DEBUG_LEVEL, &slap_debug);
	ldap_set_option(NULL, LDAP_OPT_DEBUG_LEVEL, &slap_debug);
	ldif_debug = slap_debug;

	Debug( LDAP_DEBUG_TRACE, "%s", Versionstr, 0, 0 );

	if ( (serverName = strrchr( argv[0], '/' )) == NULL ) {
		serverName = ch_strdup( argv[0] );
	} else {
		serverName = ch_strdup( serverName + 1 );
	}

#ifdef LOG_LOCAL4
	openlog( serverName, OPENLOG_OPTIONS, syslogUser );
#else
	openlog( serverName, OPENLOG_OPTIONS );
#endif

	if ( slap_init( serverMode, serverName ) != 0 ) {
		rc = 1;
		goto destroy;
	}

	if ( read_config( configfile ) != 0 ) {
		rc = 1;
		goto destroy;
	}

	if ( slap_startup(-1)  != 0 ) {
		rc = 1;
		goto shutdown;
	}

	if ( ! inetd ) {
		int		status;

		(void) SIGNAL( SIGPIPE, SIG_IGN );
		(void) SIGNAL( LDAP_SIGUSR1, slap_do_nothing );
		(void) SIGNAL( LDAP_SIGUSR2, slap_set_shutdown );
		(void) SIGNAL( SIGTERM, slap_set_shutdown );
		(void) SIGNAL( SIGINT, slap_set_shutdown );
		(void) SIGNAL( SIGHUP, slap_set_shutdown );

#ifdef LDAP_DEBUG
		lutil_detach( ldap_debug, 0 );
#else
		lutil_detach( 0, 0 );
#endif

		time( &starttime );

		if ( status = ldap_pvt_thread_create( &listener_tid, 0,
			slapd_daemon, (void *) port ) != 0 )
		{
			Debug( LDAP_DEBUG_ANY,
			    "listener ldap_pvt_thread_create failed (%d)\n", status, 0, 0 );

			rc = 1;

		} else {
			/* wait for the listener thread to complete */
			ldap_pvt_thread_join( listener_tid, (void *) NULL );
		}

	} else {
		Connection		c;
		Operation		*o;
		BerElement		ber;
		unsigned long		len, tag;
		long			msgid;
		int			flen;
		struct sockaddr_in	from;
		struct hostent		*hp;

		c.c_dn = NULL;
		c.c_cdn = NULL;
		c.c_ops = NULL;
		c.c_sb.sb_sd = 0;
		c.c_sb.sb_options = 0;
		c.c_sb.sb_naddr = udp ? 1 : 0;
		c.c_sb.sb_ber.ber_buf = NULL;
		c.c_sb.sb_ber.ber_ptr = NULL;
		c.c_sb.sb_ber.ber_end = NULL;
		ldap_pvt_thread_mutex_init( &c.c_dnmutex );
		ldap_pvt_thread_mutex_init( &c.c_opsmutex );
		ldap_pvt_thread_mutex_init( &c.c_pdumutex );
#ifdef notdefcldap
		c.c_sb.sb_addrs = (void **) saddrlist;
		c.c_sb.sb_fromaddr = &faddr;
		c.c_sb.sb_useaddr = saddrlist[ 0 ] = &saddr;
#endif
		flen = sizeof(from);
		if ( getpeername( 0, (struct sockaddr *) &from, &flen ) == 0 ) {
#ifdef SLAPD_RLOOKUPS
			hp = gethostbyaddr( (char *) &(from.sin_addr.s_addr),
			    sizeof(from.sin_addr.s_addr), AF_INET );
#else
			hp = NULL;
#endif

			Debug( LDAP_DEBUG_ARGS, "connection from %s (%s)\n",
			    hp == NULL ? "unknown" : hp->h_name,
			    inet_ntoa( from.sin_addr ), 0 );

			c.c_addr = inet_ntoa( from.sin_addr );
			c.c_domain = ch_strdup( hp == NULL ? "" : hp->h_name );
		} else {
			Debug( LDAP_DEBUG_ARGS, "connection from unknown\n",
			    0, 0, 0 );
		}

		ber_init_w_nullc( &ber, 0 );

		while ( (tag = ber_get_next( &c.c_sb, &len, &ber ))
		    == LDAP_TAG_MESSAGE ) {
			ldap_pvt_thread_mutex_lock( &currenttime_mutex );
			time( &currenttime );
			ldap_pvt_thread_mutex_unlock( &currenttime_mutex );

			if ( (tag = ber_get_int( &ber, &msgid ))
			    != LDAP_TAG_MSGID ) {
				/* log and send error */
				Debug( LDAP_DEBUG_ANY,
				   "ber_get_int returns 0x%lx\n", tag, 0, 0 );
				ber_free( &ber, 1 );
				return 1;
			}

			if ( (tag = ber_peek_tag( &ber, &len ))
			    == LBER_ERROR ) {
				/* log, close and send error */
				Debug( LDAP_DEBUG_ANY,
				   "ber_peek_tag returns 0x%lx\n", tag, 0, 0 );
				ber_free( &ber, 1 );
				close( c.c_sb.sb_sd );
				c.c_sb.sb_sd = -1;
				return 1;
			}

			connection_activity( &c );

			ber_free( &ber, 1 );
		}
	}

shutdown:
	/* remember an error during shutdown */
	rc |= slap_shutdown(-1);
destroy:
	/* remember an error during destroy */
	rc |= slap_destroy();

	Debug( LDAP_DEBUG_ANY, "slapd stopped.\n", 0, 0, 0 );

	return rc;
}


#ifdef LOG_LOCAL4

/*
 *  Convert a string to an integer by means of a dispatcher table
 *  if the string is not in the table return the default
 */

static int
cnvt_str2int (stringVal, dispatcher, defaultVal)
char      *stringVal;
STRDISP_P  dispatcher;
int        defaultVal;
{
    int        retVal = defaultVal;
    STRDISP_P  disp;

    for (disp = dispatcher; disp->stringVal; disp++) {

        if (!strncasecmp (stringVal, disp->stringVal, disp->abbr)) {

            retVal = disp->intVal;
            break;

        }
    }

    return (retVal);

} /* cnvt_str2int */

#endif  /* LOG_LOCAL4 */

