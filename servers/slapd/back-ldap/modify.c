/* modify.c - ldap backend modify function */
/* $OpenLDAP$ */
/*
 * Copyright 1998-2003 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */
/* This is an altered version */
/*
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
 *    ever read sources, credits should appear in the documentation.
 * 
 * 4. This notice may not be removed or altered.
 *
 *
 *
 * Copyright 2000, Pierangelo Masarati, All rights reserved. <ando@sys-net.it>
 * 
 * This software is being modified by Pierangelo Masarati.
 * The previously reported conditions apply to the modified code as well.
 * Changes in the original code are highlighted where required.
 * Credits for the original code go to the author, Howard Chu.
 */

#include "portable.h"

#include <stdio.h>

#include <ac/string.h>
#include <ac/socket.h>

#include "slap.h"
#include "back-ldap.h"

int
ldap_back_modify(
    Operation	*op,
    SlapReply	*rs )
{
	struct ldapinfo	*li = (struct ldapinfo *) op->o_bd->be_private;
	struct ldapconn *lc;
	LDAPMod **modv = NULL;
	LDAPMod *mods;
	Modifications *ml;
	int i, j, rc;
	struct berval mapped;
	struct berval mdn = { 0, NULL };
	ber_int_t msgid;
	dncookie dc;

	lc = ldap_back_getconn(op, rs);
	if ( !lc || !ldap_back_dobind( lc, op, rs ) ) {
		return( -1 );
	}

	/*
	 * Rewrite the modify dn, if needed
	 */
	dc.li = li;
#ifdef ENABLE_REWRITE
	dc.conn = op->o_conn;
	dc.rs = rs;
	dc.ctx = "modifyDn";
#else
	dc.tofrom = 1;
	dc.normalized = 0;
#endif
	if ( ldap_back_dn_massage( &dc, &op->o_req_dn, &mdn ) ) {
		send_ldap_result( op, rs );
		return -1;
	}

	for (i=0, ml=op->oq_modify.rs_modlist; ml; i++,ml=ml->sml_next)
		;

	mods = (LDAPMod *)ch_malloc(i*sizeof(LDAPMod));
	if (mods == NULL) {
		rc = LDAP_NO_MEMORY;
		goto cleanup;
	}
	modv = (LDAPMod **)ch_malloc((i+1)*sizeof(LDAPMod *));
	if (modv == NULL) {
		rc = LDAP_NO_MEMORY;
		goto cleanup;
	}

	for (i=0, ml=op->oq_modify.rs_modlist; ml; ml=ml->sml_next) {
		if ( ml->sml_desc->ad_type->sat_no_user_mod  ) {
			continue;
		}

		ldap_back_map(&li->at_map, &ml->sml_desc->ad_cname, &mapped,
				BACKLDAP_MAP);
		if (mapped.bv_val == NULL || mapped.bv_val[0] == '\0') {
			continue;
		}

		modv[i] = &mods[i];
		mods[i].mod_op = ml->sml_op | LDAP_MOD_BVALUES;
		mods[i].mod_type = mapped.bv_val;

		if ( ml->sml_desc->ad_type->sat_syntax ==
			slap_schema.si_syn_distinguishedName ) {
			ldap_dnattr_rewrite( &dc, ml->sml_bvalues );
		}

		if ( ml->sml_bvalues != NULL ) {	
			for (j = 0; ml->sml_bvalues[j].bv_val; j++);
			mods[i].mod_bvalues = (struct berval **)ch_malloc((j+1) *
				sizeof(struct berval *));
			for (j = 0; ml->sml_bvalues[j].bv_val; j++)
				mods[i].mod_bvalues[j] = &ml->sml_bvalues[j];
			mods[i].mod_bvalues[j] = NULL;
		} else {
			mods[i].mod_bvalues = NULL;
		}

		i++;
	}
	modv[i] = 0;

	rs->sr_err = ldap_modify_ext( lc->ld, mdn.bv_val, modv,
			op->o_ctrls, NULL, &msgid );

cleanup:;
	if ( mdn.bv_val != op->o_req_dn.bv_val ) {
		free( mdn.bv_val );
	}
	for (i=0; modv[i]; i++) {
		ch_free(modv[i]->mod_bvalues);
	}
	ch_free( mods );
	ch_free( modv );

	return ldap_back_op_result( lc, op, rs, msgid, 1 );
}

