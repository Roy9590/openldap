/* filterindex.c - generate the list of candidate entries from a filter */
/* $OpenLDAP$ */
/*
 * Copyright 1998-2002 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */

#include "portable.h"

#include <stdio.h>
#include <ac/string.h>

#include "back-bdb.h"
#include "idl.h"

static int presence_candidates(
	Backend *be,
	AttributeDescription *desc,
	ID *ids );

static int equality_candidates(
	Backend *be,
	AttributeAssertion *ava,
	ID *ids,
	ID *tmp );
static int approx_candidates(
	Backend *be,
	AttributeAssertion *ava,
	ID *ids,
	ID *tmp );
static int substring_candidates(
	Backend *be,
	SubstringsAssertion *sub,
	ID *ids,
	ID *tmp );

static int list_candidates(
	Backend *be,
	Filter *flist,
	int ftype,
	ID *ids,
	ID *tmp );

int
bdb_filter_candidates(
	Backend	*be,
	Filter	*f,
	ID *ids,
	ID *tmp )
{
	int rc = -1;
#ifdef NEW_LOGGING
	LDAP_LOG (( "filterindex", LDAP_LEVEL_ENTRY, "=> bdb_filter_candidates\n"));
#else
	Debug( LDAP_DEBUG_FILTER, "=> bdb_filter_candidates\n", 0, 0, 0 );
#endif

	switch ( f->f_choice ) {
	case SLAPD_FILTER_DN_ONE:
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_ARGS, "=> bdb_filter_candidates: \tDN ONE\n"));
#else
		Debug( LDAP_DEBUG_FILTER, "\tDN ONE\n", 0, 0, 0 );
#endif
		rc = bdb_dn2idl( be, f->f_dn, DN_ONE_PREFIX, ids );
		break;

	case SLAPD_FILTER_DN_SUBTREE:
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_ARGS, "=> bdb_filter_candidates: \tDN SUBTREE\n"));
#else
		Debug( LDAP_DEBUG_FILTER, "\tDN SUBTREE\n", 0, 0, 0 );
#endif
		rc = bdb_dn2idl( be, f->f_dn, DN_SUBTREE_PREFIX, ids );
		break;

	case LDAP_FILTER_PRESENT:
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_ARGS, "=> bdb_filter_candidates: \tPRESENT\n"));
#else
		Debug( LDAP_DEBUG_FILTER, "\tPRESENT\n", 0, 0, 0 );
#endif
		rc = presence_candidates( be, f->f_desc, ids );
		break;

	case LDAP_FILTER_EQUALITY:
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_ARGS, "=> bdb_filter_candidates: \tEQUALITY\n"));
#else
		Debug( LDAP_DEBUG_FILTER, "\tEQUALITY\n", 0, 0, 0 );
#endif
		rc = equality_candidates( be, f->f_ava, ids, tmp );
		break;

	case LDAP_FILTER_APPROX:
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_ARGS, "=> bdb_filter_candidates: \tAPPROX\n"));
#else
		Debug( LDAP_DEBUG_FILTER, "\tAPPROX\n", 0, 0, 0 );
#endif
		rc = approx_candidates( be, f->f_ava, ids, tmp );
		break;

	case LDAP_FILTER_SUBSTRINGS:
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_ARGS, "=> bdb_filter_candidates: \tSUBSTRINGS\n"));
#else
		Debug( LDAP_DEBUG_FILTER, "\tSUBSTRINGS\n", 0, 0, 0 );
#endif
		rc = substring_candidates( be, f->f_sub, ids, tmp );
		break;

	case LDAP_FILTER_GE:
		/* no GE index, use pres */
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_ARGS, "=> bdb_filter_candidates: \tGE\n"));
#else
		Debug( LDAP_DEBUG_FILTER, "\tGE\n", 0, 0, 0 );
#endif
		rc = presence_candidates( be, f->f_ava->aa_desc, ids );
		break;

	case LDAP_FILTER_LE:
		/* no LE index, use pres */
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_ARGS, "=> bdb_filter_candidates: \tLE\n"));
#else
		Debug( LDAP_DEBUG_FILTER, "\tLE\n", 0, 0, 0 );
