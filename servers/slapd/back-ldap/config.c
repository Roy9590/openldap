/* config.c - ldap backend configuration file routine */
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
#include "lutil.h"

static SLAP_EXTOP_MAIN_FN ldap_back_exop_whoami;

int
ldap_back_db_config(
    BackendDB	*be,
    const char	*fname,
    int		lineno,
    int		argc,
    char	**argv
)
{
	struct ldapinfo	*li = (struct ldapinfo *) be->be_private;

	if ( li == NULL ) {
		fprintf( stderr, "%s: line %d: ldap backend info is null!\n",
		    fname, lineno );
		return( 1 );
	}

	/* server address to query (depricated, use "uri" directive) */
	if ( strcasecmp( argv[0], "server" ) == 0 ) {
		if (argc != 2) {
			fprintf( stderr,
	"%s: line %d: missing address in \"server <address>\" line\n",
			    fname, lineno );
			return( 1 );
		}
		if (li->url != NULL)
			ch_free(li->url);
		li->url = ch_calloc(strlen(argv[1]) + 9, sizeof(char));
		if (li->url != NULL) {
			strcpy(li->url, "ldap://");
			strcat(li->url, argv[1]);
			strcat(li->url, "/");
		}

	/* URI of server to query (preferred over "server" directive) */
	} else if ( strcasecmp( argv[0], "uri" ) == 0 ) {
		if (argc != 2) {
			fprintf( stderr,
	"%s: line %d: missing address in \"uri <address>\" line\n",
			    fname, lineno );
			return( 1 );
		}
		if (li->url != NULL)
			ch_free(li->url);
		li->url = ch_strdup(argv[1]);

	/* name to use for ldap_back_group */
	} else if ( strcasecmp( argv[0], "binddn" ) == 0 ) {
		if (argc != 2) {
			fprintf( stderr,
	"%s: line %d: missing name in \"binddn <name>\" line\n",
			    fname, lineno );
			return( 1 );
		}
		ber_str2bv( argv[1], 0, 1, &li->binddn );

	/* password to use for ldap_back_group */
	} else if ( strcasecmp( argv[0], "bindpw" ) == 0 ) {
		if (argc != 2) {
			fprintf( stderr,
	"%s: line %d: missing password in \"bindpw <password>\" line\n",
			    fname, lineno );
			return( 1 );
		}
		ber_str2bv( argv[1], 0, 1, &li->bindpw );
	
	/* save bind creds for referral rebinds? */
	} else if ( strcasecmp( argv[0], "rebind-as-user" ) == 0 ) {
		if (argc != 1) {
			fprintf( stderr,
	"%s: line %d: rebind-as-user takes no arguments\n",
			    fname, lineno );
			return( 1 );
		}
		li->savecred = 1;
	
	/* intercept exop_who_am_i? */
	} else if ( strcasecmp( argv[0], "proxy-whoami" ) == 0 ) {
		if (argc != 1) {
			fprintf( stderr,
	"%s: line %d: proxy-whoami takes no arguments\n",
			    fname, lineno );
			return( 1 );
		}
		load_extop( (struct berval *)&slap_EXOP_WHOAMI, ldap_back_exop_whoami );
	
	/* dn massaging */
	} else if ( strcasecmp( argv[0], "suffixmassage" ) == 0 ) {
		BackendDB *tmp_be;
		struct berval bvnc, nvnc, pvnc, brnc, nrnc, prnc;
#ifdef ENABLE_REWRITE
		int rc;
#endif /* ENABLE_REWRITE */
		
		/*
		 * syntax:
		 * 
		 * 	suffixmassage <suffix> <massaged suffix>
		 *
		 * the <suffix> field must be defined as a valid suffix
		 * (or suffixAlias?) for the current database;
		 * the <massaged suffix> shouldn't have already been
		 * defined as a valid suffix or suffixAlias for the 
		 * current server
		 */
		if ( argc != 3 ) {
 			fprintf( stderr, "%s: line %d: syntax is"
				       " \"suffixMassage <suffix>"
				       " <massaged suffix>\"\n",
				fname, lineno );
			return( 1 );
		}
		
		ber_str2bv( argv[1], 0, 0, &bvnc );
		if ( dnPrettyNormal( NULL, &bvnc, &pvnc, &nvnc ) != LDAP_SUCCESS ) {
			fprintf( stderr, "%s: line %d: suffix DN %s is invalid\n",
				fname, lineno, bvnc.bv_val );
			return( 1 );
		}
		tmp_be = select_backend( &nvnc, 0, 0 );
		if ( tmp_be != NULL && tmp_be != be ) {
			fprintf( stderr, "%s: line %d: suffix already in use"
				       " by another backend in"
				       " \"suffixMassage <suffix>"
				       " <massaged suffix>\"\n",
				fname, lineno );
			free( nvnc.bv_val );
			free( pvnc.bv_val );
			return( 1 );
		}

		ber_str2bv( argv[2], 0, 0, &brnc );
		if ( dnPrettyNormal( NULL, &brnc, &prnc, &nrnc ) != LDAP_SUCCESS ) {
			fprintf( stderr, "%s: line %d: suffix DN %s is invalid\n",
				fname, lineno, brnc.bv_val );
			free( nvnc.bv_val );
			free( pvnc.bv_val );
			return( 1 );
		}

#if 0
		tmp_be = select_backend( &nrnc, 0, 0 );
		if ( tmp_be != NULL ) {
			fprintf( stderr, "%s: line %d: massaged suffix"
				       " already in use by another backend in" 
			       	       " \"suffixMassage <suffix>"
				       " <massaged suffix>\"\n",
                                fname, lineno );
			free( nvnc.bv_val );
			free( pvnc.bv_val );
			free( nrnc.bv_val );
			free( prnc.bv_val );
                        return( 1 );
		}
#endif

#ifdef ENABLE_REWRITE
		/*
		 * The suffix massaging is emulated by means of the
		 * rewrite capabilities
		 * FIXME: no extra rewrite capabilities should be added
		 * to the database
		 */
	 	rc = suffix_massage_config( li->rwmap.rwm_rw,
				&pvnc, &nvnc, &prnc, &nrnc );
		free( nvnc.bv_val );
		free( pvnc.bv_val );
		free( nrnc.bv_val );
		free( prnc.bv_val );

		return( rc );

#else /* !ENABLE_REWRITE */
		ber_bvarray_add( &li->rwmap.rwm_suffix_massage, &pvnc );
		ber_bvarray_add( &li->rwmap.rwm_suffix_massage, &nvnc );
		
		ber_bvarray_add( &li->rwmap.rwm_suffix_massage, &prnc );
		ber_bvarray_add( &li->rwmap.rwm_suffix_massage, &nrnc );
#endif /* !ENABLE_REWRITE */

	/* rewrite stuff ... */
 	} else if ( strncasecmp( argv[0], "rewrite", 7 ) == 0 ) {
#ifdef ENABLE_REWRITE
 		return rewrite_parse( li->rwmap.rwm_rw,
				fname, lineno, argc, argv );

#else /* !ENABLE_REWRITE */
		fprintf( stderr, "%s: line %d: rewrite capabilities "
				"are not enabled\n", fname, lineno );
#endif /* !ENABLE_REWRITE */
		
	/* objectclass/attribute mapping */
	} else if ( strcasecmp( argv[0], "map" ) == 0 ) {
		return ldap_back_map_config( &li->rwmap.rwm_oc,
				&li->rwmap.rwm_at,
				fname, lineno, argc, argv );

	/* anything else */
	} else {
		fprintf( stderr, "%s: line %d: unknown directive \"%s\" "
			"in ldap database definition (ignored)\n",
		    fname, lineno, argv[0] );
	}
	return 0;
}

