/*
 * Copyright 1998-2003 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 *
 * Copyright 2001, Pierangelo Masarati, All rights reserved. <ando@sys-net.it>
 *
 * This work has been developed to fulfill the requirements
 * of SysNet s.n.c. <http:www.sys-net.it> and it has been donated
 * to the OpenLDAP Foundation in the hope that it may be useful
 * to the Open Source community, but WITHOUT ANY WARRANTY.
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
 * 
 * 
 * This software is based on the backend back-ldap, implemented
 * by Howard Chu <hyc@highlandsun.com>, and modified by Mark Valence
 * <kurash@sassafras.com>, Pierangelo Masarati <ando@sys-net.it> and other
 * contributors. The contribution of the original software to the present
 * implementation is acknowledged in this copyright statement.
 * 
 * A special acknowledgement goes to Howard for the overall architecture
 * (and for borrowing large pieces of code), and to Mark, who implemented
 * from scratch the attribute/objectclass mapping.
 * 
 * The original copyright statement follows.
 *
 * Copyright 1999, Howard Chu, All rights reserved. <hyc@highlandsun.com>
 *
 * Permission is granted to anyone to use this software for any purpose
 * on any computer system, and to alter it and redistribute it, subject
 * to the following restrictions:
 *
 * 1. The author is not responsible for the consequences of use of this
 *    software, no matter how awful, even if they arise from flaws in it.
 *
 * 2. The origin of this software must not be misrepresented, either by
 *    explicit claim or by omission.  Since few users ever read sources,
 *    credits should appear in the documentation.
 *
 * 3. Altered versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.  Since few users
 *    ever read sources, credits should appear in the
 *    documentation.
 *
 * 4. This notice may not be removed or altered.
 *                
 */


#include "portable.h"

#include <stdio.h>

#include <ac/string.h>
#include <ac/socket.h>

#include "slap.h"
#include "../back-ldap/back-ldap.h"
#include "back-meta.h"

int
meta_back_add( Operation *op, SlapReply *rs )
{
	struct metainfo *li = ( struct metainfo * )op->o_bd->be_private;
	struct metaconn *lc;
	int i, candidate = -1;
	Attribute *a;
	LDAPMod **attrs;
	struct berval mdn = { 0, NULL }, mapped;

#ifdef NEW_LOGGING
	LDAP_LOG( BACK_META, ENTRY, "meta_back_add: %s\n",
			op->o_req_dn.bv_val, 0, 0 );
#else /* !NEW_LOGGING */
	Debug(LDAP_DEBUG_ARGS, "==> meta_back_add: %s\n",
			op->o_req_dn.bv_val, 0, 0 );
#endif /* !NEW_LOGGING */

	/*
	 * get the current connection
	 */
	lc = meta_back_getconn( op, rs, META_OP_REQUIRE_SINGLE,
			&op->o_req_ndn, &candidate );
	if ( !lc ) {
		send_ldap_result( op, rs );
	}

	if ( !meta_back_dobind( lc, op )
			|| !meta_back_is_valid( lc, candidate ) ) {
		rs->sr_err = LDAP_OTHER;
 		send_ldap_result( op, rs );
		return -1;
	}

	/*
	 * Rewrite the add dn, if needed
	 */
	switch ( rewrite_session( li->targets[ candidate ]->rwinfo,
				"addDn", op->o_req_dn.bv_val, op->o_conn,
				&mdn.bv_val ) ) {
	case REWRITE_REGEXEC_OK:
		if ( mdn.bv_val != NULL && mdn.bv_val[ 0 ] != '\0' ) {
			mdn.bv_len = strlen( mdn.bv_val );
		} else {
			mdn = op->o_req_dn;
		}

#ifdef NEW_LOGGING
		LDAP_LOG( BACK_META, DETAIL1,
				"[rw] addDn: \"%s\" -> \"%s\"\n",
				op->o_req_dn.bv_val, mdn.bv_val, 0 );
#else /* !NEW_LOGGING */
		Debug( LDAP_DEBUG_ARGS, "rw> addDn: \"%s\" -> \"%s\"\n", 
				op->o_req_dn.bv_val, mdn.bv_val, 0 );
#endif /* !NEW_LOGGING */
		break;
 		
 	case REWRITE_REGEXEC_UNWILLING:
		rs->sr_err = LDAP_UNWILLING_TO_PERFORM;
		rs->sr_text = "Operation not allowed";
 		send_ldap_result( op, rs );
		return -1;
	       	
	case REWRITE_REGEXEC_ERR:
		rs->sr_err = LDAP_OTHER;
		rs->sr_text = "Rewrite error";
 		send_ldap_result( op, rs );
		return -1;
	}

	/* Count number of attributes in entry */
	for ( i = 1, a = op->oq_add.rs_e->e_attrs; a; i++, a = a->a_next );
	
	/* Create array of LDAPMods for ldap_add() */
	attrs = ch_malloc( sizeof( LDAPMod * )*i );

	for ( i = 0, a = op->oq_add.rs_e->e_attrs; a; a = a->a_next ) {
		int j;

		if ( a->a_desc->ad_type->sat_no_user_mod  ) {
			continue;
		}

		ldap_back_map( &li->targets[ candidate ]->at_map,
				&a->a_desc->ad_cname, &mapped, BACKLDAP_MAP );
		if ( mapped.bv_val == NULL || mapped.bv_val[0] == '\0' ) {
			continue;
		}

		attrs[ i ] = ch_malloc( sizeof( LDAPMod ) );
		if ( attrs[ i ] == NULL ) {
			continue;
		}
		attrs[ i ]->mod_op = LDAP_MOD_BVALUES;
		attrs[ i ]->mod_type = mapped.bv_val;

		/*
		 * FIXME: dn-valued attrs should be rewritten
		 * to allow their use in ACLs at the back-ldap
		 * level.
		 */
		if ( strcmp( a->a_desc->ad_type->sat_syntax->ssyn_oid,
					SLAPD_DN_SYNTAX ) == 0 ) {
			ldap_dnattr_rewrite( li->targets[ candidate ]->rwinfo,
					a->a_vals, op->o_conn );
		}

		for ( j = 0; a->a_vals[ j ].bv_val; j++ );
		attrs[ i ]->mod_vals.modv_bvals = ch_malloc((j+1)*sizeof(struct berval *));
		for ( j = 0; a->a_vals[ j ].bv_val; j++ ) {
			attrs[ i ]->mod_vals.modv_bvals[ j ] = &a->a_vals[ j ];
		}
		attrs[ i ]->mod_vals.modv_bvals[ j ] = NULL;
		i++;
	}
	attrs[ i ] = NULL;

	ldap_add_s( lc->conns[ candidate ].ld, mdn.bv_val, attrs );
	for ( --i; i >= 0; --i ) {
		free( attrs[ i ]->mod_vals.modv_bvals );
		free( attrs[ i ] );
	}
	free( attrs );
	if ( mdn.bv_val != op->oq_add.rs_e->e_dn ) {
		free( mdn.bv_val );
	}
	return meta_back_op_result( lc, op, rs );
}