#endif
		rc = presence_candidates( be, f->f_ava->aa_desc, ids );
		break;

	case LDAP_FILTER_NOT:
		/* no indexing to support NOT filters */
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_ARGS, "=> bdb_filter_candidates: \tNOT\n"));
#else
		Debug( LDAP_DEBUG_FILTER, "\tNOT\n", 0, 0, 0 );
#endif
		break;

	case LDAP_FILTER_AND:
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_ARGS, "=> bdb_filter_candidates: \tAND\n"));
#else
		Debug( LDAP_DEBUG_FILTER, "\tAND\n", 0, 0, 0 );
#endif
		rc = list_candidates( be, 
			f->f_and, LDAP_FILTER_AND, ids, tmp );
		break;

	case LDAP_FILTER_OR:
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_ARGS, "=> bdb_filter_candidates: \tOR\n"));
#else
		Debug( LDAP_DEBUG_FILTER, "\tOR\n", 0, 0, 0 );
#endif
		rc = list_candidates( be, 
			f->f_or, LDAP_FILTER_OR, ids, tmp );
		break;

	default:
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_ARGS, "=> bdb_filter_candidates: \tUNKNOWN\n"));
#else
		Debug( LDAP_DEBUG_FILTER, "\tUNKNOWN %d\n",
			f->f_choice, 0, 0 );
#endif
	}

#ifdef NEW_LOGGING
	LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "=> bdb_filter_candidates: id=%ld first=%ld last=%ld\n", (long) ids[0], (long) BDB_IDL_FIRST( ids ), (long) BDB_IDL_LAST( ids ) ));
#else
	Debug( LDAP_DEBUG_FILTER,
		"<= bdb_filter_candidates: id=%ld first=%ld last=%ld\n",
		(long) ids[0],
		(long) BDB_IDL_FIRST( ids ),
		(long) BDB_IDL_LAST( ids ) );
#endif

	return rc;
}

static int
list_candidates(
	Backend	*be,
	Filter	*flist,
	int		ftype,
	ID *ids,
	ID *tmp )
{
	struct bdb_info *bdb = (struct bdb_info *) be->be_private;
	int rc = 0;
	Filter	*f;

/* Systems that can't increase thread stack size will die with these
 * structures allocated on the stack. */
#if !defined(LDAP_PVT_THREAD_STACK_SIZE) || (LDAP_PVT_THREAD_STACK_SIZE == 0)
	ID *save = ch_malloc(BDB_IDL_UM_SIZEOF);
#else
	ID save[BDB_IDL_UM_SIZE];
#endif

#ifdef NEW_LOGGING
	LDAP_LOG (( "filterindex", LDAP_LEVEL_ARGS, "=> bdb_list_candidates: 0x%x\n", ftype));
#else
	Debug( LDAP_DEBUG_FILTER, "=> bdb_list_candidates 0x%x\n", ftype, 0, 0 );
#endif

	if ( ftype == LDAP_FILTER_OR ) {
		BDB_IDL_ALL( bdb, save );
		BDB_IDL_ZERO( ids );
	} else {
		BDB_IDL_CPY( save, ids );
	}

	for ( f = flist; f != NULL; f = f->f_next ) {
		rc = bdb_filter_candidates( be, f, save, tmp );

		if ( rc != 0 ) {
			if ( ftype == LDAP_FILTER_AND ) {
				rc = 0;
				continue;
			}
			break;
		}
		
		if ( ftype == LDAP_FILTER_AND ) {
			bdb_idl_intersection( ids, save );
			if( BDB_IDL_IS_ZERO( ids ) )
				break;
		} else {
			bdb_idl_union( ids, save );
			BDB_IDL_ALL( bdb, save );
		}
	}

#if !defined(LDAP_PVT_THREAD_STACK_SIZE) || (LDAP_PVT_THREAD_STACK_SIZE == 0)
	free(save);
#endif

	if( rc ) {
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_list_candidates: id=%ld first=%ld last=%ld\n", (long) ids[0], (long) BDB_IDL_FIRST( ids ), (long) BDB_IDL_LAST( ids ) ));
#else
		Debug( LDAP_DEBUG_FILTER,
			"<= bdb_list_candidates: id=%ld first=%ld last=%ld\n",
			(long) ids[0],
			(long) BDB_IDL_FIRST(ids),
			(long) BDB_IDL_LAST(ids) );
#endif

	} else {
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_ARGS, "<= bdb_list_candidates: rc=%d\n", rc));
#else
		Debug( LDAP_DEBUG_FILTER,
			"<= bdb_list_candidates: undefined rc=%d\n",
			rc, 0, 0 );
