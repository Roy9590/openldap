/* compare.c - monitor backend compare routine */
/*
 * Copyright 1998-2003 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */
/*
 * Copyright 2001, Pierangelo Masarati, All rights reserved. <ando@sys-net.it>
 * 
 * This work has beed deveolped for the OpenLDAP Foundation 
 * in the hope that it may be useful to the Open Source community, 
 * but WITHOUT ANY WARRANTY.
 * 
 * Permission is granted to anyone to use this software for any purpose
 * on any computer system, and to alter it and redistribute it, subject
 * to the following restrictions:
 * 
 * 1. The author and SysNet s.n.c. are not responsible for the consequences
 *    of use of this software, no matter how awful, even if they arise from
 *    flaws in it.
 * 
 * 2. The origin of this software must not be misrepresented, either by
 *    explicit claim or by omission.  Since few users ever read sources,
 *    credits should appear in the documentation.
 * 
 * 3. Altered versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.  Since few users
 *    ever read sources, credits should appear in the documentation.
 *    SysNet s.n.c. cannot be responsible for the consequences of the
 *    alterations.
 * 
 * 4. This notice may not be removed or altered.
 */

#include "portable.h"

#include <stdio.h>

#include <slap.h>
#include "back-monitor.h"

int
monitor_back_compare( struct slap_op *op, struct slap_rep *rs)
{
	struct monitorinfo      *mi = 
		(struct monitorinfo *) op->o_bd->be_private;
	Entry           *e, *matched = NULL;
	Attribute	*a;

	/* get entry with reader lock */
	monitor_cache_dn2entry( mi, &op->o_req_ndn, &e, &matched );
	if ( e == NULL ) {
		if ( matched ) {
			rs->sr_matched = ch_strdup( matched->e_dn );
			monitor_cache_release( mi, matched );
		}
		rs->sr_err = LDAP_NO_SUCH_OBJECT;

		send_ldap_result( op, rs );

		rs->sr_matched = NULL;

		return( 0 );
	}

	rs->sr_err = access_allowed( op, e, op->oq_compare.rs_ava->aa_desc,
			&op->oq_compare.rs_ava->aa_value, ACL_COMPARE, NULL );
	if ( !rs->sr_err ) {
		rs->sr_err = LDAP_INSUFFICIENT_ACCESS;
		goto return_results;
	}

	rs->sr_err = LDAP_NO_SUCH_ATTRIBUTE;

	for ( a = attrs_find( e->e_attrs, op->oq_compare.rs_ava->aa_desc );
			a != NULL;
			a = attrs_find( a->a_next, op->oq_compare.rs_ava->aa_desc )) {
		rs->sr_err = LDAP_COMPARE_FALSE;

#ifdef SLAP_NVALUES
		if ( value_find_ex( op->oq_compare.rs_ava->aa_desc,
			SLAP_MR_ATTRIBUTE_VALUE_NORMALIZED_MATCH |
				SLAP_MR_ASSERTED_VALUE_NORMALIZED_MATCH,
			a->a_nvals, &op->oq_compare.rs_ava->aa_value ) == 0 )
#else
		if ( value_find( ava->aa_desc, a->a_vals,
			&op->oq_compare.rs_ava->aa_value ) == 0 )
#endif
		{
			rs->sr_err = LDAP_COMPARE_TRUE;
			break;
		}
	}

return_results:;
	send_ldap_result( op, rs );
	if ( rs->sr_err == LDAP_COMPARE_FALSE
			|| rs->sr_err == LDAP_COMPARE_TRUE ) {
		rs->sr_err = LDAP_SUCCESS;
	}

	monitor_cache_release( mi, e );

	return( rs->sr_err );
}

