/* $OpenLDAP$ */
/*
 * Copyright 1998-2000 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */
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

#include <ac/string.h>
#include <ac/socket.h>

#include "ldap_pvt.h"
#include "slap.h"

int
do_delete(
    Connection	*conn,
    Operation	*op
)
{
	char *dn, *ndn = NULL;
	const char *text;
	Backend	*be;
	int rc;
	int manageDSAit;

#ifdef NEW_LOGGING
	LDAP_LOG(( "operation", LDAP_LEVEL_ENTRY,
		   "do_delete: conn %d\n", conn->c_connid ));
#else
	Debug( LDAP_DEBUG_TRACE, "do_delete\n", 0, 0, 0 );
#endif

	/*
	 * Parse the delete request.  It looks like this:
	 *
	 *	DelRequest := DistinguishedName
	 */

	if ( ber_scanf( op->o_ber, "a", &dn ) == LBER_ERROR ) {
#ifdef NEW_LOGGING
		LDAP_LOG(( "operation", LDAP_LEVEL_ERR,
			   "do_delete: conn: %d  ber_scanf failed\n", conn->c_connid ));
#else
		Debug( LDAP_DEBUG_ANY, "ber_scanf failed\n", 0, 0, 0 );
#endif
		send_ldap_disconnect( conn, op,
			LDAP_PROTOCOL_ERROR, "decoding error" );
		return SLAPD_DISCONNECT;
	}

	if( ( rc = get_ctrls( conn, op, 1 ) ) != LDAP_SUCCESS ) {
#ifdef NEW_LOGGING
		LDAP_LOG(( "oepration", LDAP_LEVEL_ERR,
			   "do_delete: conn %d  get_ctrls failed\n", conn->c_connid ));
#else
		Debug( LDAP_DEBUG_ANY, "do_delete: get_ctrls failed\n", 0, 0, 0 );
#endif
		goto cleanup;
	} 

	ndn = ch_strdup( dn );

	if(	dn_normalize( ndn ) == NULL ) {
#ifdef NEW_LOGGING
		LDAP_LOG(( "operation", LDAP_LEVEL_ERR,
			   "do_delete: conn %d  invalid dn (%s).\n", conn->c_connid, dn ));
#else
		Debug( LDAP_DEBUG_ANY, "do_delete: invalid dn (%s)\n", dn, 0, 0 );
#endif
		send_ldap_result( conn, op, rc = LDAP_INVALID_DN_SYNTAX, NULL,
		    "invalid DN", NULL, NULL );
		goto cleanup;
	}

	if( *ndn == '\0' ) {
#ifdef NEW_LOGGING
		LDAP_LOG(( "operation", LDAP_LEVEL_INFO, "do_delete: conn %d: "
			"Attempt to delete root DSE.\n", conn->c_connid ));
#else
		Debug( LDAP_DEBUG_ANY, "do_delete: root dse!\n", 0, 0, 0 );
#endif
		/* protocolError would likely be a more appropriate error */
		send_ldap_result( conn, op, rc = LDAP_UNWILLING_TO_PERFORM,
			NULL, "cannot delete the root DSE", NULL, NULL );
		goto cleanup;

#ifdef SLAPD_SCHEMA_DN
	} else if ( strcasecmp( ndn, SLAPD_SCHEMA_DN ) == 0 ) {
#ifdef NEW_LOGGING
		LDAP_LOG(( "operation", LDAP_LEVEL_INFO, "do_delete: conn %d: "
			"Attempt to delete subschema subentry.\n", conn->c_connid ));
#else
		Debug( LDAP_DEBUG_ANY, "do_delete: subschema subentry!\n", 0, 0, 0 );
#endif
		/* protocolError would likely be a more appropriate error */
		send_ldap_result( conn, op, rc = LDAP_UNWILLING_TO_PERFORM,
			NULL, "cannot delete the root DSE", NULL, NULL );
		goto cleanup;

#endif
	}

	Statslog( LDAP_DEBUG_STATS, "conn=%ld op=%d DEL dn=\"%s\"\n",
		op->o_connid, op->o_opid, dn, 0, 0 );

	manageDSAit = get_manageDSAit( op );

	/*
	 * We could be serving multiple database backends.  Select the
	 * appropriate one, or send a referral to our "referral server"
	 * if we don't hold it.
	 */
	if ( (be = select_backend( ndn, manageDSAit )) == NULL ) {
		struct berval **ref = referral_rewrite( default_referral,
			NULL, dn, LDAP_SCOPE_DEFAULT );

		send_ldap_result( conn, op, rc = LDAP_REFERRAL,
			NULL, NULL, ref ? ref : default_referral, NULL );

		ber_bvecfree( ref );
		goto cleanup;
	}

	/* check restrictions */
	rc = backend_check_restrictions( be, conn, op, NULL, &text ) ;
	if( rc != LDAP_SUCCESS ) {
		send_ldap_result( conn, op, rc,
			NULL, text, NULL, NULL );
		goto cleanup;
	}

	/* check for referrals */
	rc = backend_check_referrals( be, conn, op, dn, ndn );
	if ( rc != LDAP_SUCCESS ) {
		goto cleanup;
	}

	/* deref suffix alias if appropriate */
	ndn = suffix_alias( be, ndn );

	/*
	 * do the delete if 1 && (2 || 3)
	 * 1) there is a delete function implemented in this backend;
	 * 2) this backend is master for what it holds;
	 * 3) it's a replica and the dn supplied is the update_ndn.
	 */
	if ( be->be_delete ) {
		/* do the update here */
#ifndef SLAPD_MULTIMASTER
		int repl_user = be_isupdate( be, op->o_ndn );
		if ( be->be_update_ndn == NULL || repl_user )
#endif
		{
			if ( (*be->be_delete)( be, conn, op, dn, ndn ) == 0 ) {
#ifdef SLAPD_MULTIMASTER
				if (be->be_update_ndn == NULL || !repl_user )
#endif
				{
					replog( be, op, dn, NULL );
				}
			}
#ifndef SLAPD_MULTIMASTER
		} else {
			struct berval **defref = be->be_update_refs
				? be->be_update_refs : default_referral;
			struct berval **ref = referral_rewrite( default_referral,
				NULL, dn, LDAP_SCOPE_DEFAULT );

			send_ldap_result( conn, op, rc = LDAP_REFERRAL, NULL, NULL,
				ref ? ref : defref, NULL );

			ber_bvecfree( ref );
#endif
		}

	} else {
		send_ldap_result( conn, op, rc = LDAP_UNWILLING_TO_PERFORM,
			NULL, "operation not supported within namingContext", NULL, NULL );
	}
cleanup:
	if( ndn != NULL ) free( ndn );
	free( dn );
	return rc;
}