#endif
	}

	return rc;
}

static int
presence_candidates(
	Backend	*be,
	AttributeDescription *desc,
	ID *ids )
{
	struct bdb_info *bdb = (struct bdb_info *) be->be_private;
	DB *db;
	int rc;
	slap_mask_t mask;
	struct berval prefix = {0};

#ifdef NEW_LOGGING
	LDAP_LOG (( "filterindex", LDAP_LEVEL_ENTRY, "=> bdb_presence_candidates\n"));
#else
	Debug( LDAP_DEBUG_TRACE, "=> bdb_presence_candidates\n", 0, 0, 0 );
#endif

	if( desc == slap_schema.si_ad_objectClass ) {
		BDB_IDL_ALL( bdb, ids );
		return 0;
	}

	rc = bdb_index_param( be, desc, LDAP_FILTER_PRESENT,
		&db, &mask, &prefix );

	if( rc != LDAP_SUCCESS ) {
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "=> bdb_presence_candidates: index_parm returned=%d\n", rc ));
#else
		Debug( LDAP_DEBUG_TRACE,
			"<= bdb_presence_candidates: index_param returned=%d\n",
			rc, 0, 0 );
#endif
		return 0;
	}

	if( db == NULL ) {
		/* not indexed */
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_presence_candidates: not indexed\n" ));
#else
		Debug( LDAP_DEBUG_TRACE,
			"<= bdb_presence_candidates: not indexed\n",
			0, 0, 0 );
#endif
		return 0;
	}

	if( prefix.bv_val == NULL ) {
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_presence_candidates: no prefix\n" ));
#else
		Debug( LDAP_DEBUG_TRACE,
			"<= bdb_presence_candidates: no prefix\n",
			0, 0, 0 );
#endif
		return 0;
	}

	rc = bdb_key_read( be, db, NULL, &prefix, ids );

	if( rc == DB_NOTFOUND ) {
		BDB_IDL_ZERO( ids );
		rc = 0;
	} else if( rc != LDAP_SUCCESS ) {
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_presence_candidates: key read failed (%d)\n", rc ));
#else
		Debug( LDAP_DEBUG_TRACE,
			"<= bdb_presense_candidates: key read failed (%d)\n",
			rc, 0, 0 );
#endif
		goto done;
	}

#ifdef NEW_LOGGING
	LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_presence_candidates: id=%ld first=%ld last=%ld\n", (long) ids[0], (long) BDB_IDL_FIRST( ids ), (long) BDB_IDL_LAST( ids ) ));
#else
	Debug(LDAP_DEBUG_TRACE,
		"<= bdb_presence_candidates: id=%ld first=%ld last=%ld\n",
		(long) ids[0],
		(long) BDB_IDL_FIRST(ids),
		(long) BDB_IDL_LAST(ids) );
#endif

done:
	return rc;
}

static int
equality_candidates(
	Backend	*be,
	AttributeAssertion *ava,
	ID *ids,
	ID *tmp )
{
	DB	*db;
	int i;
	int rc;
	slap_mask_t mask;
	struct berval prefix = {0};
	struct berval *keys = NULL;
	MatchingRule *mr;

#ifdef NEW_LOGGING
	LDAP_LOG (( "filterindex", LDAP_LEVEL_ENTRY, "=> equality_candidates\n"));
#else
	Debug( LDAP_DEBUG_TRACE, "=> bdb_equality_candidates\n", 0, 0, 0 );
#endif

	rc = bdb_index_param( be, ava->aa_desc, LDAP_FILTER_EQUALITY,
		&db, &mask, &prefix );

	if( rc != LDAP_SUCCESS ) {
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "=> bdb_equality_candidates: index_param failed (%d)\n", rc));
#else
		Debug( LDAP_DEBUG_ANY,
			"<= bdb_equality_candidates: index_param failed (%d)\n",
			rc, 0, 0 );
