/*
 * Copyright (c) 1995 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

#include "portable.h"

#include <stdio.h>

#include <ac/socket.h>
#include <ac/string.h>
#include <ac/time.h>

#include "ldap_defaults.h"
#include "slap.h"

char *supportedControls[] = {
	NULL
};

char *supportedExtensions[] = {
	NULL
};

#if defined( SLAPD_MONITOR_DN )

void
monitor_info( Connection *conn, Operation *op )
{
	Entry		*e;
	char		buf[BUFSIZ];
	struct berval	val;
	struct berval	*vals[2];
	int    nconns, nwritewaiters, nreadwaiters;
	struct tm	*ltm;
	char		*p;
    char       buf2[22];
    char       buf3[22];
	Connection *c;
	int			connindex;
    time_t		currenttime;

	vals[0] = &val;
	vals[1] = NULL;

	e = (Entry *) ch_calloc( 1, sizeof(Entry) );
	/* initialize reader/writer lock */
	e->e_attrs = NULL;
	e->e_dn = ch_strdup( SLAPD_MONITOR_DN );
	e->e_ndn = dn_normalize_case( ch_strdup(SLAPD_MONITOR_DN) );
	e->e_private = NULL;

	val.bv_val = (char *) Versionstr;
	if (( p = strchr( Versionstr, '\n' )) == NULL ) {
		val.bv_len = strlen( Versionstr );
	} else {
		val.bv_len = p - Versionstr;
	}
	attr_merge( e, "version", vals );

	ldap_pvt_thread_mutex_lock( &active_threads_mutex );
	sprintf( buf, "%d", active_threads );
	ldap_pvt_thread_mutex_unlock( &active_threads_mutex );
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attr_merge( e, "threads", vals );

	nconns = 0;
	nwritewaiters = 0;
	nreadwaiters = 0;

	/* loop through the connections */
	for ( c = connection_first( &connindex );
		c != NULL;
		c = connection_next( c, &connindex ))
	{
		nconns++;
		if ( c->c_writewaiter ) {
			nwritewaiters++;
		}
		if ( c->c_currentber != NULL ) {
			nreadwaiters++;
		}

		ldap_pvt_thread_mutex_lock( &gmtime_mutex );
#ifndef LDAP_LOCALTIME
		ltm = gmtime( &c->c_starttime );
		strftime( buf2, sizeof(buf2), "%Y%m%d%H%M%SZ", ltm );

		ltm = gmtime( &c->c_activitytime );
		strftime( buf3, sizeof(buf2), "%Y%m%d%H%M%SZ", ltm );
#else
		ltm = localtime( &c->.c_starttime );
		strftime( buf2, sizeof(buf2), "%y%m%d%H%M%SZ", ltm );

		ltm = localtime( &c->c_activitytime );
		strftime( buf3, sizeof(buf2), "%y%m%d%H%M%SZ", ltm );
#endif

		ldap_pvt_thread_mutex_unlock( &gmtime_mutex );

		sprintf( buf,
			"%ld : %ld "
			": %ld/%ld/%ld/%ld "
			": %ld/%ld/%ld "
			": %s%s%s%s%s%s "
			": %s : %s : %s "
			": %s : %s",

			c->c_connid,
			(long) c->c_protocol,

			c->c_n_ops_received, c->c_n_ops_executing,
			c->c_n_ops_pending, c->c_n_ops_completed,

			/* add low-level counters here */
			c->c_n_get, c->c_n_read, c->c_n_write,

		    c->c_currentber ? "r" : "",
		    c->c_writewaiter ? "w" : "",
		    c->c_ops != NULL ? "x" : "",
		    c->c_pending_ops != NULL ? "p" : "",
			connection_state2str( c->c_conn_state ),
			c->c_bind_in_progress ? "S" : "",

		    c->c_cdn ? c->c_cdn : "<anonymous>",
		    c->c_client_addr ? c->c_client_addr : "unknown",
		    c->c_client_name ? c->c_client_name : "unknown",

		    buf2,
			buf3
		);

		val.bv_val = buf;
		val.bv_len = strlen( buf );
		attr_merge( e, "connection", vals );
	}
	connection_done(c);

	sprintf( buf, "%d", nconns );
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attr_merge( e, "currentconnections", vals );

	sprintf( buf, "%ld", connections_nextid() );
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attr_merge( e, "totalconnections", vals );

	sprintf( buf, "%ld", (long) dtblsize );
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attr_merge( e, "dtablesize", vals );

	sprintf( buf, "%d", nwritewaiters );
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attr_merge( e, "writewaiters", vals );

	sprintf( buf, "%d", nreadwaiters );
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attr_merge( e, "readwaiters", vals );

	ldap_pvt_thread_mutex_lock(&num_ops_mutex);
	sprintf( buf, "%ld", num_ops_initiated );
	ldap_pvt_thread_mutex_unlock(&num_ops_mutex);
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attr_merge( e, "opsinitiated", vals );

	ldap_pvt_thread_mutex_lock(&num_ops_mutex);
	sprintf( buf, "%ld", num_ops_completed );
	ldap_pvt_thread_mutex_unlock(&num_ops_mutex);
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attr_merge( e, "opscompleted", vals );

	ldap_pvt_thread_mutex_lock(&num_sent_mutex);
	sprintf( buf, "%ld", num_entries_sent );
	ldap_pvt_thread_mutex_unlock(&num_sent_mutex);
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attr_merge( e, "entriessent", vals );

	ldap_pvt_thread_mutex_lock(&num_sent_mutex);
	sprintf( buf, "%ld", num_bytes_sent );
	ldap_pvt_thread_mutex_unlock(&num_sent_mutex);
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attr_merge( e, "bytessent", vals );

	currenttime = slap_get_time();

	ldap_pvt_thread_mutex_lock( &gmtime_mutex );
#ifndef LDAP_LOCALTIME
	ltm = gmtime( &currenttime );
	strftime( buf, sizeof(buf), "%Y%m%d%H%M%SZ", ltm );
#else
	ltm = localtime( &currenttime );
	strftime( buf, sizeof(buf), "%y%m%d%H%M%SZ", ltm );
#endif
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attr_merge( e, "currenttime", vals );

#ifndef LDAP_LOCALTIME
	ltm = gmtime( &starttime );
	strftime( buf, sizeof(buf), "%Y%m%d%H%M%SZ", ltm );
#else
	ltm = localtime( &starttime );
	strftime( buf, sizeof(buf), "%y%m%d%H%M%SZ", ltm );
#endif
	ldap_pvt_thread_mutex_unlock( &gmtime_mutex );

	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attr_merge( e, "starttime", vals );

	sprintf( buf, "%d", nbackends );
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attr_merge( e, "nbackends", vals );

#ifdef HAVE_THREAD_CONCURRENCY
	sprintf( buf, "%d", ldap_pvt_thread_get_concurrency() );
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attr_merge( e, "concurrency", vals );
#endif

	send_search_entry( &backends[0], conn, op, e, NULL, 0 );
	send_ldap_search_result( conn, op, LDAP_SUCCESS, NULL, NULL, 1 );

	entry_free( e );
}

#endif /* slapd_monitor_dn */