int
ldap_back_map_config(
		struct ldapmap	*oc_map,
		struct ldapmap	*at_map,
		const char	*fname,
		int		lineno,
		int		argc,
		char		**argv )
{
	struct ldapmap		*map;
	struct ldapmapping	*mapping;
	char			*src, *dst;
	int			is_oc = 0;

	if ( argc < 3 || argc > 4 ) {
		fprintf( stderr,
	"%s: line %d: syntax is \"map {objectclass | attribute} [<local> | *] {<foreign> | *}\"\n",
			fname, lineno );
		return 1;
	}

	if ( strcasecmp( argv[1], "objectclass" ) == 0 ) {
		map = oc_map;
		is_oc = 1;

	} else if ( strcasecmp( argv[1], "attribute" ) == 0 ) {
		map = at_map;

	} else {
		fprintf( stderr, "%s: line %d: syntax is "
			"\"map {objectclass | attribute} [<local> | *] "
			"{<foreign> | *}\"\n",
			fname, lineno );
		return 1;
	}

	if ( strcmp( argv[2], "*" ) == 0 ) {
		if ( argc < 4 || strcmp( argv[3], "*" ) == 0 ) {
			map->drop_missing = ( argc < 4 );
			return 0;
		}
		src = dst = argv[3];

	} else if ( argc < 4 ) {
		src = "";
		dst = argv[2];

	} else {
		src = argv[2];
		dst = ( strcmp( argv[3], "*" ) == 0 ? src : argv[3] );
	}

	if ( ( map == at_map )
			&& ( strcasecmp( src, "objectclass" ) == 0
			|| strcasecmp( dst, "objectclass" ) == 0 ) )
	{
		fprintf( stderr,
			"%s: line %d: objectclass attribute cannot be mapped\n",
			fname, lineno );
	}

	mapping = (struct ldapmapping *)ch_calloc( 2,
		sizeof(struct ldapmapping) );
	if ( mapping == NULL ) {
		fprintf( stderr,
			"%s: line %d: out of memory\n",
			fname, lineno );
		return 1;
	}
	ber_str2bv( src, 0, 1, &mapping->src );
	ber_str2bv( dst, 0, 1, &mapping->dst );
	mapping[1].src = mapping->dst;
	mapping[1].dst = mapping->src;

	/*
	 * schema check
	 */
	if ( is_oc ) {
		if ( src[0] != '\0' ) {
			if ( oc_bvfind( &mapping->src ) == NULL ) {
				fprintf( stderr,
	"%s: line %d: warning, source objectClass '%s' "
	"should be defined in schema\n",
					fname, lineno, src );

				/*
				 * FIXME: this should become an err
				 */
			}
		}

		if ( oc_bvfind( &mapping->dst ) == NULL ) {
			fprintf( stderr,
	"%s: line %d: warning, destination objectClass '%s' "
	"is not defined in schema\n",
				fname, lineno, dst );
		}
	} else {
		int			rc;
		const char		*text = NULL;
		AttributeDescription	*ad = NULL;

		if ( src[0] != '\0' ) {
			rc = slap_bv2ad( &mapping->src, &ad, &text );
			if ( rc != LDAP_SUCCESS ) {
				fprintf( stderr,
	"%s: line %d: warning, source attributeType '%s' "
	"should be defined in schema\n",
					fname, lineno, src );

				/*
				 * FIXME: this should become an err
				 */
			}

			ad = NULL;
		}

		rc = slap_bv2ad( &mapping->dst, &ad, &text );
		if ( rc != LDAP_SUCCESS ) {
			fprintf( stderr,
	"%s: line %d: warning, destination attributeType '%s' "
	"is not defined in schema\n",
				fname, lineno, dst );
		}
	}

	if ( (src[0] != '\0' && avl_find( map->map, (caddr_t)mapping, mapping_cmp ) != NULL)
			|| avl_find( map->remap, (caddr_t)&mapping[1], mapping_cmp ) != NULL)
	{
		fprintf( stderr,
			"%s: line %d: duplicate mapping found (ignored)\n",
			fname, lineno );
		/* FIXME: free stuff */
		goto error_return;
	}

	if ( src[0] != '\0' ) {
		avl_insert( &map->map, (caddr_t)mapping,
					mapping_cmp, mapping_dup );
	}
	avl_insert( &map->remap, (caddr_t)&mapping[1],
				mapping_cmp, mapping_dup );

	return 0;

error_return:;
	if ( mapping ) {
		ch_free( mapping->src.bv_val );
		ch_free( mapping->dst.bv_val );
		ch_free( mapping );
	}

	return 1;
}