#endif
		return rc;
	}

	if ( db == NULL ) {
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "=> bdb_equality_candidates: not indexed\n"));
#else
		Debug( LDAP_DEBUG_ANY,
			"<= bdb_equality_candidates: not indexed\n", 0, 0, 0 );
#endif
		return -1;
	}

	mr = ava->aa_desc->ad_type->sat_equality;
	if( !mr ) {
		return -1;
	}

	if( !mr->smr_filter ) {
		return -1;
	}

	rc = (mr->smr_filter)(
		LDAP_FILTER_EQUALITY,
		mask,
		ava->aa_desc->ad_type->sat_syntax,
		mr,
		&prefix,
		&ava->aa_value,
		&keys );

	if( rc != LDAP_SUCCESS ) {
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "=> bdb_equality_candidates: MR filter failed (%d)\n", rc));
#else
		Debug( LDAP_DEBUG_TRACE,
			"<= bdb_equality_candidates: MR filter failed (%d)\n",
			rc, 0, 0 );
#endif
		return rc;
	}

	if( keys == NULL ) {
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "=> bdb_equality_candidates: no keys\n"));
#else
		Debug( LDAP_DEBUG_TRACE,
			"<= bdb_equality_candidates: no keys\n",
			0, 0, 0 );
#endif
		return 0;
	}

	for ( i= 0; keys[i].bv_val != NULL; i++ ) {
		rc = bdb_key_read( be, db, NULL, &keys[i], tmp );

		if( rc == DB_NOTFOUND ) {
			BDB_IDL_ZERO( ids );
			rc = 0;
		} else if( rc != LDAP_SUCCESS ) {
#ifdef NEW_LOGGING
			LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_equality_candidates: key read failed (%d)\n", rc));
#else
			Debug( LDAP_DEBUG_TRACE,
				"<= bdb_equality_candidates key read failed (%d)\n",
				rc, 0, 0 );
#endif
			break;
		}

		if( BDB_IDL_IS_ZERO( tmp ) ) {
#ifdef NEW_LOGGING
			LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "=> bdb_equality_candidates: NULL\n"));
#else
			Debug( LDAP_DEBUG_TRACE,
				"<= bdb_equality_candidates NULL\n",
				0, 0, 0 );
#endif
			BDB_IDL_ZERO( ids );
			break;
		}

		bdb_idl_intersection( ids, tmp );

		if( BDB_IDL_IS_ZERO( ids ) )
			break;
	}

	ber_bvarray_free( keys );

#ifdef NEW_LOGGING
	LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_equality_candidates: id=%ld first=%ld last=%ld\n", (long) ids[0], (long) BDB_IDL_FIRST( ids ), (long) BDB_IDL_LAST( ids ) ));
#else
	Debug( LDAP_DEBUG_TRACE,
		"<= bdb_equality_candidates id=%ld, first=%ld, last=%ld\n",
		(long) ids[0],
		(long) BDB_IDL_FIRST(ids),
		(long) BDB_IDL_LAST(ids) );
#endif
	return( rc );
}


