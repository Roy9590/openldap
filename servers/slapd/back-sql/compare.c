/* $OpenLDAP$ */
/* This work is part of OpenLDAP Software <http://www.openldap.org/>.
 *
 * Copyright 1999-2005 The OpenLDAP Foundation.
 * Portions Copyright 1999 Dmitry Kovalev.
 * Portions Copyright 2002 Pierangelo Masarati.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted only as authorized by the OpenLDAP
 * Public License.
 *
 * A copy of this license is available in the file LICENSE in the
 * top-level directory of the distribution or, alternatively, at
 * <http://www.OpenLDAP.org/license.html>.
 */
/* ACKNOWLEDGEMENTS:
 * This work was initially developed by Dmitry Kovalev for inclusion
 * by OpenLDAP Software.  Additional significant contributors include
 * Pierangelo Masarati.
 */

#include "portable.h"

#include <stdio.h>
#include <sys/types.h>

#include "slap.h"
#include "proto-sql.h"

int
backsql_compare( Operation *op, SlapReply *rs )
{
	SQLHDBC			dbh = SQL_NULL_HDBC;
	Entry			*e = NULL, user_entry;
	Attribute		*a = NULL;
	backsql_srch_info	bsi;
	int			rc;
	AttributeName		anlist[2];

	user_entry.e_name.bv_val = NULL;
	user_entry.e_name.bv_len = 0;
	user_entry.e_nname.bv_val = NULL;
	user_entry.e_nname.bv_len = 0;
	user_entry.e_attrs = NULL;
 
 	Debug( LDAP_DEBUG_TRACE, "==>backsql_compare()\n", 0, 0, 0 );

	rs->sr_err = backsql_get_db_conn( op, &dbh );
	if (!dbh) {
     		Debug( LDAP_DEBUG_TRACE, "backsql_compare(): "
			"could not get connection handle - exiting\n",
			0, 0, 0 );

		rs->sr_text = ( rs->sr_err == LDAP_OTHER )
			? "SQL-backend error" : NULL;
		goto return_results;
	}

	memset( &anlist[0], 0, 2 * sizeof( AttributeName ) );
	anlist[0].an_name = op->oq_compare.rs_ava->aa_desc->ad_cname;
	anlist[0].an_desc = op->oq_compare.rs_ava->aa_desc;

	/*
	 * Try to get attr as dynamic operational
	 */
	if ( is_at_operational( op->oq_compare.rs_ava->aa_desc->ad_type ) ) {
		SlapReply	nrs = { 0 };

		user_entry.e_attrs = NULL;
		user_entry.e_name = op->o_req_dn;
		user_entry.e_nname = op->o_req_ndn;

		nrs.sr_attrs = anlist;
		nrs.sr_entry = &user_entry;
		nrs.sr_attr_flags = SLAP_OPATTRS_NO;
		nrs.sr_operational_attrs = NULL;

		rs->sr_err = backsql_operational( op, &nrs );
		if ( rs->sr_err != LDAP_SUCCESS ) {
			goto return_results;
		}
		
		user_entry.e_attrs = nrs.sr_operational_attrs;

	} else {
		rc = backsql_init_search( &bsi, &op->o_req_ndn,
				LDAP_SCOPE_BASE, 
				SLAP_NO_LIMIT, SLAP_NO_LIMIT,
				(time_t)(-1), NULL, dbh, op, rs, anlist,
				BACKSQL_ISF_GET_ID );
		if ( rc != LDAP_SUCCESS ) {
			Debug( LDAP_DEBUG_TRACE, "backsql_compare(): "
				"could not retrieve compareDN ID - no such entry\n", 
				0, 0, 0 );
			rs->sr_err = LDAP_NO_SUCH_OBJECT;
			goto return_results;
		}

		bsi.bsi_e = &user_entry;
		rc = backsql_id2entry( &bsi, &bsi.bsi_base_id );
		if ( rc != LDAP_SUCCESS ) {
			Debug( LDAP_DEBUG_TRACE, "backsql_compare(): "
				"error %d in backsql_id2entry() "
				"- compare failed\n", rc, 0, 0 );
			rs->sr_err = rc;
			goto return_results;
		}
	}
	e = &user_entry;

	if ( ! access_allowed( op, e, op->oq_compare.rs_ava->aa_desc, 
				&op->oq_compare.rs_ava->aa_value,
				ACL_COMPARE, NULL ) ) {
		rs->sr_err = LDAP_INSUFFICIENT_ACCESS;
		goto return_results;
	}

	rs->sr_err = LDAP_NO_SUCH_ATTRIBUTE;
	for ( a = attrs_find( e->e_attrs, op->oq_compare.rs_ava->aa_desc );
			a != NULL;
			a = attrs_find( a->a_next, op->oq_compare.rs_ava->aa_desc ))
	{
		rs->sr_err = LDAP_COMPARE_FALSE;
		if ( value_find_ex( op->oq_compare.rs_ava->aa_desc,
					SLAP_MR_ATTRIBUTE_VALUE_NORMALIZED_MATCH |
					SLAP_MR_ASSERTED_VALUE_NORMALIZED_MATCH,
					a->a_nvals,
					&op->oq_compare.rs_ava->aa_value,
					op->o_tmpmemctx ) == 0 )
		{
			rs->sr_err = LDAP_COMPARE_TRUE;
			break;
		}
	}

return_results:;
	send_ldap_result( op, rs );

	if ( !BER_BVISNULL( &bsi.bsi_base_id.eid_ndn ) ) {
		(void)backsql_free_entryID( &bsi.bsi_base_id, 0 );
	}

	if ( e != NULL ) {
		entry_clean( e );
	}

	Debug(LDAP_DEBUG_TRACE,"<==backsql_compare()\n",0,0,0);
	switch ( rs->sr_err ) {
	case LDAP_COMPARE_TRUE:
	case LDAP_COMPARE_FALSE:
		return 0;

	default:
		return 1;
	}
}
 