static int
ldap_back_exop_whoami(
	Operation *op,
	SlapReply *rs )
{
	struct berval *bv = NULL;

	if ( op->oq_extended.rs_reqdata != NULL ) {
		/* no request data should be provided */
		rs->sr_text = "no request data expected";
		return rs->sr_err = LDAP_PROTOCOL_ERROR;
	}

	rs->sr_err = backend_check_restrictions( op, rs, 
			(struct berval *)&slap_EXOP_WHOAMI );
	if( rs->sr_err != LDAP_SUCCESS ) return rs->sr_err;

	/* if auth'd by back-ldap and request is proxied, forward it */
	if ( op->o_conn->c_authz_backend && !strcmp(op->o_conn->c_authz_backend->be_type, "ldap" ) && !dn_match(&op->o_ndn, &op->o_conn->c_ndn)) {
		struct ldapconn *lc;

		LDAPControl c, *ctrls[2] = {&c, NULL};
		LDAPMessage *res;
		Operation op2 = *op;
		ber_int_t msgid;

		op2.o_ndn = op->o_conn->c_ndn;
		lc = ldap_back_getconn(&op2, rs);
		if (!lc || !ldap_back_dobind( lc, op, rs )) {
			return -1;
		}
		c.ldctl_oid = LDAP_CONTROL_PROXY_AUTHZ;
		c.ldctl_iscritical = 1;
		c.ldctl_value.bv_val = ch_malloc(op->o_ndn.bv_len+4);
		c.ldctl_value.bv_len = op->o_ndn.bv_len + 3;
		strcpy(c.ldctl_value.bv_val, "dn:");
		strcpy(c.ldctl_value.bv_val+3, op->o_ndn.bv_val);

		rs->sr_err = ldap_whoami(lc->ld, ctrls, NULL, &msgid);
		if (rs->sr_err == LDAP_SUCCESS) {
			if (ldap_result(lc->ld, msgid, 1, NULL, &res) == -1) {
				ldap_get_option(lc->ld, LDAP_OPT_ERROR_NUMBER,
					&rs->sr_err);
			} else {
				rs->sr_err = ldap_parse_whoami(lc->ld, res, &bv);
				ldap_msgfree(res);
			}
		}
		ch_free(c.ldctl_value.bv_val);
		if (rs->sr_err != LDAP_SUCCESS) {
			rs->sr_err = ldap_back_map_result(rs);
		}
	} else {
	/* else just do the same as before */
		bv = (struct berval *) ch_malloc( sizeof(struct berval) );
		if( op->o_dn.bv_len ) {
			bv->bv_len = op->o_dn.bv_len + sizeof("dn:")-1;
			bv->bv_val = ch_malloc( bv->bv_len + 1 );
			AC_MEMCPY( bv->bv_val, "dn:", sizeof("dn:")-1 );
			AC_MEMCPY( &bv->bv_val[sizeof("dn:")-1], op->o_dn.bv_val,
				op->o_dn.bv_len );
			bv->bv_val[bv->bv_len] = '\0';
		} else {
			bv->bv_len = 0;
			bv->bv_val = NULL;
		}
	}

	rs->sr_rspdata = bv;
	return rs->sr_err;
}