static int
approx_candidates(
	Backend	*be,
	AttributeAssertion *ava,
	ID *ids,
	ID *tmp )
{
	DB	*db;
	int i;
	int rc;
	slap_mask_t mask;
	struct berval prefix = {0};
	struct berval *keys = NULL;
	MatchingRule *mr;

#ifdef NEW_LOGGING
	LDAP_LOG (( "filterindex", LDAP_LEVEL_ENTRY, "=> bdb_approx_candidates\n"));
#else
	Debug( LDAP_DEBUG_TRACE, "=> bdb_approx_candidates\n", 0, 0, 0 );
#endif

	rc = bdb_index_param( be, ava->aa_desc, LDAP_FILTER_APPROX,
		&db, &mask, &prefix );

	if( rc != LDAP_SUCCESS ) {
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_approx_candidates: index_param failed (%d)\n", rc ));
#else
		Debug( LDAP_DEBUG_ANY,
			"<= bdb_approx_candidates: index_param failed (%d)\n",
			rc, 0, 0 );
#endif
		return rc;
	}

	if ( db == NULL ) {
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_approx_candidates: not indexed\n" ));
#else
		Debug( LDAP_DEBUG_ANY,
			"<= bdb_approx_candidates: not indexed\n", 0, 0, 0 );
#endif
		return -1;
	}

	mr = ava->aa_desc->ad_type->sat_approx;
	if( !mr ) {
		/* no approx matching rule, try equality matching rule */
		mr = ava->aa_desc->ad_type->sat_equality;
	}

	if( !mr ) {
		return -1;
	}

	if( !mr->smr_filter ) {
		return -1;
	}

	rc = (mr->smr_filter)(
		LDAP_FILTER_APPROX,
		mask,
		ava->aa_desc->ad_type->sat_syntax,
		mr,
		&prefix,
		&ava->aa_value,
		&keys );

	if( rc != LDAP_SUCCESS ) {
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_approx_candidates: MR filter failed (%d)\n", rc ));
#else
		Debug( LDAP_DEBUG_TRACE,
			"<= bdb_approx_candidates: (%s) MR filter failed (%d)\n",
			prefix.bv_val, rc, 0 );
#endif
		return rc;
	}

	if( keys == NULL ) {
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_approx_candidates: no keys (%s)\n", prefix.bv_val ));
#else
		Debug( LDAP_DEBUG_TRACE,
			"<= bdb_approx_candidates: no keys (%s)\n",
			prefix.bv_val, 0, 0 );
#endif
		return 0;
	}

	for ( i= 0; keys[i].bv_val != NULL; i++ ) {
		rc = bdb_key_read( be, db, NULL, &keys[i], tmp );

		if( rc == DB_NOTFOUND ) {
			BDB_IDL_ZERO( ids );
			rc = 0;
			break;
		} else if( rc != LDAP_SUCCESS ) {
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_approx_candidates: key read failed (%d)\n", rc ));
#else
			Debug( LDAP_DEBUG_TRACE, "<= bdb_approx_candidates key read failed (%d)\n",
				rc, 0, 0 );
#endif
			break;
		}

		if( BDB_IDL_IS_ZERO( tmp ) ) {
#ifdef NEW_LOGGING
			LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_approx_candidates: NULL\n" ));
#else
			Debug( LDAP_DEBUG_TRACE, "<= bdb_approx_candidates NULL\n",
				0, 0, 0 );
#endif
			BDB_IDL_ZERO( ids );
			break;
		}

		bdb_idl_intersection( ids, tmp );

		if( BDB_IDL_IS_ZERO( ids ) )
			break;
	}

	ber_bvarray_free( keys );

#ifdef NEW_LOGGING
	LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_approx_candidates: id=%ld first=%ld last=%ld\n", (long) ids[0], (long) BDB_IDL_FIRST( ids ), (long) BDB_IDL_LAST( ids ) ));
#else
	Debug( LDAP_DEBUG_TRACE, "<= bdb_approx_candidates %ld, first=%ld, last=%ld\n",
		(long) ids[0],
		(long) BDB_IDL_FIRST(ids),
		(long) BDB_IDL_LAST(ids) );
#endif
	return( rc );
}

