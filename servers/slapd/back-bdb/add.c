/* add.c - ldap BerkeleyDB back-end add routine */
/* $OpenLDAP$ */
/*
 * Copyright 1998-2002 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */

#include "portable.h"

#include <stdio.h>
#include <ac/string.h>

#include "back-bdb.h"
#include "external.h"

int
bdb_add(
	BackendDB	*be,
	Connection	*conn,
	Operation	*op,
	Entry	*e )
{
	struct bdb_info *bdb = (struct bdb_info *) be->be_private;
	struct berval	pdn;
	Entry		*p = NULL;
	int			rc; 
	const char	*text;
	char textbuf[SLAP_TEXT_BUFLEN];
	size_t textlen = sizeof textbuf;
	AttributeDescription *children = slap_schema.si_ad_children;
	DB_TXN		*ltid = NULL;
	struct bdb_op_info opinfo;

	Debug(LDAP_DEBUG_ARGS, "==> bdb_add: %s\n", e->e_dn, 0, 0);

	/* check entry's schema */
	rc = entry_schema_check( e, NULL, &text, textbuf, textlen );
	if ( rc != LDAP_SUCCESS ) {
		Debug( LDAP_DEBUG_TRACE,
			"bdb_add: entry failed schema check: %s (%d)\n",
			text, rc, 0 );
		goto return_results;
	}

	/*
	 * acquire an ID outside of the operation transaction
	 * to avoid serializing adds.
	 */
	rc = bdb_next_id( be, NULL, &e->e_id );
	if( rc != 0 ) {
		Debug( LDAP_DEBUG_TRACE,
			"bdb_add: next_id failed (%d)\n",
			rc, 0, 0 );
		rc = LDAP_OTHER;
		text = "internal error";
		goto return_results;
	}

	if( 0 ) {
		/* transaction retry */
retry:	rc = txn_abort( ltid );
		ltid = NULL;
		op->o_private = NULL;
		if( rc != 0 ) {
			rc = LDAP_OTHER;
			text = "internal error";
			goto return_results;
		}
	}

	/* begin transaction */
	if( bdb->bi_txn ) {
		rc = txn_begin( bdb->bi_dbenv, NULL, &ltid, 
			bdb->bi_db_opflags );
		text = NULL;
		if( rc != 0 ) {
			Debug( LDAP_DEBUG_TRACE,
				"bdb_add: txn_begin failed: %s (%d)\n",
				db_strerror(rc), rc, 0 );
			rc = LDAP_OTHER;
			text = "internal error";
			goto return_results;
		}
	}

	opinfo.boi_bdb = be;
	opinfo.boi_txn = ltid;
	opinfo.boi_err = 0;
	op->o_private = &opinfo;
	
	/*
	 * Get the parent dn and see if the corresponding entry exists.
	 * If the parent does not exist, only allow the "root" user to
	 * add the entry.
	 */
	pdn.bv_val = dn_parent( be, e->e_ndn );
	if (pdn.bv_val && *pdn.bv_val)
		pdn.bv_len = e->e_nname.bv_len - (pdn.bv_val - e->e_ndn);
	else
		pdn.bv_len = 0;

	if( pdn.bv_len != 0 ) {
		Entry *matched = NULL;

		/* get parent */
		rc = bdb_dn2entry( be, ltid, &pdn, &p, &matched, 0 );

		switch( rc ) {
		case 0:
		case DB_NOTFOUND:
			break;
		case DB_LOCK_DEADLOCK:
		case DB_LOCK_NOTGRANTED:
			goto retry;
		default:
			rc = LDAP_OTHER;
			text = "internal error";
			goto return_results;
		}

		if ( p == NULL ) {
			char *matched_dn = NULL;
			BVarray refs;

			if ( matched != NULL ) {
				matched_dn = ch_strdup( matched->e_dn );
				refs = is_entry_referral( matched )
					? get_entry_referrals( be, conn, op, matched )
					: NULL;
				bdb_entry_return( be, matched );
				matched = NULL;

			} else {
				refs = referral_rewrite( default_referral,
					NULL, &e->e_name, LDAP_SCOPE_DEFAULT );
			}

			Debug( LDAP_DEBUG_TRACE, "bdb_add: parent does not exist\n",
				0, 0, 0 );

			send_ldap_result( conn, op, rc = LDAP_REFERRAL,
				matched_dn, NULL, refs, NULL );

			bvarray_free( refs );
			ch_free( matched_dn );

			goto done;
		}

		if ( ! access_allowed( be, conn, op, p,
			children, NULL, ACL_WRITE ) )
		{
			Debug( LDAP_DEBUG_TRACE, "bdb_add: no write access to parent\n",
				0, 0, 0 );
			rc = LDAP_INSUFFICIENT_ACCESS;
			text = "no write access to parent";
			goto return_results;;
		}

		if ( is_entry_alias( p ) ) {
			/* parent is an alias, don't allow add */
			Debug( LDAP_DEBUG_TRACE, "bdb_add: parent is alias\n",
				0, 0, 0 );
			rc = LDAP_ALIAS_PROBLEM;
			text = "parent is an alias";
			goto return_results;;
		}

		if ( is_entry_referral( p ) ) {
			/* parent is a referral, don't allow add */
			char *matched_dn = ch_strdup( p->e_dn );
			BVarray refs = is_entry_referral( p )
				? get_entry_referrals( be, conn, op, p )
				: NULL;

			Debug( LDAP_DEBUG_TRACE, "bdb_add: parent is referral\n",
				0, 0, 0 );

			send_ldap_result( conn, op, rc = LDAP_REFERRAL,
				matched_dn, NULL, refs, NULL );

			bvarray_free( refs );
			free( matched_dn );
			goto done;
		}

		/* free parent and writer lock */
		bdb_entry_return( be, p );
		p = NULL;

	} else {
		/*
		 * no parent!
		 *	must be adding entry to at suffix
		 *  or with parent ""
		 */
		if ( !be_isroot( be, &op->o_ndn )) {
			if ( be_issuffix( be, "" ) || be_isupdate( be, &op->o_ndn ) ) {
				p = (Entry *)&slap_entry_root;

				/* check parent for "children" acl */
				rc = access_allowed( be, conn, op, p,
					children, NULL, ACL_WRITE );
				p = NULL;

				if ( ! rc ) {
					Debug( LDAP_DEBUG_TRACE,
						"bdb_add: no write access to parent\n",
						0, 0, 0 );
					rc = LDAP_INSUFFICIENT_ACCESS;
					text = "no write access to parent";
					goto return_results;;
				}

			} else {
				Debug( LDAP_DEBUG_TRACE, "bdb_add: %s denied\n",
					pdn.bv_len == 0 ? "suffix" : "entry at root",
					0, 0 );
				rc = LDAP_INSUFFICIENT_ACCESS;
				goto return_results;
			}
		}
	}

	/* dn2id index */
	rc = bdb_dn2id_add( be, ltid, &pdn, e );
	if ( rc != 0 ) {
		Debug( LDAP_DEBUG_TRACE, "bdb_add: dn2id_add failed: %s (%d)\n",
			db_strerror(rc), rc, 0 );

		switch( rc ) {
		case DB_LOCK_DEADLOCK:
		case DB_LOCK_NOTGRANTED:
			goto retry;
		case DB_KEYEXIST:
			rc = LDAP_ALREADY_EXISTS;
			break;
		default:
			rc = LDAP_OTHER;
		}
		goto return_results;
	}

	/* id2entry index */
	rc = bdb_id2entry_add( be, ltid, e );
	if ( rc != 0 ) {
		Debug( LDAP_DEBUG_TRACE, "bdb_add: id2entry_add failed\n",
			0, 0, 0 );
		switch( rc ) {
		case DB_LOCK_DEADLOCK:
		case DB_LOCK_NOTGRANTED:
			goto retry;
		default:
			rc = LDAP_OTHER;
		}
		text = "entry store failed";
		goto return_results;
	}

	/* attribute indexes */
	rc = bdb_index_entry_add( be, ltid, e, e->e_attrs );
	if ( rc != LDAP_SUCCESS ) {
		Debug( LDAP_DEBUG_TRACE, "bdb_add: index_entry_add failed\n",
			0, 0, 0 );
		switch( rc ) {
		case DB_LOCK_DEADLOCK:
		case DB_LOCK_NOTGRANTED:
			goto retry;
		default:
			rc = LDAP_OTHER;
		}
		text = "index generation failed";
		goto return_results;
	}

	if( bdb->bi_txn ) {
		rc = txn_commit( ltid, 0 );
	}
	ltid = NULL;
	op->o_private = NULL;

	if( rc != 0 ) {
		Debug( LDAP_DEBUG_TRACE,
			"bdb_add: txn_commit failed: %s (%d)\n",
			db_strerror(rc), rc, 0 );
		rc = LDAP_OTHER;
		text = "commit failed";

	} else {
		Debug( LDAP_DEBUG_TRACE,
			"bdb_add: added id=%08lx dn=\"%s\"\n",
			e->e_id, e->e_dn, 0 );
		rc = LDAP_SUCCESS;
		text = NULL;
	}

return_results:
	send_ldap_result( conn, op, rc,
		NULL, text, NULL, NULL );

	if( rc == LDAP_SUCCESS && bdb->bi_txn_cp ) {
		ldap_pvt_thread_yield();
		TXN_CHECKPOINT( bdb->bi_dbenv,
			bdb->bi_txn_cp_kbyte, bdb->bi_txn_cp_min, 0 );
	}

done:
	if (p != NULL) {
		/* free parent and writer lock */
		bdb_entry_return( be, p ); 
	}

	if( ltid != NULL ) {
		txn_abort( ltid );
		op->o_private = NULL;
	}

	return rc;
}
