/* compare.c - bdb2 backend compare routine */

#include "portable.h"

#include <stdio.h>

#include <ac/socket.h>
#include <ac/string.h>

#include "slap.h"
#include "back-bdb2.h"
#include "proto-back-bdb2.h"

static int
bdb2i_back_compare_internal(
    BackendDB	*be,
    Connection	*conn,
    Operation	*op,
    char	*dn,
    Ava		*ava
)
{
	struct ldbminfo	*li = (struct ldbminfo *) be->be_private;
	char		*matched;
	Entry		*e;
	Attribute	*a;
	int		rc;

	/* get entry with reader lock */
	if ( (e = bdb2i_dn2entry_r( be, dn, &matched )) == NULL ) {
		send_ldap_result( conn, op, LDAP_NO_SUCH_OBJECT, matched, "" );

		if(matched == NULL) free(matched);
		return( 1 );
	}

	/* check for deleted */
	if ( ! access_allowed( be, conn, op, e,
		ava->ava_type, &ava->ava_value, ACL_COMPARE ) )
	{
		send_ldap_result( conn, op, LDAP_INSUFFICIENT_ACCESS, "", "" );
		rc = 1;
		goto return_results;
	}

	if ( (a = attr_find( e->e_attrs, ava->ava_type )) == NULL ) {
		send_ldap_result( conn, op, LDAP_NO_SUCH_ATTRIBUTE, "", "" );
		rc = 1;
		goto return_results;
	}

	if ( value_find( a->a_vals, &ava->ava_value, a->a_syntax, 1 ) == 0 ) 
		send_ldap_result( conn, op, LDAP_COMPARE_TRUE, "", "" );
	else
		send_ldap_result( conn, op, LDAP_COMPARE_FALSE, "", "" );

	rc = 0;

return_results:;
	bdb2i_cache_return_entry_r( &li->li_cache, e );
	return( rc );
}


int
bdb2_back_compare(
    BackendDB	*be,
    Connection	*conn,
    Operation	*op,
    char	*dn,
    Ava		*ava
)
{
	DB_LOCK         lock;
	struct ldbminfo	*li = (struct ldbminfo *) be->be_private;
	struct timeval  time1;
	int             ret;

	bdb2i_start_timing( be->be_private, &time1 );

	if ( bdb2i_enter_backend_r( get_dbenv( be ), &lock ) != 0 ) {

		send_ldap_result( conn, op, LDAP_OPERATIONS_ERROR, "", "" );
		return( 1 );

	}

	ret = bdb2i_back_compare_internal( be, conn, op, dn, ava );
	(void) bdb2i_leave_backend( get_dbenv( be ), lock );
	bdb2i_stop_timing( be->be_private, time1, "CMP", conn, op );

	return( ret );
}


