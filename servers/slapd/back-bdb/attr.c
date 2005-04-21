/* attr.c - backend routines for dealing with attributes */
/* $OpenLDAP$ */
/* This work is part of OpenLDAP Software <http://www.openldap.org/>.
 *
 * Copyright 2000-2005 The OpenLDAP Foundation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted only as authorized by the OpenLDAP
 * Public License.
 *
 * A copy of this license is available in the file LICENSE in the
 * top-level directory of the distribution or, alternatively, at
 * <http://www.OpenLDAP.org/license.html>.
 */

#include "portable.h"

#include <stdio.h>

#include <ac/socket.h>
#include <ac/string.h>

#include "slap.h"
#include "back-bdb.h"
#include "lutil.h"

/* for the cache of attribute information (which are indexed, etc.) */
typedef struct bdb_attrinfo {
	AttributeDescription *ai_desc; /* attribute description cn;lang-en */
	slap_mask_t ai_indexmask;	/* how the attr is indexed	*/
#ifdef LDAP_COMP_MATCH
	ComponentReference* ai_cr; /*component indexing*/
#endif
} AttrInfo;

static int
ainfo_type_cmp(
	const void *v_desc,
	const void *v_a
)
{
	const AttributeDescription *desc = v_desc;
	const AttrInfo	*a = v_a;
	return SLAP_PTRCMP(desc, a->ai_desc);
}

static int
ainfo_cmp(
	const void	*v_a,
	const void	*v_b
)
{
	const AttrInfo *a = v_a, *b = v_b;
	return SLAP_PTRCMP(a->ai_desc, b->ai_desc);
}

#ifdef LDAP_COMP_MATCH
void
bdb_attr_comp_ref(
	struct bdb_info *bdb,
	AttributeDescription *desc,
	ComponentReference** cr )
{
	AttrInfo	*a;

	a = (AttrInfo *) avl_find( bdb->bi_attrs, desc, ainfo_type_cmp );
	
	*cr = a != NULL ? a->ai_cr : 0 ;
}
void
bdb_attr_mask_cr(
	struct bdb_info *bdb,
	AttributeDescription *desc,
	slap_mask_t *indexmask,
	ComponentReference** cr )
{
	AttrInfo	*a;

	a = (AttrInfo *) avl_find( bdb->bi_attrs, desc, ainfo_type_cmp );
	if ( a ) {
		*indexmask = a->ai_indexmask;
		*cr = a->ai_cr;
	} else {
		*indexmask = 0;
		*cr = NULL;
	}
}
#endif

void
bdb_attr_mask(
	struct bdb_info	*bdb,
	AttributeDescription *desc,
	slap_mask_t *indexmask )
{
	AttrInfo	*a;

	a = (AttrInfo *) avl_find( bdb->bi_attrs, desc, ainfo_type_cmp );
	
	*indexmask = a != NULL ? a->ai_indexmask : 0;
}

