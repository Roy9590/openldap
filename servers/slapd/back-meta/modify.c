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
meta_back_modify( Operation *op, SlapReply *rs )
{
	struct metainfo		*li = ( struct metainfo * )op->o_bd->be_private;
	struct metaconn 	*lc;
	int			rc = 0;
	LDAPMod			**modv = NULL;
	LDAPMod			*mods = NULL;
	Modifications		*ml;
	int			candidate = -1, i;
	char			*mdn;
	struct berval		mapped;

	lc = meta_back_getconn( li, op, rs, META_OP_REQUIRE_SINGLE,
			&op->o_req_ndn, &candidate );
	if ( !lc ) {
		rc = -1;
		goto cleanup;
	}
	
	if ( !meta_back_dobind( lc, op )
			|| !meta_back_is_valid( lc, candidate ) ) {
		rs->sr_err = LDAP_OTHER;
		rc = -1;
		goto cleanup;
	}

	/*
	 * Rewrite the modify dn, if needed
	 */
	switch ( rewrite_session( li->targets[ candidate ]->rwinfo,
				"modifyDn", op->o_req_dn.bv_val,
				op->o_conn, &mdn ) ) {
	case REWRITE_REGEXEC_OK:
		if ( mdn == NULL ) {
			mdn = ( char * )op->o_req_dn.bv_val;
		}
#ifdef NEW_LOGGING
		LDAP_LOG( BACK_META, DETAIL1,
				"[rw] modifyDn: \"%s\" -> \"%s\"\n",
				op->o_req_dn.bv_val, mdn, 0 );
#else /* !NEW_LOGGING */
		Debug( LDAP_DEBUG_ARGS, "rw> modifyDn: \"%s\" -> \"%s\"\n",
				op->o_req_dn.bv_val, mdn, 0 );
#endif /* !NEW_LOGGING */
		break;
		
	case REWRITE_REGEXEC_UNWILLING:
		rs->sr_err = LDAP_UNWILLING_TO_PERFORM;
		rs->sr_text = "Operation not allowed";
		rc = -1;
		goto cleanup;

	case REWRITE_REGEXEC_ERR:
		rs->sr_err = LDAP_OTHER;
		rs->sr_text = "Rewrite error";
		rc = -1;
		goto cleanup;
	}

	for ( i = 0, ml = op->oq_modify.rs_modlist; ml; i++ ,ml = ml->sml_next )
		;

	mods = ch_malloc( sizeof( LDAPMod )*i );
	if ( mods == NULL ) {
		rs->sr_err = LDAP_NO_MEMORY;
		rc = -1;
		goto cleanup;
	}
	modv = ( LDAPMod ** )ch_malloc( ( i + 1 )*sizeof( LDAPMod * ) );
	if ( modv == NULL ) {
		rs->sr_err = LDAP_NO_MEMORY;
		rc = -1;
		goto cleanup;
	}

	for ( i = 0, ml = op->oq_modify.rs_modlist; ml; ml = ml->sml_next ) {
		int j;

		if ( ml->sml_desc->ad_type->sat_no_user_mod  ) {
			continue;
		}

		ldap_back_map( &li->targets[ candidate ]->at_map,
				&ml->sml_desc->ad_cname, &mapped,
				BACKLDAP_MAP );
		if ( mapped.bv_val == NULL || mapped.bv_val[0] == '\0' ) {
			continue;
		}

		modv[ i ] = &mods[ i ];
		mods[ i ].mod_op = ml->sml_op | LDAP_MOD_BVALUES;
		mods[ i ].mod_type = mapped.bv_val;

		/*
		 * FIXME: dn-valued attrs should be rewritten
		 * to allow their use in ACLs at the back-ldap
		 * level.
		 */
		if ( strcmp( ml->sml_desc->ad_type->sat_syntax->ssyn_oid,
					SLAPD_DN_SYNTAX ) == 0 ) {
			ldap_dnattr_rewrite(
				li->targets[ candidate ]->rwinfo,
				ml->sml_bvalues, op->o_conn );
		}

		if ( ml->sml_bvalues != NULL ){
			for (j = 0; ml->sml_bvalues[ j ].bv_val; j++);
			mods[ i ].mod_bvalues = (struct berval **)ch_malloc((j+1) *
				sizeof(struct berval *));
			for (j = 0; ml->sml_bvalues[ j ].bv_val; j++)
				mods[ i ].mod_bvalues[ j ] = &ml->sml_bvalues[j];
			mods[ i ].mod_bvalues[ j ] = NULL;

		} else {
			mods[ i ].mod_bvalues = NULL;
		}

		i++;
	}
	modv[ i ] = 0;

	ldap_modify_s( lc->conns[ candidate ].ld, mdn, modv );

cleanup:;
	if ( mdn != op->o_req_dn.bv_val ) {
		free( mdn );
	}
	if ( modv != NULL ) {
		for ( i = 0; modv[ i ]; i++) {
			free( modv[ i ]->mod_bvalues );
		}
	}
	free( mods );
	free( modv );
	
	if ( rc == 0 ) {
		return meta_back_op_result( lc, op, rs ) == LDAP_SUCCESS
			? 0 : 1;
	} /* else */

	send_ldap_result( op, rs );

	return rc;
}

