/* value.c - routines for dealing with values */
/* $OpenLDAP$ */
/*
 * Copyright 1998-2003 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */

#include "portable.h"

#include <stdio.h>

#include <ac/ctype.h>
#include <ac/socket.h>
#include <ac/string.h>
#include <ac/time.h>

#include <sys/stat.h>

#include "slap.h"

int
value_add( 
    BerVarray *vals,
    BerVarray addvals
)
{
	int	n, nn;
	BerVarray v2;

	for ( nn = 0; addvals != NULL && addvals[nn].bv_val != NULL; nn++ )
		;	/* NULL */

	if ( *vals == NULL ) {
		*vals = (BerVarray) SLAP_MALLOC( (nn + 1)
		    * sizeof(struct berval) );
		if( *vals == NULL ) {
#ifdef NEW_LOGGING
			 LDAP_LOG( OPERATION, ERR,
		      "value_add: SLAP_MALLOC failed.\n", 0, 0, 0 );
#else
			Debug(LDAP_DEBUG_TRACE,
		      "value_add: SLAP_MALLOC failed.\n", 0, 0, 0 );
#endif
			return LBER_ERROR_MEMORY;
		}
		n = 0;
	} else {
		for ( n = 0; (*vals)[n].bv_val != NULL; n++ ) {
			;	/* Empty */
		}
		*vals = (BerVarray) SLAP_REALLOC( (char *) *vals,
		    (n + nn + 1) * sizeof(struct berval) );
		if( *vals == NULL ) {
#ifdef NEW_LOGGING
			 LDAP_LOG( OPERATION, ERR,
		      "value_add: SLAP_MALLOC failed.\n", 0, 0, 0 );
#else
			Debug(LDAP_DEBUG_TRACE,
		      "value_add: SLAP_MALLOC failed.\n", 0, 0, 0 );
#endif
			return LBER_ERROR_MEMORY;
		}
	}

	v2 = *vals + n;
	for ( ; addvals->bv_val; v2++, addvals++ ) {
		ber_dupbv(v2, addvals);
		if (v2->bv_val == NULL) break;
	}
	v2->bv_val = NULL;
	v2->bv_len = 0;

	return LDAP_SUCCESS;
}

int
value_add_one( 
    BerVarray *vals,
    struct berval *addval
)
{
	int	n;
	BerVarray v2;

	if ( *vals == NULL ) {
		*vals = (BerVarray) SLAP_MALLOC( 2 * sizeof(struct berval) );
		if( *vals == NULL ) {
#ifdef NEW_LOGGING
			 LDAP_LOG( OPERATION, ERR,
		      "value_add_one: SLAP_MALLOC failed.\n", 0, 0, 0 );
#else
			Debug(LDAP_DEBUG_TRACE,
		      "value_add_one: SLAP_MALLOC failed.\n", 0, 0, 0 );
#endif
			return LBER_ERROR_MEMORY;
		}
		n = 0;
	} else {
		for ( n = 0; (*vals)[n].bv_val != NULL; n++ ) {
			;	/* Empty */
		}
		*vals = (BerVarray) SLAP_REALLOC( (char *) *vals,
		    (n + 2) * sizeof(struct berval) );
		if( *vals == NULL ) {
#ifdef NEW_LOGGING
			 LDAP_LOG( OPERATION, ERR,
		      "value_add_one: SLAP_MALLOC failed.\n", 0, 0, 0 );
#else
			Debug(LDAP_DEBUG_TRACE,
		      "value_add_one: SLAP_MALLOC failed.\n", 0, 0, 0 );
#endif
			return LBER_ERROR_MEMORY;
		}
	}

	v2 = *vals + n;
	ber_dupbv(v2, addval);

	v2++;
	v2->bv_val = NULL;
	v2->bv_len = 0;

	return LDAP_SUCCESS;
}

int asserted_value_validate_normalize( 
	AttributeDescription *ad,
	MatchingRule *mr,
	unsigned usage,
	struct berval *in,
	struct berval *out,
	const char ** text,
	void *ctx )
{
	int rc;

	/* we expect the value to be in the assertion syntax */
	assert( !SLAP_MR_IS_VALUE_OF_ATTRIBUTE_SYNTAX(usage) );

	if( mr == NULL ) {
		*text = "inappropriate matching request";
		return LDAP_INAPPROPRIATE_MATCHING;
	}

	if( !mr->smr_match ) {
		*text = "requested matching rule not supported";
		return LDAP_INAPPROPRIATE_MATCHING;
	}

	rc = (mr->smr_syntax->ssyn_validate)( mr->smr_syntax, in );

	if( rc != LDAP_SUCCESS ) {
		*text = "value does not conform to assertion syntax";
		return LDAP_INVALID_SYNTAX;
	}

	if( mr->smr_normalize ) {
		rc = (mr->smr_normalize)( usage,
			ad ? ad->ad_type->sat_syntax : NULL,
			mr, in, out, ctx );

		if( rc != LDAP_SUCCESS ) {
			*text = "unable to normalize value for matching";
			return LDAP_INVALID_SYNTAX;
		}

	} else {
		ber_dupbv_x( out, in, ctx );
	}

	return LDAP_SUCCESS;
}


int
value_match(
	int *match,
	AttributeDescription *ad,
	MatchingRule *mr,
	unsigned flags,
	struct berval *v1, /* stored value */
	void *v2, /* assertion */
	const char ** text )
{
	int rc;
	struct berval nv1 = { 0, NULL };
	struct berval nv2 = { 0, NULL };

	assert( mr != NULL );

	if( !mr->smr_match ) {
		return LDAP_INAPPROPRIATE_MATCHING;
	}


	rc = (mr->smr_match)( match, flags,
		ad->ad_type->sat_syntax,
		mr,
		nv1.bv_val != NULL ? &nv1 : v1,
		nv2.bv_val != NULL ? &nv2 : v2 );
	
	if (nv1.bv_val ) free( nv1.bv_val );
	if (nv2.bv_val ) free( nv2.bv_val );
	return rc;
}

int value_find_ex(
	AttributeDescription *ad,
	unsigned flags,
	BerVarray vals,
	struct berval *val,
	void *ctx )
{
	int	i;
	int rc;
	struct berval nval = { 0, NULL };
	MatchingRule *mr = ad->ad_type->sat_equality;

	if( mr == NULL || !mr->smr_match ) {
		return LDAP_INAPPROPRIATE_MATCHING;
	}

	assert(SLAP_IS_MR_ATTRIBUTE_VALUE_NORMALIZED_MATCH( flags ));

	if( !SLAP_IS_MR_ASSERTED_VALUE_NORMALIZED_MATCH( flags ) &&
		mr->smr_normalize )
	{
		rc = (mr->smr_normalize)(
			flags & (SLAP_MR_TYPE_MASK|SLAP_MR_SUBTYPE_MASK),
			ad ? ad->ad_type->sat_syntax : NULL,
			mr, val, &nval, ctx );

		if( rc != LDAP_SUCCESS ) {
			return LDAP_INVALID_SYNTAX;
		}
	}

	for ( i = 0; vals[i].bv_val != NULL; i++ ) {
		int match;
		const char *text;

		rc = value_match( &match, ad, mr, flags,
			&vals[i], nval.bv_val == NULL ? val : &nval, &text );

		if( rc == LDAP_SUCCESS && match == 0 ) {
			sl_free( nval.bv_val, ctx );
			return rc;
		}
	}

	sl_free( nval.bv_val, ctx );
	return LDAP_NO_SUCH_ATTRIBUTE;
}