int
bdb_attr_index_config(
	struct bdb_info	*bdb,
	const char		*fname,
	int			lineno,
	int			argc,
	char		**argv )
{
	int rc;
	int	i;
	slap_mask_t mask;
	char **attrs;
	char **indexes = NULL;

	attrs = ldap_str2charray( argv[0], "," );

	if( attrs == NULL ) {
		fprintf( stderr, "%s: line %d: "
			"no attributes specified: %s\n",
			fname, lineno, argv[0] );
		return LDAP_PARAM_ERROR;
	}

	if ( argc > 1 ) {
		indexes = ldap_str2charray( argv[1], "," );

		if( indexes == NULL ) {
			fprintf( stderr, "%s: line %d: "
				"no indexes specified: %s\n",
				fname, lineno, argv[1] );
			return LDAP_PARAM_ERROR;
		}
	}

	if( indexes == NULL ) {
		mask = bdb->bi_defaultmask;

	} else {
		mask = 0;

		for ( i = 0; indexes[i] != NULL; i++ ) {
			slap_mask_t index;
			rc = slap_str2index( indexes[i], &index );

			if( rc != LDAP_SUCCESS ) {
				fprintf( stderr, "%s: line %d: "
					"index type \"%s\" undefined\n",
					fname, lineno, indexes[i] );
				return LDAP_PARAM_ERROR;
			}

			mask |= index;
		}
	}

	if( !mask ) {
		fprintf( stderr, "%s: line %d: "
			"no indexes selected\n",
			fname, lineno );
		return LDAP_PARAM_ERROR;
	}

	for ( i = 0; attrs[i] != NULL; i++ ) {
		AttrInfo	*a;
		AttributeDescription *ad;
		const char *text;
#ifdef LDAP_COMP_MATCH
		ComponentReference* cr = NULL;
		AttrInfo *a_cr = NULL;
#endif

		if( strcasecmp( attrs[i], "default" ) == 0 ) {
			bdb->bi_defaultmask |= mask;
			continue;
		}

#ifdef LDAP_COMP_MATCH
		if ( is_component_reference( attrs[i] ) ) {
			rc = extract_component_reference( attrs[i], &cr );
			if ( rc != LDAP_SUCCESS ) {
				fprintf( stderr, "%s: line %d: "
					"index component reference\"%s\" undefined\n",
					fname, lineno, attrs[i] );
				return rc;
			}
			cr->cr_indexmask = mask;
			/*
			 * After extracting a component reference
			 * only the name of a attribute will be remaining
			 */
		} else {
			cr = NULL;
		}
#endif
		a = (AttrInfo *) ch_malloc( sizeof(AttrInfo) );

#ifdef LDAP_COMP_MATCH
		a->ai_cr = NULL;
#endif
		ad = NULL;
		rc = slap_str2ad( attrs[i], &ad, &text );

		if( rc != LDAP_SUCCESS ) {
			fprintf( stderr, "%s: line %d: "
				"index attribute \"%s\" undefined\n",
				fname, lineno, attrs[i] );
			return rc;
		}

		if( slap_ad_is_binary( ad ) ) {
			fprintf( stderr, "%s: line %d: "
				"index of attribute \"%s\" disallowed\n",
				fname, lineno, attrs[i] );
			return LDAP_UNWILLING_TO_PERFORM;
		}

		if( IS_SLAP_INDEX( mask, SLAP_INDEX_APPROX ) && !(
			ad->ad_type->sat_approx
				&& ad->ad_type->sat_approx->smr_indexer
				&& ad->ad_type->sat_approx->smr_filter ) )
		{
			fprintf( stderr, "%s: line %d: "
				"approx index of attribute \"%s\" disallowed\n",
				fname, lineno, attrs[i] );
			return LDAP_INAPPROPRIATE_MATCHING;
		}

		if( IS_SLAP_INDEX( mask, SLAP_INDEX_EQUALITY ) && !(
			ad->ad_type->sat_equality
				&& ad->ad_type->sat_equality->smr_indexer
				&& ad->ad_type->sat_equality->smr_filter ) )
		{
			fprintf( stderr, "%s: line %d: "
				"equality index of attribute \"%s\" disallowed\n",
				fname, lineno, attrs[i] );
			return LDAP_INAPPROPRIATE_MATCHING;
		}

		if( IS_SLAP_INDEX( mask, SLAP_INDEX_SUBSTR ) && !(
			ad->ad_type->sat_substr
				&& ad->ad_type->sat_substr->smr_indexer
				&& ad->ad_type->sat_substr->smr_filter ) )
		{
			fprintf( stderr, "%s: line %d: "
				"substr index of attribute \"%s\" disallowed\n",
				fname, lineno, attrs[i] );
			return LDAP_INAPPROPRIATE_MATCHING;
		}

		Debug( LDAP_DEBUG_CONFIG, "index %s 0x%04lx\n",
			ad->ad_cname.bv_val, mask, 0 ); 

		a->ai_desc = ad;
		a->ai_indexmask = mask;
#ifdef LDAP_COMP_MATCH
		if ( cr ) {
			a_cr = avl_find( bdb->bi_attrs, ad, ainfo_type_cmp );
			if ( a_cr ) {
				/*
				 * AttrInfo is already in AVL
				 * just add the extracted component reference
				 * in the AttrInfo
				 */
				rc = insert_component_reference( cr, &a_cr->ai_cr );
				if ( rc != LDAP_SUCCESS) {
					fprintf( stderr, " error during inserting component reference in %s ", attrs[i]);
					return LDAP_PARAM_ERROR;
				}
				continue;
			} else {
				rc = insert_component_reference( cr, &a->ai_cr );
				if ( rc != LDAP_SUCCESS) {
					fprintf( stderr, " error during inserting component reference in %s ", attrs[i]);
					return LDAP_PARAM_ERROR;
				}
			}
		}
#endif
		rc = avl_insert( &bdb->bi_attrs, (caddr_t) a,
		                 ainfo_cmp, avl_dup_error );

		if( rc ) {
			fprintf( stderr, "%s: line %d: duplicate index definition "
				"for attr \"%s\" (ignored)\n",
				fname, lineno, attrs[i] );

			return LDAP_PARAM_ERROR;
		}
	}

	ldap_charray_free( attrs );
	if ( indexes != NULL ) ldap_charray_free( indexes );

	return LDAP_SUCCESS;
}

static int
bdb_attr_index_unparser( void *v1, void *v2 )
{
	AttrInfo *ai = v1;
	BerVarray *bva = v2;
	struct berval bv;
	char *ptr;

	slap_index2bvlen( ai->ai_indexmask, &bv );
	if ( bv.bv_len ) {
		bv.bv_len += ai->ai_desc->ad_cname.bv_len + 1;
		ptr = ch_malloc( bv.bv_len+1 );
		bv.bv_val = lutil_strcopy( ptr, ai->ai_desc->ad_cname.bv_val );
		*bv.bv_val++ = ' ';
		slap_index2bv( ai->ai_indexmask, &bv );
		bv.bv_val = ptr;
		ber_bvarray_add( bva, &bv );
	}
}

static AttributeDescription addef = { NULL, NULL, BER_BVC("default") };
static AttrInfo aidef = { &addef };

void
bdb_attr_index_unparse( struct bdb_info *bdb, BerVarray *bva )
{
	if ( bdb->bi_defaultmask ) {
		aidef.ai_indexmask = bdb->bi_defaultmask;
		bdb_attr_index_unparser( &aidef, bva );
	}
	avl_apply( bdb->bi_attrs, bdb_attr_index_unparser, bva, -1, AVL_INORDER );
}

static void
bdb_attrinfo_free( AttrInfo *ai )
{
#ifdef LDAP_COMP_MATCH
	free( ai->ai_cr );
#endif
	free( ai );
}

void
bdb_attr_index_destroy( Avlnode *tree )
{
	avl_free( tree, bdb_attrinfo_free );
}

void bdb_attr_index_free( struct bdb_info *bdb, AttributeDescription *ad )
{
	AttrInfo *ai;

	ai = avl_delete( &bdb->bi_attrs, ad, ainfo_type_cmp );
	if ( ai )
		bdb_attrinfo_free( ai );
}
