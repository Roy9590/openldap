/* $OpenLDAP$ */
/*
 * Copyright 1998-2003 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */
/*
 * Copyright (c) 1995 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

#include "portable.h"

#include <stdio.h>

#include <ac/socket.h>
#include <ac/string.h>
#include <ac/time.h>

#include "ldap_pvt.h"
#include "slap.h"
#ifdef LDAP_SLAPI
#include "slapi.h"
#endif
#include "lutil.h"


int
do_modify(
    Operation	*op,
    SlapReply	*rs )
{
	struct berval dn = { 0, NULL };
	char		*last;
	ber_tag_t	tag;
	ber_len_t	len;
	Modifications	*modlist = NULL;
	Modifications	**modtail = &modlist;
#ifdef LDAP_DEBUG
	Modifications *tmp;
#endif
#ifdef LDAP_SLAPI
	LDAPMod		**modv = NULL;
	Slapi_PBlock *pb = op->o_pb;
#endif
	int manageDSAit;

#ifdef NEW_LOGGING
	LDAP_LOG( OPERATION, ENTRY, "do_modify: enter\n", 0, 0, 0 );
#else
	Debug( LDAP_DEBUG_TRACE, "do_modify\n", 0, 0, 0 );
#endif

	/*
	 * Parse the modify request.  It looks like this:
	 *
	 *	ModifyRequest := [APPLICATION 6] SEQUENCE {
	 *		name	DistinguishedName,
	 *		mods	SEQUENCE OF SEQUENCE {
	 *			operation	ENUMERATED {
	 *				add	(0),
	 *				delete	(1),
	 *				replace	(2)
	 *			},
	 *			modification	SEQUENCE {
	 *				type	AttributeType,
	 *				values	SET OF AttributeValue
	 *			}
	 *		}
	 *	}
	 */

	if ( ber_scanf( op->o_ber, "{m" /*}*/, &dn ) == LBER_ERROR ) {
#ifdef NEW_LOGGING
		LDAP_LOG( OPERATION, ERR, "do_modify: ber_scanf failed\n", 0, 0, 0 );
#else
		Debug( LDAP_DEBUG_ANY, "do_modify: ber_scanf failed\n", 0, 0, 0 );
#endif

		send_ldap_discon( op, rs, LDAP_PROTOCOL_ERROR, "decoding error" );
		return SLAPD_DISCONNECT;
	}

#ifdef NEW_LOGGING
	LDAP_LOG( OPERATION, ARGS, "do_modify: dn (%s)\n", dn.bv_val, 0, 0 );
#else
	Debug( LDAP_DEBUG_ARGS, "do_modify: dn (%s)\n", dn.bv_val, 0, 0 );