#ifdef ENABLE_REWRITE
static char *
suffix_massage_regexize( const char *s )
{
	char *res, *ptr;
	const char *p, *r;
	int i;

	for ( i = 0, p = s; 
			( r = strchr( p, ',' ) ) != NULL; 
			p = r + 1, i++ )
		;

	res = ch_calloc( sizeof( char ), strlen( s ) + 4 + 4*i + 1 );

	ptr = lutil_strcopy( res, "(.*)" );
	for ( i = 0, p = s;
			( r = strchr( p, ',' ) ) != NULL;
			p = r + 1 , i++ ) {
		ptr = lutil_strncopy( ptr, p, r - p + 1 );
		ptr = lutil_strcopy( ptr, "[ ]?" );

		if ( r[ 1 ] == ' ' ) {
			r++;
		}
	}
	lutil_strcopy( ptr, p );

	return res;
}

static char *
suffix_massage_patternize( const char *s )
{
	ber_len_t	len;
	char		*res;

	len = strlen( s );

	res = ch_calloc( sizeof( char ), len + sizeof( "%1" ) );
	if ( res == NULL ) {
		return NULL;
	}

	strcpy( res, "%1" );
	strcpy( res + sizeof( "%1" ) - 1, s );

	return res;
}

int
suffix_massage_config( 
		struct rewrite_info *info,
		struct berval *pvnc,
		struct berval *nvnc,
		struct berval *prnc,
		struct berval *nrnc
)
{
	char *rargv[ 5 ];
	int line = 0;

	rargv[ 0 ] = "rewriteEngine";
	rargv[ 1 ] = "on";
	rargv[ 2 ] = NULL;
	rewrite_parse( info, "<suffix massage>", ++line, 2, rargv );

	rargv[ 0 ] = "rewriteContext";
	rargv[ 1 ] = "default";
	rargv[ 2 ] = NULL;
	rewrite_parse( info, "<suffix massage>", ++line, 2, rargv );

	rargv[ 0 ] = "rewriteRule";
	rargv[ 1 ] = suffix_massage_regexize( pvnc->bv_val );
	rargv[ 2 ] = suffix_massage_patternize( prnc->bv_val );
	rargv[ 3 ] = ":";
	rargv[ 4 ] = NULL;
	rewrite_parse( info, "<suffix massage>", ++line, 4, rargv );
	ch_free( rargv[ 1 ] );
	ch_free( rargv[ 2 ] );
	
	rargv[ 0 ] = "rewriteContext";
	rargv[ 1 ] = "searchResult";
	rargv[ 2 ] = NULL;
	rewrite_parse( info, "<suffix massage>", ++line, 2, rargv );
	
	rargv[ 0 ] = "rewriteRule";
	rargv[ 1 ] = suffix_massage_regexize( prnc->bv_val );
	rargv[ 2 ] = suffix_massage_patternize( pvnc->bv_val );
	rargv[ 3 ] = ":";
	rargv[ 4 ] = NULL;
	rewrite_parse( info, "<suffix massage>", ++line, 4, rargv );
	ch_free( rargv[ 1 ] );
	ch_free( rargv[ 2 ] );

#if 0
	/*
	 * FIXME: this is no longer required since now we map filters
	 * based on the parsed filter structure, so we can deal directly
	 * with attribute types and values.  The rewriteContext 
	 * "searchFilter" now refers to the value of attrbutes
	 * with DN syntax.
	 */

	/*
	 * the filter should be rewritten as
	 * 
	 * rewriteRule
	 * 	"(.*)member=([^)]+),o=Foo Bar,[ ]?c=US(.*)"
	 * 	"%1member=%2,dc=example,dc=com%3"
	 *
	 * where "o=Foo Bar, c=US" is the virtual naming context,
	 * and "dc=example, dc=com" is the real naming context
	 */
	rargv[ 0 ] = "rewriteContext";
	rargv[ 1 ] = "searchFilter";
	rargv[ 2 ] = NULL;
	rewrite_parse( info, "<suffix massage>", ++line, 2, rargv );

#if 1 /* rewrite filters */
	{
		/*
		 * Note: this is far more optimistic than desirable:
		 * for any AVA value ending with the virtual naming
		 * context the terminal part will be replaced by the
		 * real naming context; a better solution would be to
		 * walk the filter looking for DN-valued attributes,
		 * and only rewrite those that require rewriting
		 */
		char 	vbuf_[BUFSIZ], *vbuf = vbuf_,
			rbuf_[BUFSIZ], *rbuf = rbuf_;
		int 	len;

		len = snprintf( vbuf, sizeof( vbuf_ ), 
				"(.*)%s\\)(.*)", nvnc->bv_val );
		if ( len == -1 ) {
			/* 
			 * traditional behavior: snprintf returns -1 
			 * if buffer is insufficient
			 */
			return -1;

		} else if ( len >= (int)sizeof( vbuf_ ) ) {
			/* 
			 * C99: snprintf returns the required size 
			 */
			vbuf = ch_malloc( len + 1 );
			len = snprintf( vbuf, len,
					"(.*)%s\\)(.*)", nvnc->bv_val );
			assert( len > 0 );
		}

		len = snprintf( rbuf, sizeof( rbuf_ ), "%%1%s)%%2", 
				nrnc->bv_val );
		if ( len == -1 ) {
			return -1;

		} else if ( len >= (int)sizeof( rbuf_ ) ) {
			rbuf = ch_malloc( len + 1 );
			len = snprintf( rbuf, sizeof( rbuf_ ), "%%1%s)%%2", 
					nrnc->bv_val );
			assert( len > 0 );
		}
		
		rargv[ 0 ] = "rewriteRule";
		rargv[ 1 ] = vbuf;
		rargv[ 2 ] = rbuf;
		rargv[ 3 ] = ":";
		rargv[ 4 ] = NULL;
		rewrite_parse( info, "<suffix massage>", ++line, 4, rargv );

		if ( vbuf != vbuf_ ) {
			ch_free( vbuf );
		}

		if ( rbuf != rbuf_ ) {
			ch_free( rbuf );
		}
	}
#endif /* rewrite filters */
#endif

#if 0 /*  "matched" is not normalized */
	rargv[ 0 ] = "rewriteContext";
	rargv[ 1 ] = "matchedDn";
	rargv[ 2 ] = "alias";
	rargv[ 3 ] = "searchResult";
	rargv[ 4 ] = NULL;
	rewrite_parse( info, "<suffix massage>", ++line, 4, rargv );
#else /* normalize "matched" */

	rargv[ 0 ] = "rewriteContext";
	rargv[ 1 ] = "matchedDN";
	rargv[ 2 ] = "alias";
	rargv[ 3 ] = "searchResult";
	rargv[ 4 ] = NULL;
	rewrite_parse( info, "<suffix massage>", ++line, 4, rargv );

	rargv[ 0 ] = "rewriteContext";
	rargv[ 1 ] = "searchAttrDN";
	rargv[ 2 ] = "alias";
	rargv[ 3 ] = "searchResult";
	rargv[ 4 ] = NULL;
	rewrite_parse( info, "<suffix massage>", ++line, 4, rargv );

#if 0
	rargv[ 0 ] = "rewriteRule";
	rargv[ 1 ] = suffix_massage_regexize( prnc->bv_val );
	rargv[ 2 ] = suffix_massage_patternize( nvnc->bv_val );
	rargv[ 3 ] = ":";
	rargv[ 4 ] = NULL;
	rewrite_parse( info, "<suffix massage>", ++line, 4, rargv );
	ch_free( rargv[ 1 ] );
	ch_free( rargv[ 2 ] );
#endif /* 0 */
#endif /* normalize "matched" */

	return 0;
}
#endif /* ENABLE_REWRITE */