static int
substring_candidates(
	Backend	*be,
	SubstringsAssertion	*sub,
	ID *ids,
	ID *tmp )
{
	DB	*db;
	int i;
	int rc;
	slap_mask_t mask;
	struct berval prefix = {0};
	struct berval *keys = NULL;
	MatchingRule *mr;

#ifdef NEW_LOGGING
	LDAP_LOG (( "filterindex", LDAP_LEVEL_ENTRY, "=> bdb_substring_candidates\n"));
#else
	Debug( LDAP_DEBUG_TRACE, "=> bdb_substring_candidates\n", 0, 0, 0 );
#endif

	rc = bdb_index_param( be, sub->sa_desc, LDAP_FILTER_SUBSTRINGS,
		&db, &mask, &prefix );

	if( rc != LDAP_SUCCESS ) {
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_substring_candidates: index_param failed (%d)\n", rc ));
#else
		Debug( LDAP_DEBUG_ANY,
			"<= bdb_substring_candidates: index_param failed (%d)\n",
			rc, 0, 0 );
#endif
		return rc;
	}

	if ( db == NULL ) {
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_substring_candidates: not indexed\n"));
#else
		Debug( LDAP_DEBUG_ANY,
			"<= bdb_substring_candidates not indexed\n",
			0, 0, 0 );
#endif
		return -1;
	}

	mr = sub->sa_desc->ad_type->sat_substr;

	if( !mr ) {
		return -1;
	}

	if( !mr->smr_filter ) {
		return -1;
	}

	rc = (mr->smr_filter)(
		LDAP_FILTER_SUBSTRINGS,
		mask,
		sub->sa_desc->ad_type->sat_syntax,
		mr,
		&prefix,
		sub,
		&keys );

	if( rc != LDAP_SUCCESS ) {
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_substring_candidates: (%s) MR filter failed (%d)\n", sub->sa_desc->ad_cname.bv_val, rc ));
#else
		Debug( LDAP_DEBUG_TRACE,
			"<= bdb_substring_candidates: (%s) MR filter failed (%d)\n",
			sub->sa_desc->ad_cname.bv_val, rc, 0 );
#endif
		return rc;
	}

	if( keys == NULL ) {
#ifdef NEW_LOGGING
		LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_substring_candidates: (%s) MR filter failed (%d)\n", mask, sub->sa_desc->ad_cname.bv_val ));
#else
		Debug( LDAP_DEBUG_TRACE,
			"<= bdb_substring_candidates: (0x%04lx) no keys (%s)\n",
			mask, sub->sa_desc->ad_cname.bv_val, 0 );
#endif
		return 0;
	}

	for ( i= 0; keys[i].bv_val != NULL; i++ ) {
		rc = bdb_key_read( be, db, NULL, &keys[i], tmp );

		if( rc == DB_NOTFOUND ) {
			BDB_IDL_ZERO( ids );
			rc = 0;
			break;
		} else if( rc != LDAP_SUCCESS ) {
#ifdef NEW_LOGGING
			LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_substring_candidates: key read failed (%d)\n", rc));
#else
			Debug( LDAP_DEBUG_TRACE, "<= bdb_substring_candidates key read failed (%d)\n",
				rc, 0, 0 );
#endif
			break;
		}

		if( BDB_IDL_IS_ZERO( tmp ) ) {
#ifdef NEW_LOGGING
			LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_substring_candidates: NULL \n" ));
#else
			Debug( LDAP_DEBUG_TRACE, "<= bdb_substring_candidates NULL\n",
				0, 0, 0 );
#endif
			BDB_IDL_ZERO( ids );
			break;
		}

		bdb_idl_intersection( ids, tmp );

		if( BDB_IDL_IS_ZERO( ids ) )
			break;
	}

	ber_bvarray_free( keys );

#ifdef NEW_LOGGING
	LDAP_LOG (( "filterindex", LDAP_LEVEL_RESULTS, "<= bdb_substring_candidates: id=%ld first=%ld last=%ld\n", (long) ids[0], (long) BDB_IDL_FIRST( ids ), (long) BDB_IDL_LAST( ids ) ));
#else
	Debug( LDAP_DEBUG_TRACE, "<= bdb_substring_candidates %ld, first=%ld, last=%ld\n",
		(long) ids[0],
		(long) BDB_IDL_FIRST(ids),
		(long) BDB_IDL_LAST(ids) );
#endif
	return( 0 );
}