#endif


	/* collect modifications & save for later */

	for ( tag = ber_first_element( op->o_ber, &len, &last );
	    tag != LBER_DEFAULT;
	    tag = ber_next_element( op->o_ber, &len, last ) )
	{
		ber_int_t mop;
		Modifications tmp, *mod;

#ifdef SLAP_NVALUES
		tmp.sml_nvalues = NULL;
#endif

		if ( ber_scanf( op->o_ber, "{i{m[W]}}", &mop,
		    &tmp.sml_type, &tmp.sml_values )
		    == LBER_ERROR )
		{
			send_ldap_discon( op, rs, LDAP_PROTOCOL_ERROR, "decoding modlist error" );
			rs->sr_err = SLAPD_DISCONNECT;
			goto cleanup;
		}

		mod = (Modifications *) ch_malloc( sizeof(Modifications) );
		mod->sml_op = mop;
		mod->sml_type = tmp.sml_type;
		mod->sml_values = tmp.sml_values;
#ifdef SLAP_NVALUES
		mod->sml_nvalues = NULL;
#endif
		mod->sml_desc = NULL;
		mod->sml_next = NULL;
		*modtail = mod;

		switch( mop ) {
		case LDAP_MOD_ADD:
			if ( mod->sml_values == NULL ) {
#ifdef NEW_LOGGING
				LDAP_LOG( OPERATION, ERR, 
					"do_modify: modify/add operation (%ld) requires values\n",
					(long)mop, 0, 0 );
#else
				Debug( LDAP_DEBUG_ANY,
					"do_modify: modify/add operation (%ld) requires values\n",
					(long) mop, 0, 0 );
#endif

				send_ldap_error( op, rs, LDAP_PROTOCOL_ERROR,
					"modify/add operation requires values" );
				goto cleanup;
			}

			/* fall through */

		case LDAP_MOD_DELETE:
		case LDAP_MOD_REPLACE:
			break;

		default: {
#ifdef NEW_LOGGING
				LDAP_LOG( OPERATION, ERR, 
					"do_modify: invalid modify operation (%ld)\n", (long)mop, 0, 0 );
#else
				Debug( LDAP_DEBUG_ANY,
					"do_modify: invalid modify operation (%ld)\n",
					(long) mop, 0, 0 );
#endif

				send_ldap_error( op, rs, LDAP_PROTOCOL_ERROR,
					"unrecognized modify operation" );
				goto cleanup;
			}
		}

		modtail = &mod->sml_next;
	}
	*modtail = NULL;

	if( get_ctrls( op, rs, 1 ) != LDAP_SUCCESS ) {
#ifdef NEW_LOGGING
		LDAP_LOG( OPERATION, ERR, "do_modify: get_ctrls failed\n", 0, 0, 0 );
#else
		Debug( LDAP_DEBUG_ANY, "do_modify: get_ctrls failed\n", 0, 0, 0 );
#endif

		goto cleanup;
	}

	rs->sr_err = dnPrettyNormal( NULL, &dn, &op->o_req_dn, &op->o_req_ndn );
	if( rs->sr_err != LDAP_SUCCESS ) {
#ifdef NEW_LOGGING
		LDAP_LOG( OPERATION, INFO, "do_modify: conn %d  invalid dn (%s)\n",
			op->o_connid, dn.bv_val, 0 );
#else
		Debug( LDAP_DEBUG_ANY,
			"do_modify: invalid dn (%s)\n", dn.bv_val, 0, 0 );
#endif
		send_ldap_error( op, rs, LDAP_INVALID_DN_SYNTAX, "invalid DN" );
		goto cleanup;
	}

	if( op->o_req_ndn.bv_len == 0 ) {
#ifdef NEW_LOGGING
		LDAP_LOG( OPERATION, ERR, 
			"do_modify: attempt to modify root DSE.\n",0, 0, 0 );
#else
		Debug( LDAP_DEBUG_ANY, "do_modify: root dse!\n", 0, 0, 0 );
#endif

		send_ldap_error( op, rs, LDAP_UNWILLING_TO_PERFORM,
			"modify upon the root DSE not supported" );
		goto cleanup;

	} else if ( bvmatch( &op->o_req_ndn, &global_schemandn ) ) {
#ifdef NEW_LOGGING
		LDAP_LOG( OPERATION, ERR,
			"do_modify: attempt to modify subschema subentry.\n" , 0, 0, 0  );
#else
		Debug( LDAP_DEBUG_ANY, "do_modify: subschema subentry!\n", 0, 0, 0 );
#endif

		send_ldap_error( op, rs, LDAP_UNWILLING_TO_PERFORM,
			"modification of subschema subentry not supported" );
		goto cleanup;
	}

#ifdef LDAP_DEBUG
#ifdef NEW_LOGGING
	LDAP_LOG( OPERATION, DETAIL1, "do_modify: modifications:\n", 0, 0, 0  );
#else
	Debug( LDAP_DEBUG_ARGS, "modifications:\n", 0, 0, 0 );
