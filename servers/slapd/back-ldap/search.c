/* search.c - ldap backend search function */

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
 */

#include "portable.h"

#include <stdio.h>

#include <ac/socket.h>
#include <ac/string.h>
#include <ac/time.h>

#include "slap.h"
#include "back-ldap.h"

int
ldap_back_search(
    Backend	*be,
    Connection	*conn,
    Operation	*op,
    char	*base,
    int		scope,
    int		deref,
    int		size,
    int		time,
    Filter	*filter,
    char	*filterstr,
    char	**attrs,
    int		attrsonly
)
{
	struct ldapinfo	*li = (struct ldapinfo *) be->be_private;
	struct ldapconn *lc;
	struct timeval	tv;
	LDAPMessage		*res, *e;
	int			i, rc, msgid, sres = LDAP_SUCCESS; 
	char *match = NULL, *err = NULL;

	lc = ldap_back_getconn(li, conn, op);
	if (!lc)
		return( -1 );

	if (deref != -1)
		ldap_set_option( lc->ld, LDAP_OPT_DEREF, (void *)&deref);
	if (time != -1)
		ldap_set_option( lc->ld, LDAP_OPT_TIMELIMIT, (void *)&time);
	if (size != -1)
		ldap_set_option( lc->ld, LDAP_OPT_SIZELIMIT, (void *)&size);
	if (!lc->bound) {
		ldap_back_dobind(lc, op);
		if (!lc->bound)
			return( -1 );
	}

	if ((msgid = ldap_search(lc->ld, base, scope, filterstr, attrs,
		attrsonly)) == -1)
fail:		return( ldap_back_op_result(lc, op) );

	/* We pull apart the ber result, stuff it into a slapd entry, and
	 * let send_search_entry stuff it back into ber format. Slow & ugly,
	 * but this is necessary for version matching, and for ACL processing.
	 */
	
	for (i=0, rc=0; rc != -1;
		rc = ldap_result(lc->ld, LDAP_RES_ANY, 0, &tv, &res)) {
		int ab;

		/* check for abandon */
		ldap_pvt_thread_mutex_lock( &op->o_abandonmutex );
		ab = op->o_abandon;
		ldap_pvt_thread_mutex_unlock( &op->o_abandonmutex );

		if (ab) {
			ldap_abandon(lc->ld, msgid);
		} else if (rc == 0) {
			tv.tv_sec = 0;
			tv.tv_usec = 100000;
			ldap_pvt_thread_yield();
			continue;
		} else if (rc == LDAP_RES_SEARCH_ENTRY) {
			e = ldap_first_entry(lc->ld,res);
			ldap_send_entry(be, op, lc, e, attrs, attrsonly);
			i++;
		} else {
			sres = ldap_result2error(lc->ld, res, 1);
			ldap_get_option(lc->ld, LDAP_OPT_ERROR_STRING, &err);
			ldap_get_option(lc->ld, LDAP_OPT_MATCHED_DN, &match);
			rc = 0;
		}
		ldap_msgfree(res);
		if (ab)
			return (0);
		else if (rc == 0)
			break;
	}

	if (rc == -1)
		goto fail;

	send_ldap_search_result( conn, op, sres, match, err, i );
	if (match)
		free(match);
	if (err)
		free(err);
	return( 0 );
}

ldap_send_entry(
	Backend *be,
	Operation *op,
	struct ldapconn *lc,
	LDAPMessage *e,
	char **attrs,
	int attrsonly
)
{
	char *a;
	Entry ent;
	BerElement *ber = NULL;
	Attribute *attr;
	struct berval *dummy = NULL;

	ent.e_dn = ldap_get_dn(lc->ld, e);
	ent.e_ndn = dn_normalize_case( ch_strdup( ent.e_dn));
	ent.e_id = 0;
	ent.e_attrs = 0;
	ent.e_private = 0;
	attr = (Attribute *)4;
	attr = (Attribute *)((long)&ent.e_attrs - ((long)&attr->a_next-4));

	for (a = ldap_first_attribute(lc->ld, e, &ber); a;
		a = ldap_next_attribute(lc->ld, e, ber)) {
		attr->a_next = (Attribute *)ch_malloc( sizeof(Attribute) );
		attr=attr->a_next;
		attr->a_next = 0;
		attr->a_type = ch_strdup(a);
		attr->a_syntax = attr_syntax(a);
		attr->a_vals = ldap_get_values_len(lc->ld, e, a);
		if (!attr->a_vals)
			attr->a_vals = &dummy;
	}
	send_search_entry( be, lc->conn, op, &ent, attrs, attrsonly );
	for (;ent.e_attrs;) {
		attr=ent.e_attrs;
		ent.e_attrs = attr->a_next;
		free(attr->a_type);
		if (attr->a_vals != &dummy)
			ber_bvecfree(attr->a_vals);
		free(attr);
	}
	if (ber)
		ber_free(ber,0);
}
