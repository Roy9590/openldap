/*
 *	 Copyright 1999, Dmitry Kovalev <mit@openldap.org>, All rights reserved.
 *
 *	 Redistribution and use in source and binary forms are permitted only
 *	 as authorized by the OpenLDAP Public License.	A copy of this
 *	 license is available at http://www.OpenLDAP.org/license.html or
 *	 in file LICENSE in the top-level directory of the distribution.
 */

#include "portable.h"

#ifdef SLAPD_SQL

#include <stdio.h>
#include <sys/types.h>
#include "slap.h"
#include "back-sql.h"
#include "sql-wrap.h"
#include "entry-id.h"
#include "util.h"

int
backsql_compare( Operation *op, SlapReply *rs )
{
	backsql_info		*bi = (backsql_info*)op->o_bd->be_private;
	backsql_entryID		user_id;
	SQLHDBC			dbh;
	Entry			*e, user_entry;
	Attribute		*a;
	backsql_srch_info	bsi;
	int			rc;
	AttributeName		anlist[2];
 
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

	rc = backsql_dn2id( bi, &user_id, dbh, &op->o_req_ndn );
	if ( rc != LDAP_SUCCESS ) {
		Debug( LDAP_DEBUG_TRACE, "backsql_compare(): "
			"could not retrieve bind dn id - no such entry\n", 
			0, 0, 0 );
		rs->sr_err = LDAP_INVALID_CREDENTIALS;
		goto return_results;
	}

	anlist[0].an_name = op->oq_compare.rs_ava->aa_desc->ad_cname;
	anlist[0].an_desc = op->oq_compare.rs_ava->aa_desc;
	anlist[1].an_name.bv_val = NULL;
	backsql_init_search( &bsi, &op->o_req_ndn, LDAP_SCOPE_BASE, 
			-1, -1, -1, NULL, dbh, op, anlist);
	e = backsql_id2entry( &bsi, &user_entry, &user_id );
	if ( e == NULL ) {
		Debug( LDAP_DEBUG_TRACE, "backsql_compare(): "
			"error in backsql_id2entry() - auth failed\n",
			0, 0, 0 );
		rs->sr_err = LDAP_OTHER;
		goto return_results;
	}

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

	Debug(LDAP_DEBUG_TRACE,"<==backsql_compare()\n",0,0,0);
	switch ( rs->sr_err ) {
	case LDAP_COMPARE_TRUE:
	case LDAP_COMPARE_FALSE:
		return 0;

	default:
		return 1;
	}
}
 
/*
 * sets the supported operational attributes (if required)
 */

int
backsql_operational(
	Operation	*op,
	SlapReply	*rs,
	int		opattrs,
	Attribute	**a )
{

	backsql_info 		*bi = (backsql_info*)op->o_bd->be_private;
	SQLHDBC 		dbh = SQL_NULL_HDBC;
	Attribute		**aa = a;
	int			rc = 0;

	Debug( LDAP_DEBUG_TRACE, "==>backsql_operational(): entry '%s'\n",
			rs->sr_entry->e_nname.bv_val, 0, 0 );


	if ( ( opattrs || ad_inlist( slap_schema.si_ad_hasSubordinates, rs->sr_attrs ) ) 
			&& attr_find( rs->sr_entry->e_attrs, slap_schema.si_ad_hasSubordinates ) == NULL ) {
		
		rc = backsql_get_db_conn( op, &dbh );
		if ( rc != LDAP_SUCCESS ) {
			Debug( LDAP_DEBUG_TRACE, "backsql_operational(): "
				"could not get connection handle - exiting\n", 
				0, 0, 0 );
			return 1;
		}
		
		rc = backsql_has_children( bi, dbh, &rs->sr_entry->e_nname );

		switch( rc ) {
		case LDAP_COMPARE_TRUE:
		case LDAP_COMPARE_FALSE:
			*aa = slap_operational_hasSubordinate( rc == LDAP_COMPARE_TRUE );
			if ( *aa != NULL ) {
				aa = &(*aa)->a_next;
			}
			rc = 0;
			break;

		default:
			Debug(LDAP_DEBUG_TRACE, 
				"backsql_operational(): "
				"has_children failed( %d)\n", 
				rc, 0, 0 );
			rc = 1;
			break;
		}
	}

	return rc;
}

#endif /* SLAPD_SQL */