#endif

	for ( tmp = modlist; tmp != NULL; tmp = tmp->sml_next ) {
#ifdef NEW_LOGGING
		LDAP_LOG( OPERATION, DETAIL1, "\t%s:  %s\n", 
			tmp->sml_op == LDAP_MOD_ADD ?
			"add" : (tmp->sml_op == LDAP_MOD_DELETE ?
			"delete" : "replace"), tmp->sml_type.bv_val, 0 );

		if ( tmp->sml_values == NULL ) {
			LDAP_LOG( OPERATION, DETAIL1, "\t\tno values", 0, 0, 0 );
		} else if ( tmp->sml_values[0].bv_val == NULL ) {
			LDAP_LOG( OPERATION, DETAIL1, "\t\tzero values", 0, 0, 0 );
		} else if ( tmp->sml_values[1].bv_val == NULL ) {
			LDAP_LOG( OPERATION, DETAIL1, "\t\tone value", 0, 0, 0 );
		} else {
			LDAP_LOG( OPERATION, DETAIL1, "\t\tmultiple values", 0, 0, 0 );
		}

#else
		Debug( LDAP_DEBUG_ARGS, "\t%s: %s\n",
			tmp->sml_op == LDAP_MOD_ADD
				? "add" : (tmp->sml_op == LDAP_MOD_DELETE
					? "delete" : "replace"), tmp->sml_type.bv_val, 0 );

		if ( tmp->sml_values == NULL ) {
			Debug( LDAP_DEBUG_ARGS, "%s\n",
			   "\t\tno values", NULL, NULL );
		} else if ( tmp->sml_values[0].bv_val == NULL ) {
			Debug( LDAP_DEBUG_ARGS, "%s\n",
			   "\t\tzero values", NULL, NULL );
		} else if ( tmp->sml_values[1].bv_val == NULL ) {
			Debug( LDAP_DEBUG_ARGS, "%s, length %ld\n",
			   "\t\tone value", (long) tmp->sml_values[0].bv_len, NULL );
		} else {
			Debug( LDAP_DEBUG_ARGS, "%s\n",
			   "\t\tmultiple values", NULL, NULL );
		}
#endif
	}

	if ( StatslogTest( LDAP_DEBUG_STATS ) ) {
		char abuf[BUFSIZ/2], *ptr = abuf;
		int len = 0;

		Statslog( LDAP_DEBUG_STATS, "conn=%lu op=%lu MOD dn=\"%s\"\n",
			op->o_connid, op->o_opid, dn.bv_val, 0, 0 );

		for ( tmp = modlist; tmp != NULL; tmp = tmp->sml_next ) {
			if (len + 1 + tmp->sml_type.bv_len > sizeof(abuf)) {
				Statslog( LDAP_DEBUG_STATS, "conn=%lu op=%lu MOD attr=%s\n",
				    op->o_connid, op->o_opid, abuf, 0, 0 );
	    			len = 0;
				ptr = abuf;
			}
			if (len) {
				*ptr++ = ' ';
				len++;
			}
			ptr = lutil_strcopy(ptr, tmp->sml_type.bv_val);
			len += tmp->sml_type.bv_len;
		}
		if (len) {
			Statslog( LDAP_DEBUG_STATS, "conn=%lu op=%lu MOD attr=%s\n",
	    			op->o_connid, op->o_opid, abuf, 0, 0 );
		}
	}
#endif	/* LDAP_DEBUG */

	manageDSAit = get_manageDSAit( op );

	/*
	 * We could be serving multiple database backends.  Select the
	 * appropriate one, or send a referral to our "referral server"
	 * if we don't hold it.
	 */
	if ( (op->o_bd = select_backend( &op->o_req_ndn, manageDSAit, 0 )) == NULL ) {
		rs->sr_ref = referral_rewrite( default_referral,
			NULL, &op->o_req_dn, LDAP_SCOPE_DEFAULT );
		if (!rs->sr_ref) rs->sr_ref = default_referral;

		rs->sr_err = LDAP_REFERRAL;
		send_ldap_result( op, rs );

		if (rs->sr_ref != default_referral) ber_bvarray_free( rs->sr_ref );
		goto cleanup;
	}

	/* check restrictions */
	if( backend_check_restrictions( op, rs, NULL ) != LDAP_SUCCESS ) {
		send_ldap_result( op, rs );
		goto cleanup;
	}

	/* check for referrals */
	if( backend_check_referrals( op, rs ) != LDAP_SUCCESS ) {
		goto cleanup;
	}

