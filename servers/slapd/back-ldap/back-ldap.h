/* back-ldap.h - ldap backend header file */
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

#ifndef SLAPD_LDAP_H
#define SLAPD_LDAP_H

#include "external.h"

/* String rewrite library */
#ifdef ENABLE_REWRITE
#include "rewrite.h"
#endif /* ENABLE_REWRITE */

LDAP_BEGIN_DECL

struct slap_conn;
struct slap_op;
struct slap_backend_db;

struct ldapconn {
	struct slap_conn	*conn;
	LDAP		*ld;
	struct berval	cred;
	struct berval 	bound_dn;
	struct berval	local_dn;
	int		bound;
	ldap_pvt_thread_mutex_t		lc_mutex;
};

struct ldapmap {
	int drop_missing;

	Avlnode *map;
	Avlnode *remap;
};

struct ldapmapping {
	struct berval src;
	struct berval dst;
};

struct ldapinfo {
	struct slap_backend_db	*be;
	char *url;
	struct berval binddn;
	struct berval bindpw;
	ldap_pvt_thread_mutex_t		conn_mutex;
	int savecred;
	Avlnode *conntree;
#ifdef ENABLE_REWRITE
	struct rewrite_info *rwinfo;
#else /* !ENABLE_REWRITE */
	BerVarray suffix_massage;
#endif /* !ENABLE_REWRITE */

	struct ldapmap oc_map;
	struct ldapmap at_map;
};

struct ldapconn *ldap_back_getconn(struct slap_op *op, struct slap_rep *rs);
int ldap_back_dobind(struct ldapconn *lc, Operation *op, SlapReply *rs);
int ldap_back_map_result(SlapReply *rs);
int ldap_back_op_result(struct ldapconn *lc, Operation *op, SlapReply *rs,
	ber_int_t msgid, int sendok);
int	back_ldap_LTX_init_module(int argc, char *argv[]);

void ldap_back_dn_massage(struct ldapinfo *li, struct berval *dn,
	struct berval *res, int normalized, int tofrom);

extern int ldap_back_conn_cmp( const void *c1, const void *c2);
extern int ldap_back_conn_dup( void *c1, void *c2 );
extern void ldap_back_conn_free( void *c );

/* attributeType/objectClass mapping */
int mapping_cmp (const void *, const void *);
int mapping_dup (void *, void *);

void ldap_back_map_init ( struct ldapmap *lm, struct ldapmapping ** );
void ldap_back_map ( struct ldapmap *map, struct berval *s, struct berval *m,
	int remap );
#define BACKLDAP_MAP	0
#define BACKLDAP_REMAP	1
char *
ldap_back_map_filter(
		struct ldapmap *at_map,
		struct ldapmap *oc_map,
		struct berval *f,
		int remap
);
char **
ldap_back_map_attrs(
		struct ldapmap *at_map,
		AttributeName *a,
		int remap
);

extern void mapping_free ( void *mapping );

extern int ldap_back_map_config(
		struct ldapmap	*oc_map,
		struct ldapmap	*at_map,
		const char	*fname,
		int		lineno,
		int		argc,
		char		**argv );

extern int
ldap_back_filter_map_rewrite_(
#ifdef ENABLE_REWRITE
		struct rewrite_info	*info,
		void			*cookie,
#endif /* ENABLE_REWRITE */
		struct ldapmap		*at_map,
		struct ldapmap		*oc_map,
		Filter			*f,
		struct berval		*fstr,
		int			remap );

/* suffix massaging by means of librewrite */
#ifdef ENABLE_REWRITE
extern int suffix_massage_config( struct rewrite_info *info,
		struct berval *pvnc, struct berval *nvnc,
		struct berval *prnc, struct berval *nrnc);
extern int ldap_dnattr_rewrite( struct rewrite_info *rwinfo, BerVarray a_vals, void *cookie );
#endif /* ENABLE_REWRITE */

LDAP_END_DECL

#endif /* SLAPD_LDAP_H */
