/* bind.c - ldbm backend bind and unbind routines */
/* $OpenLDAP$ */
/*
 * Copyright 1998-2000 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */

#include "portable.h"

#include <stdio.h>

#include <ac/krb.h>
#include <ac/socket.h>
#include <ac/string.h>
#include <ac/unistd.h>

#include "slap.h"
#include "back-ldbm.h"
#include "proto-back-ldbm.h"

int
ldbm_back_bind(
    Backend		*be,
    Connection		*conn,
    Operation		*op,
    const char		*dn,
    const char		*ndn,
    int			method,
    struct berval	*cred,
	char**	edn
)
{
	struct ldbminfo	*li = (struct ldbminfo *) be->be_private;
	Entry		*e;
	Attribute	*a;
	int		rc;
	Entry		*matched;
#ifdef LDAP_API_FEATURE_X_OPENLDAP_V2_KBIND
	char		krbname[MAX_K_NAME_SZ + 1];
	AttributeDescription *krbattr = slap_schema.si_ad_krbName;
	AUTH_DAT	ad;
#endif

	AttributeDescription *password = slap_schema.si_ad_userPassword;

#ifdef NEW_LOGGING
	LDAP_LOG(( "backend", LDAP_LEVEL_ENTRY,
		"ldbm_back_bind: dn: %s.\n", dn ));
#else
	Debug(LDAP_DEBUG_ARGS, "==> ldbm_back_bind: dn: %s\n", dn, 0, 0);
#endif


	*edn = NULL;
	dn = ndn;

	/* get entry with reader lock */
	if ( (e = dn2entry_r( be, dn, &matched )) == NULL ) {
		char *matched_dn = NULL;
		struct berval **refs = NULL;

		if( matched != NULL ) {
			matched_dn = ch_strdup( matched->e_dn );

			refs = is_entry_referral( matched )
				? get_entry_referrals( be, conn, op, matched,
					dn, LDAP_SCOPE_DEFAULT )
				: NULL;

			cache_return_entry_r( &li->li_cache, matched );

		} else {
			refs = referral_rewrite( default_referral,
				NULL, dn, LDAP_SCOPE_DEFAULT );
		}

		/* allow noauth binds */
		rc = 1;
		if ( method == LDAP_AUTH_SIMPLE ) {
			if ( be_isroot_pw( be, conn, dn, cred ) ) {
				*edn = ch_strdup( be_root_dn( be ) );
				rc = 0; /* front end will send result */

			} else if ( refs != NULL ) {
				send_ldap_result( conn, op, LDAP_REFERRAL,
					matched_dn, NULL, refs, NULL );

			} else {
				send_ldap_result( conn, op, LDAP_INVALID_CREDENTIALS,
					NULL, NULL, NULL, NULL );
			}

		} else if ( refs != NULL ) {
			send_ldap_result( conn, op, LDAP_REFERRAL,
				matched_dn, NULL, refs, NULL );

		} else {
			send_ldap_result( conn, op, LDAP_INVALID_CREDENTIALS,
				NULL, NULL, NULL, NULL );
		}

		ber_bvecfree( refs );
		free( matched_dn );
		return( rc );
	}

	*edn = ch_strdup( e->e_dn );

	/* check for deleted */

	if ( is_entry_alias( e ) ) {
		/* entry is an alias, don't allow bind */
#ifdef NEW_LOGGING
		LDAP_LOG(( "backend", LDAP_LEVEL_INFO,
			   "ldbm_back_bind: entry (%s) is an alias.\n", e->e_dn ));
#else
		Debug( LDAP_DEBUG_TRACE, "entry is alias\n", 0,
		    0, 0 );
#endif


		send_ldap_result( conn, op, LDAP_ALIAS_PROBLEM,
		    NULL, "entry is alias", NULL, NULL );

		rc = 1;
		goto return_results;
	}

	if ( is_entry_referral( e ) ) {
		/* entry is a referral, don't allow bind */
		struct berval **refs = get_entry_referrals( be,
			conn, op, e, dn, LDAP_SCOPE_DEFAULT );

#ifdef NEW_LOGGING
		LDAP_LOG(( "backend", LDAP_LEVEL_INFO,
			   "ldbm_back_bind: entry(%s) is a referral.\n", e->e_dn ));
#else
		Debug( LDAP_DEBUG_TRACE, "entry is referral\n", 0,
		    0, 0 );
#endif


		if( refs != NULL ) {
			send_ldap_result( conn, op, LDAP_REFERRAL,
				e->e_dn, NULL, refs, NULL );

		} else {
			send_ldap_result( conn, op, LDAP_INVALID_CREDENTIALS,
				NULL, NULL, NULL, NULL );
		}

		ber_bvecfree( refs );

		rc = 1;
		goto return_results;
	}

	switch ( method ) {
	case LDAP_AUTH_SIMPLE:
		/* check for root dn/passwd */
		if ( be_isroot_pw( be, conn, dn, cred ) ) {
			/* front end will send result */
			if(*edn != NULL) free( *edn );
			*edn = ch_strdup( be_root_dn( be ) );
			rc = 0;
			goto return_results;
		}

		if ( ! access_allowed( be, conn, op, e,
			password, NULL, ACL_AUTH ) )
		{
			send_ldap_result( conn, op, LDAP_INSUFFICIENT_ACCESS,
				NULL, NULL, NULL, NULL );
			rc = 1;
			goto return_results;
		}

		if ( (a = attr_find( e->e_attrs, password )) == NULL ) {
			send_ldap_result( conn, op, LDAP_INAPPROPRIATE_AUTH,
			    NULL, NULL, NULL, NULL );

			/* stop front end from sending result */
			rc = 1;
			goto return_results;
		}

		if ( slap_passwd_check( conn, a, cred ) != 0 ) {
			send_ldap_result( conn, op, LDAP_INVALID_CREDENTIALS,
				NULL, NULL, NULL, NULL );
			/* stop front end from sending result */
			rc = 1;
			goto return_results;
		}

		rc = 0;
		break;

#ifdef LDAP_API_FEATURE_X_OPENLDAP_V2_KBIND
	case LDAP_AUTH_KRBV41:
		if ( krbv4_ldap_auth( be, cred, &ad ) != LDAP_SUCCESS ) {
			send_ldap_result( conn, op, LDAP_INVALID_CREDENTIALS,
			    NULL, NULL, NULL, NULL );
			rc = 1;
			goto return_results;
		}

		if ( ! access_allowed( be, conn, op, e,
			krbattr, NULL, ACL_AUTH ) )
		{
			send_ldap_result( conn, op, LDAP_INSUFFICIENT_ACCESS,
				NULL, NULL, NULL, NULL );
			rc = 1;
			goto return_results;
		}

		sprintf( krbname, "%s%s%s@%s", ad.pname, *ad.pinst ? "."
		    : "", ad.pinst, ad.prealm );

		if ( (a = attr_find( e->e_attrs, krbattr )) == NULL ) {
			/*
			 * no krbname values present:  check against DN
			 */
			if ( strcasecmp( dn, krbname ) == 0 ) {
				rc = 0;
				break;
			}
			send_ldap_result( conn, op, LDAP_INAPPROPRIATE_AUTH,
			    NULL, NULL, NULL, NULL );
			rc = 1;
			goto return_results;

		} else {	/* look for krbname match */
			struct berval	krbval;

			krbval.bv_val = krbname;
			krbval.bv_len = strlen( krbname );

			if ( value_find( a->a_desc, a->a_vals, &krbval ) != 0 ) {
				send_ldap_result( conn, op,
				    LDAP_INVALID_CREDENTIALS,
					NULL, NULL, NULL, NULL );
				rc = 1;
				goto return_results;
			}
		}
		rc = 0;
		break;

	case LDAP_AUTH_KRBV42:
		send_ldap_result( conn, op, LDAP_UNWILLING_TO_PERFORM,
			NULL, "Kerberos bind step 2 not supported",
			NULL, NULL );
		/* stop front end from sending result */
		rc = LDAP_UNWILLING_TO_PERFORM;
		goto return_results;
#endif

	default:
		send_ldap_result( conn, op, LDAP_STRONG_AUTH_NOT_SUPPORTED,
		    NULL, "authentication method not supported", NULL, NULL );
		rc = 1;
		goto return_results;
	}

return_results:;
	/* free entry and reader lock */
	cache_return_entry_r( &li->li_cache, e );

	/* front end with send result on success (rc==0) */
	return( rc );
}