#if defined( LDAP_SLAPI )
	slapi_x_pblock_set_operation( pb, op );
	slapi_pblock_set( pb, SLAPI_MODIFY_TARGET, (void *)dn.bv_val );
	slapi_pblock_set( pb, SLAPI_MANAGEDSAIT, (void *)manageDSAit );
	modv = slapi_x_modifications2ldapmods( &modlist );
	slapi_pblock_set( pb, SLAPI_MODIFY_MODS, (void *)modv );

	rs->sr_err = doPluginFNs( op->o_bd, SLAPI_PLUGIN_PRE_MODIFY_FN, pb );
	if ( rs->sr_err != 0 ) {
		/*
		 * A preoperation plugin failure will abort the
		 * entire operation.
		 */
#ifdef NEW_LOGGING
		LDAP_LOG( OPERATION, INFO, "do_modify: modify preoperation plugin "
				"failed\n", 0, 0, 0 );
#else
		Debug(LDAP_DEBUG_TRACE, "do_modify: modify preoperation plugin failed.\n",
				0, 0, 0);
#endif
		if ( slapi_pblock_get( pb, SLAPI_RESULT_CODE, (void *)&rs->sr_err ) != 0) {
			rs->sr_err = LDAP_OTHER;
		}
		slapi_x_free_ldapmods( modv );
		modv = NULL;
		goto cleanup;
	}

	/*
	 * It's possible that the preoperation plugin changed the
	 * modification array, so we need to convert it back to
	 * a Modification list.
	 *
	 * Calling slapi_x_modifications2ldapmods() destroyed modlist so
	 * we don't need to free it.
	 */
	slapi_pblock_get( pb, SLAPI_MODIFY_MODS, (void **)&modv );
	modlist = slapi_x_ldapmods2modifications( modv );

	/*
	 * NB: it is valid for the plugin to return no modifications
	 * (for example, a plugin might store some attributes elsewhere
	 * and remove them from the modification list; if only those
	 * attribute types were included in the modification request,
	 * then slapi_x_ldapmods2modifications() above will return
	 * NULL).
	 */
	if ( modlist == NULL ) {
		rs->sr_err = LDAP_SUCCESS;
		send_ldap_result( op, rs );
		goto cleanup;
	}
#endif /* defined( LDAP_SLAPI ) */

	/*
	 * do the modify if 1 && (2 || 3)
	 * 1) there is a modify function implemented in this backend;
	 * 2) this backend is master for what it holds;
	 * 3) it's a replica and the dn supplied is the update_ndn.
	 */
	if ( op->o_bd->be_modify ) {
		/* do the update here */
		int repl_user = be_isupdate( op->o_bd, &op->o_ndn );
#ifndef SLAPD_MULTIMASTER
		/* Multimaster slapd does not have to check for replicator dn
		 * because it accepts each modify request
		 */
		if ( !op->o_bd->be_update_ndn.bv_len || repl_user )
#endif
		{
			int update = op->o_bd->be_update_ndn.bv_len;
			char textbuf[SLAP_TEXT_BUFLEN];
			size_t textlen = sizeof textbuf;

			rs->sr_err = slap_mods_check( modlist, update, &rs->sr_text,
				textbuf, textlen );

			if( rs->sr_err != LDAP_SUCCESS ) {
				send_ldap_result( op, rs );
				goto cleanup;
			}

			if ( !repl_user ) {
				for( modtail = &modlist;
					*modtail != NULL;
					modtail = &(*modtail)->sml_next )
				{
					/* empty */
				}

				rs->sr_err = slap_mods_opattrs( op, modlist, modtail,
					&rs->sr_text, textbuf, textlen );
				if( rs->sr_err != LDAP_SUCCESS ) {
					send_ldap_result( op, rs );
					goto cleanup;
				}
			}

			op->orm_modlist = modlist;
			if ( (op->o_bd->be_modify)( op, rs ) == 0
#ifdef SLAPD_MULTIMASTER
				&& !repl_user
#endif
			) {
				/* but we log only the ones not from a replicator user */
				replog( op );
			}

#ifndef SLAPD_MULTIMASTER
		/* send a referral */
		} else {
			BerVarray defref = op->o_bd->be_update_refs
				? op->o_bd->be_update_refs : default_referral;
			rs->sr_ref = referral_rewrite( defref,
				NULL, &op->o_req_dn, LDAP_SCOPE_DEFAULT );

			if (!rs->sr_ref) rs->sr_ref = defref;
			rs->sr_err = LDAP_REFERRAL;
			send_ldap_result( op, rs );
			if (rs->sr_ref != defref) ber_bvarray_free( rs->sr_ref );
#endif
		}
	} else {
		send_ldap_error( op, rs, LDAP_UNWILLING_TO_PERFORM,
		    "operation not supported within namingContext" );
	}

#if defined( LDAP_SLAPI )
	if ( doPluginFNs( op->o_bd, SLAPI_PLUGIN_POST_MODIFY_FN, pb ) != 0 ) {
#ifdef NEW_LOGGING
		LDAP_LOG( OPERATION, INFO, "do_modify: modify postoperation plugins "
				"failed\n", 0, 0, 0 );
#else
		Debug(LDAP_DEBUG_TRACE, "do_modify: modify postoperation plugins "
				"failed.\n", 0, 0, 0);
#endif
	}
#endif /* defined( LDAP_SLAPI ) */

cleanup:
	free( op->o_req_dn.bv_val );
	free( op->o_req_ndn.bv_val );
	if ( modlist != NULL ) slap_mods_free( modlist );
#if defined( LDAP_SLAPI )
	if ( modv != NULL ) slapi_x_free_ldapmods( modv );
#endif
	return rs->sr_err;
}

/*
 * Do basic attribute type checking and syntax validation.
 */
int slap_mods_check(
	Modifications *ml,
	int update,
	const char **text,
	char *textbuf,
	size_t textlen )
{
	int rc;

	for( ; ml != NULL; ml = ml->sml_next ) {
		AttributeDescription *ad = NULL;

		/* convert to attribute description */
		rc = slap_bv2ad( &ml->sml_type, &ml->sml_desc, text );

		if( rc != LDAP_SUCCESS ) {
			snprintf( textbuf, textlen, "%s: %s",
				ml->sml_type.bv_val, *text );
			*text = textbuf;
			return rc;
		}

		ad = ml->sml_desc;

		if( slap_syntax_is_binary( ad->ad_type->sat_syntax )
			&& !slap_ad_is_binary( ad ))
		{
			/* attribute requires binary transfer */
			snprintf( textbuf, textlen,
				"%s: requires ;binary transfer",
				ml->sml_type.bv_val );
			*text = textbuf;
			return LDAP_UNDEFINED_TYPE;
		}

		if( !slap_syntax_is_binary( ad->ad_type->sat_syntax )
			&& slap_ad_is_binary( ad ))
		{
			/* attribute requires binary transfer */
			snprintf( textbuf, textlen,
				"%s: disallows ;binary transfer",
				ml->sml_type.bv_val );
			*text = textbuf;
			return LDAP_UNDEFINED_TYPE;
		}

		if( slap_ad_is_tag_range( ad )) {
			/* attribute requires binary transfer */
			snprintf( textbuf, textlen,
				"%s: inappropriate use of tag range option",
				ml->sml_type.bv_val );
			*text = textbuf;
			return LDAP_UNDEFINED_TYPE;
		}

		if (!update && is_at_no_user_mod( ad->ad_type )) {
			/* user modification disallowed */
			snprintf( textbuf, textlen,
				"%s: no user modification allowed",
				ml->sml_type.bv_val );
			*text = textbuf;
			return LDAP_CONSTRAINT_VIOLATION;
		}

		if ( is_at_obsolete( ad->ad_type ) &&
			( ml->sml_op == LDAP_MOD_ADD || ml->sml_values != NULL ) )
		{
			/*
			 * attribute is obsolete,
			 * only allow replace/delete with no values
			 */
			snprintf( textbuf, textlen,
				"%s: attribute is obsolete",
				ml->sml_type.bv_val );
			*text = textbuf;
			return LDAP_CONSTRAINT_VIOLATION;
		}

		/*
		 * check values
		 */
		if( ml->sml_values != NULL ) {
			ber_len_t nvals;
			slap_syntax_validate_func *validate =
				ad->ad_type->sat_syntax->ssyn_validate;
			slap_syntax_transform_func *pretty =
				ad->ad_type->sat_syntax->ssyn_pretty;
 
			if( !pretty && !validate ) {
				*text = "no validator for syntax";
				snprintf( textbuf, textlen,
					"%s: no validator for syntax %s",
					ml->sml_type.bv_val,
					ad->ad_type->sat_syntax->ssyn_oid );
				*text = textbuf;
				return LDAP_INVALID_SYNTAX;
			}

			/*
			 * check that each value is valid per syntax
			 *	and pretty if appropriate
			 */
			for( nvals = 0; ml->sml_values[nvals].bv_val; nvals++ ) {
				struct berval pval;
				if( pretty ) {
					rc = pretty( ad->ad_type->sat_syntax,
						&ml->sml_values[nvals], &pval );
				} else {
					rc = validate( ad->ad_type->sat_syntax,
						&ml->sml_values[nvals] );
				}

				if( rc != 0 ) {
					snprintf( textbuf, textlen,
						"%s: value #%ld invalid per syntax",
						ml->sml_type.bv_val, (long) nvals );
					*text = textbuf;
					return LDAP_INVALID_SYNTAX;
				}

				if( pretty ) {
					ber_memfree( ml->sml_values[nvals].bv_val );
					ml->sml_values[nvals] = pval;
				}
			}

			/*
			 * a rough single value check... an additional check is needed
			 * to catch add of single value to existing single valued attribute
			 */
			if ((ml->sml_op == LDAP_MOD_ADD || ml->sml_op == LDAP_MOD_REPLACE)
				&& nvals > 1 && is_at_single_value( ad->ad_type ))
			{
				snprintf( textbuf, textlen,
					"%s: multiple values provided",
					ml->sml_type.bv_val );
				*text = textbuf;
				return LDAP_CONSTRAINT_VIOLATION;
			}

#ifdef SLAP_NVALUES
			if( nvals && ad->ad_type->sat_equality &&
				ad->ad_type->sat_equality->smr_normalize )
			{
				ml->sml_nvalues = ch_malloc( (nvals+1)*sizeof(struct berval) );
				for( nvals = 0; ml->sml_values[nvals].bv_val; nvals++ ) {
					rc = ad->ad_type->sat_equality->smr_normalize(
						0,
						ad->ad_type->sat_syntax,
						ad->ad_type->sat_equality,
						&ml->sml_values[nvals], &ml->sml_nvalues[nvals] );
					if( rc ) {
#ifdef NEW_LOGGING
						LDAP_LOG( OPERATION, DETAIL1,
							"str2entry:  NULL (ssyn_normalize %d)\n",
							rc, 0, 0 );
#else
						Debug( LDAP_DEBUG_ANY,
							"<= str2entry NULL (ssyn_normalize %d)\n",
							rc, 0, 0 );
#endif
						snprintf( textbuf, textlen,
							"%s: value #%ld normalization failed",
							ml->sml_type.bv_val, (long) nvals );
						*text = textbuf;
						return rc;
					}
				}
				ml->sml_nvalues[nvals].bv_val = NULL;
				ml->sml_nvalues[nvals].bv_len = 0;
			}
#endif
		}
	}

	return LDAP_SUCCESS;
}

int slap_mods_opattrs(
	Operation *op,
	Modifications *mods,
	Modifications **modtail,
	const char **text,
	char *textbuf, size_t textlen )
{
	struct berval name, timestamp, csn;
#ifdef SLAP_NVALUES
	struct berval nname;
#endif
	char timebuf[ LDAP_LUTIL_GENTIME_BUFSIZE ];
	char csnbuf[ LDAP_LUTIL_CSNSTR_BUFSIZE ];
	Modifications *mod;

	int mop = op->o_tag == LDAP_REQ_ADD
		? LDAP_MOD_ADD : LDAP_MOD_REPLACE;

	assert( modtail != NULL );
	assert( *modtail == NULL );

	if( SLAP_LASTMOD(op->o_bd) ) {
		struct tm *ltm;
		time_t now = slap_get_time();

		ldap_pvt_thread_mutex_lock( &gmtime_mutex );
		ltm = gmtime( &now );
		lutil_gentime( timebuf, sizeof(timebuf), ltm );

		csn.bv_len = lutil_csnstr( csnbuf, sizeof( csnbuf ), 0, 0 );
		ldap_pvt_thread_mutex_unlock( &gmtime_mutex );
		csn.bv_val = csnbuf;

		timestamp.bv_val = timebuf;
		timestamp.bv_len = strlen(timebuf);

		if( op->o_dn.bv_len == 0 ) {
			name.bv_val = SLAPD_ANONYMOUS;
			name.bv_len = sizeof(SLAPD_ANONYMOUS)-1;
#ifdef SLAP_NVALUES
			nname = name;
#endif
		} else {
			name = op->o_dn;
#ifdef SLAP_NVALUES
			nname = op->o_ndn;
#endif
		}
	}

	if( op->o_tag == LDAP_REQ_ADD ) {
		struct berval tmpval;

		if( global_schemacheck ) {
			int rc = mods_structural_class( mods, &tmpval,
				text, textbuf, textlen );
			if( rc != LDAP_SUCCESS ) {
				return rc;
			}

			mod = (Modifications *) ch_malloc( sizeof( Modifications ) );
			mod->sml_op = mop;
			mod->sml_type.bv_val = NULL;
			mod->sml_desc = slap_schema.si_ad_structuralObjectClass;
			mod->sml_values =
				(BerVarray) ch_malloc( 2 * sizeof( struct berval ) );
			ber_dupbv( &mod->sml_values[0], &tmpval );
			mod->sml_values[1].bv_len = 0;
			mod->sml_values[1].bv_val = NULL;
			assert( mod->sml_values[0].bv_val );
#ifdef SLAP_NVALUES
			mod->sml_nvalues =
				(BerVarray) ch_malloc( 2 * sizeof( struct berval ) );
			ber_dupbv( &mod->sml_nvalues[0], &tmpval );
			mod->sml_nvalues[1].bv_len = 0;
			mod->sml_nvalues[1].bv_val = NULL;
			assert( mod->sml_nvalues[0].bv_val );
#endif
			*modtail = mod;
			modtail = &mod->sml_next;
		}

		if( SLAP_LASTMOD(op->o_bd) ) {
			char uuidbuf[ LDAP_LUTIL_UUIDSTR_BUFSIZE ];

			tmpval.bv_len = lutil_uuidstr( uuidbuf, sizeof( uuidbuf ) );
			tmpval.bv_val = uuidbuf;
		
			mod = (Modifications *) ch_malloc( sizeof( Modifications ) );
			mod->sml_op = mop;
			mod->sml_type.bv_val = NULL;
			mod->sml_desc = slap_schema.si_ad_entryUUID;
			mod->sml_values =
				(BerVarray) ch_malloc( 2 * sizeof( struct berval ) );
			ber_dupbv( &mod->sml_values[0], &tmpval );
			mod->sml_values[1].bv_len = 0;
			mod->sml_values[1].bv_val = NULL;
			assert( mod->sml_values[0].bv_val );
#ifdef SLAP_NVALUES
			mod->sml_nvalues = NULL;
#endif
			*modtail = mod;
			modtail = &mod->sml_next;

			mod = (Modifications *) ch_malloc( sizeof( Modifications ) );
			mod->sml_op = mop;
			mod->sml_type.bv_val = NULL;
			mod->sml_desc = slap_schema.si_ad_creatorsName;
			mod->sml_values = (BerVarray) ch_malloc( 2 * sizeof( struct berval ) );
			ber_dupbv( &mod->sml_values[0], &name );
			mod->sml_values[1].bv_len = 0;
			mod->sml_values[1].bv_val = NULL;
			assert( mod->sml_values[0].bv_val );
#ifdef SLAP_NVALUES
			mod->sml_nvalues =
				(BerVarray) ch_malloc( 2 * sizeof( struct berval ) );
			ber_dupbv( &mod->sml_nvalues[0], &nname );
			mod->sml_nvalues[1].bv_len = 0;
			mod->sml_nvalues[1].bv_val = NULL;
			assert( mod->sml_nvalues[0].bv_val );
#endif
			*modtail = mod;
			modtail = &mod->sml_next;

			mod = (Modifications *) ch_malloc( sizeof( Modifications ) );
			mod->sml_op = mop;
			mod->sml_type.bv_val = NULL;
			mod->sml_desc = slap_schema.si_ad_createTimestamp;
			mod->sml_values = (BerVarray) ch_malloc( 2 * sizeof( struct berval ) );
			ber_dupbv( &mod->sml_values[0], &timestamp );
			mod->sml_values[1].bv_len = 0;
			mod->sml_values[1].bv_val = NULL;
			assert( mod->sml_values[0].bv_val );
#ifdef SLAP_NVALUES
			mod->sml_nvalues = NULL;
#endif
			*modtail = mod;
			modtail = &mod->sml_next;
		}
	}

	if( SLAP_LASTMOD(op->o_bd) ) {
		mod = (Modifications *) ch_malloc( sizeof( Modifications ) );
		mod->sml_op = mop;
		mod->sml_type.bv_val = NULL;
		mod->sml_desc = slap_schema.si_ad_entryCSN;
		mod->sml_values = (BerVarray) ch_malloc( 2 * sizeof( struct berval ) );
		ber_dupbv( &mod->sml_values[0], &csn );
		mod->sml_values[1].bv_len = 0;
		mod->sml_values[1].bv_val = NULL;
		assert( mod->sml_values[0].bv_val );
#ifdef SLAP_NVALUES
		mod->sml_nvalues = NULL;
#endif
		*modtail = mod;
		modtail = &mod->sml_next;

		mod = (Modifications *) ch_malloc( sizeof( Modifications ) );
		mod->sml_op = mop;
		mod->sml_type.bv_val = NULL;
		mod->sml_desc = slap_schema.si_ad_modifiersName;
		mod->sml_values = (BerVarray) ch_malloc( 2 * sizeof( struct berval ) );
		ber_dupbv( &mod->sml_values[0], &name );
		mod->sml_values[1].bv_len = 0;
		mod->sml_values[1].bv_val = NULL;
		assert( mod->sml_values[0].bv_val );
#ifdef SLAP_NVALUES
		mod->sml_nvalues =
			(BerVarray) ch_malloc( 2 * sizeof( struct berval ) );
		ber_dupbv( &mod->sml_nvalues[0], &nname );
		mod->sml_nvalues[1].bv_len = 0;
		mod->sml_nvalues[1].bv_val = NULL;
		assert( mod->sml_nvalues[0].bv_val );
#endif
		*modtail = mod;
		modtail = &mod->sml_next;

		mod = (Modifications *) ch_malloc( sizeof( Modifications ) );
		mod->sml_op = mop;
		mod->sml_type.bv_val = NULL;
		mod->sml_desc = slap_schema.si_ad_modifyTimestamp;
		mod->sml_values = (BerVarray) ch_malloc( 2 * sizeof( struct berval ) );
		ber_dupbv( &mod->sml_values[0], &timestamp );
		mod->sml_values[1].bv_len = 0;
		mod->sml_values[1].bv_val = NULL;
		assert( mod->sml_values[0].bv_val );
#ifdef SLAP_NVALUES
		mod->sml_nvalues = NULL;
#endif
		*modtail = mod;
		modtail = &mod->sml_next;
	}

	*modtail = NULL;
	return LDAP_SUCCESS;
}

