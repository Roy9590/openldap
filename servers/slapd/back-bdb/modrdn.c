/* modrdn.c - bdb backend modrdn routine */
/* $OpenLDAP$ */
/*
 * Copyright 1998-2003 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */

#include "portable.h"

#include <stdio.h>
#include <ac/string.h>

#include "back-bdb.h"
#include "external.h"

int
bdb_modrdn( Operation	*op, SlapReply *rs )
{
	struct bdb_info *bdb = (struct bdb_info *) op->o_bd->be_private;
	AttributeDescription *children = slap_schema.si_ad_children;
	AttributeDescription *entry = slap_schema.si_ad_entry;
	struct berval	p_dn, p_ndn;
	struct berval	new_dn = {0, NULL}, new_ndn = {0, NULL};
	int		isroot = -1;
	Entry		*e = NULL;
	Entry		*p = NULL;
	Entry		*matched;
	/* LDAP v2 supporting correct attribute handling. */
	LDAPRDN		new_rdn = NULL;
	LDAPRDN		old_rdn = NULL;
	char textbuf[SLAP_TEXT_BUFLEN];
	size_t textlen = sizeof textbuf;
	DB_TXN *	ltid = NULL;
	struct bdb_op_info opinfo;

	ID			id;

	Entry		*np = NULL;			/* newSuperior Entry */
	struct berval	*np_dn = NULL;			/* newSuperior dn */
	struct berval	*np_ndn = NULL;			/* newSuperior ndn */
	struct berval	*new_parent_dn = NULL;	/* np_dn, p_dn, or NULL */

	/* Used to interface with bdb_modify_internal() */
	Modifications	*mod = NULL;		/* Used to delete old rdn */

	int		manageDSAit = get_manageDSAit( op );

	u_int32_t	locker = 0;
	DB_LOCK		lock;

	int		noop = 0;

#if defined(LDAP_CLIENT_UPDATE) || defined(LDAP_SYNC)
        Operation *ps_list;
	struct psid_entry *pm_list, *pm_prev;
#endif

#ifdef NEW_LOGGING
	LDAP_LOG ( OPERATION, ENTRY, "==>bdb_modrdn(%s,%s,%s)\n", 
		op->o_req_dn.bv_val,op->oq_modrdn.rs_newrdn.bv_val,
		op->oq_modrdn.rs_newSup ? op->oq_modrdn.rs_newSup->bv_val : "NULL" );
#else
	Debug( LDAP_DEBUG_TRACE, "==>bdb_modrdn(%s,%s,%s)\n",
		op->o_req_dn.bv_val,op->oq_modrdn.rs_newrdn.bv_val,
		op->oq_modrdn.rs_newSup ? op->oq_modrdn.rs_newSup->bv_val : "NULL" );
#endif

	if( 0 ) {
retry:	/* transaction retry */
		if (e != NULL) {
			bdb_cache_delete_entry(&bdb->bi_cache, e);
			bdb_unlocked_cache_return_entry_w(&bdb->bi_cache, e);
			e = NULL;
		}
		if (p != NULL) {
			bdb_unlocked_cache_return_entry_r(&bdb->bi_cache, p);
			p = NULL;
		}
		if (np != NULL) {
			bdb_unlocked_cache_return_entry_r(&bdb->bi_cache, np);
			np = NULL;
		}
#ifdef NEW_LOGGING
		LDAP_LOG ( OPERATION, DETAIL1, "==>bdb_modrdn: retrying...\n", 0, 0, 0);
#else
		Debug( LDAP_DEBUG_TRACE, "==>bdb_modrdn: retrying...\n", 0, 0, 0 );
#endif

#if defined(LDAP_CLIENT_UPDATE) || defined(LDAP_SYNC)
                pm_list = LDAP_LIST_FIRST(&op->o_pm_list);
                while ( pm_list != NULL ) {
                        LDAP_LIST_REMOVE ( pm_list, ps_link );
			pm_prev = pm_list;
                        pm_list = LDAP_LIST_NEXT ( pm_list, ps_link );
			ch_free( pm_prev );
                }
#endif

		rs->sr_err = TXN_ABORT( ltid );
		ltid = NULL;
		op->o_private = NULL;
		op->o_do_not_cache = opinfo.boi_acl_cache;
		if( rs->sr_err != 0 ) {
			rs->sr_err = LDAP_OTHER;
			rs->sr_text = "internal error";
			goto return_results;
		}
		ldap_pvt_thread_yield();
	}

	/* begin transaction */
	rs->sr_err = TXN_BEGIN( bdb->bi_dbenv, NULL, &ltid, 
		bdb->bi_db_opflags );
	rs->sr_text = NULL;
	if( rs->sr_err != 0 ) {
#ifdef NEW_LOGGING
		LDAP_LOG ( OPERATION, ERR, 
			"==>bdb_modrdn: txn_begin failed: %s (%d)\n", 
			db_strerror(rs->sr_err), rs->sr_err, 0 );
#else
		Debug( LDAP_DEBUG_TRACE,
			"bdb_delete: txn_begin failed: %s (%d)\n",
			db_strerror(rs->sr_err), rs->sr_err, 0 );
#endif
		rs->sr_err = LDAP_OTHER;
		rs->sr_text = "internal error";
		goto return_results;
	}

	locker = TXN_ID ( ltid );

	opinfo.boi_bdb = op->o_bd;
	opinfo.boi_txn = ltid;
	opinfo.boi_locker = locker;
	opinfo.boi_err = 0;
	opinfo.boi_acl_cache = op->o_do_not_cache;
	op->o_private = &opinfo;

	/* get entry */
	rs->sr_err = bdb_dn2entry_w( op->o_bd, ltid, &op->o_req_ndn, &e, &matched, DB_RMW, locker, &lock );

	switch( rs->sr_err ) {
	case 0:
	case DB_NOTFOUND:
		break;
	case DB_LOCK_DEADLOCK:
	case DB_LOCK_NOTGRANTED:
		goto retry;
	case LDAP_BUSY:
		rs->sr_text = "ldap server busy";
		goto return_results;
	default:
		rs->sr_err = LDAP_OTHER;
		rs->sr_text = "internal error";
		goto return_results;
	}

	if ( e == NULL ) {
		if( matched != NULL ) {
			rs->sr_matched = ch_strdup( matched->e_dn );
			rs->sr_ref = is_entry_referral( matched )
				? get_entry_referrals( op, matched )
				: NULL;
			bdb_unlocked_cache_return_entry_r( &bdb->bi_cache, matched);
			matched = NULL;

		} else {
			rs->sr_ref = referral_rewrite( default_referral,
				NULL, &op->o_req_dn, LDAP_SCOPE_DEFAULT );
		}

		rs->sr_err = LDAP_REFERRAL;
		send_ldap_result( op, rs );

		ber_bvarray_free( rs->sr_ref );
		free( (char *)rs->sr_matched );
		rs->sr_ref = NULL;
		rs->sr_matched = NULL;

		goto done;
	}

	/* check write on old entry */
	rs->sr_err = access_allowed( op, e, entry, NULL, ACL_WRITE, NULL );

	if ( ! rs->sr_err ) {
		switch( opinfo.boi_err ) {
		case DB_LOCK_DEADLOCK:
		case DB_LOCK_NOTGRANTED:
			goto retry;
		}

#ifdef NEW_LOGGING
		LDAP_LOG ( OPERATION, ERR, 
			"==>bdb_modrdn: no access to entry\n", 0, 0, 0 );
#else
		Debug( LDAP_DEBUG_TRACE, "no access to entry\n", 0,
			0, 0 );
#endif
		rs->sr_text = "no write access to old entry";
		rs->sr_err = LDAP_INSUFFICIENT_ACCESS;
		goto return_results;
	}

#ifndef BDB_HIER
	rs->sr_err = bdb_dn2id_children( op->o_bd, ltid, &e->e_nname, 0 );
	if ( rs->sr_err != DB_NOTFOUND ) {
		switch( rs->sr_err ) {
		case DB_LOCK_DEADLOCK:
		case DB_LOCK_NOTGRANTED:
			goto retry;
		case 0:
#ifdef NEW_LOGGING
			LDAP_LOG ( OPERATION, DETAIL1, 
				"<=- bdb_modrdn: non-leaf %s\n", op->o_req_dn.bv_val, 0, 0 );
#else
			Debug(LDAP_DEBUG_ARGS,
				"<=- bdb_modrdn: non-leaf %s\n",
				op->o_req_dn.bv_val, 0, 0);
#endif
			rs->sr_err = LDAP_NOT_ALLOWED_ON_NONLEAF;
			rs->sr_text = "subtree rename not supported";
			break;
		default:
#ifdef NEW_LOGGING
			LDAP_LOG ( OPERATION, ERR, 
				"<=- bdb_modrdn: has_children failed %s (%d)\n",
				db_strerror(rs->sr_err), rs->sr_err, 0 );
#else
			Debug(LDAP_DEBUG_ARGS,
				"<=- bdb_modrdn: has_children failed: %s (%d)\n",
				db_strerror(rs->sr_err), rs->sr_err, 0 );
#endif
			rs->sr_err = LDAP_OTHER;
			rs->sr_text = "internal error";
		}
		goto return_results;
	}
#endif
	if (!manageDSAit && is_entry_referral( e ) ) {
		/* parent is a referral, don't allow add */
		rs->sr_ref = get_entry_referrals( op, e );

#ifdef NEW_LOGGING
		LDAP_LOG ( OPERATION, DETAIL1, 
			"==>bdb_modrdn: entry %s is referral \n", e->e_dn, 0, 0 );
#else
		Debug( LDAP_DEBUG_TRACE, "bdb_modrdn: entry %s is referral\n",
			e->e_dn, 0, 0 );
#endif

		rs->sr_err = LDAP_REFERRAL,
		rs->sr_matched = e->e_name.bv_val;
		send_ldap_result( op, rs );

		ber_bvarray_free( rs->sr_ref );
		rs->sr_ref = NULL;
		rs->sr_matched = NULL;
		goto done;
	}

	if ( be_issuffix( op->o_bd, &e->e_nname ) ) {
		p_ndn = slap_empty_bv;
	} else {
		dnParent( &e->e_nname, &p_ndn );
	}
	np_ndn = &p_ndn;
	if ( p_ndn.bv_len != 0 ) {
		/* Make sure parent entry exist and we can write its 
		 * children.
		 */
		rs->sr_err = bdb_dn2entry_r( op->o_bd, ltid, &p_ndn, &p, NULL, 0, locker, &lock );

		switch( rs->sr_err ) {
		case 0:
		case DB_NOTFOUND:
			break;
		case DB_LOCK_DEADLOCK:
		case DB_LOCK_NOTGRANTED:
			goto retry;
		case LDAP_BUSY:
			rs->sr_text = "ldap server busy";
			goto return_results;
		default:
			rs->sr_err = LDAP_OTHER;
			rs->sr_text = "internal error";
			goto return_results;
		}

		if( p == NULL) {
#ifdef NEW_LOGGING
			LDAP_LOG ( OPERATION, ERR, 
				"==>bdb_modrdn: parent does not exist\n", 0, 0, 0 );
#else
			Debug( LDAP_DEBUG_TRACE, "bdb_modrdn: parent does not exist\n",
				0, 0, 0);
#endif
			rs->sr_err = LDAP_OTHER;
			rs->sr_text = "old entry's parent does not exist";
			goto return_results;
		}

		/* check parent for "children" acl */
		rs->sr_err = access_allowed( op, p,
			children, NULL, ACL_WRITE, NULL );

		if ( ! rs->sr_err ) {
			switch( opinfo.boi_err ) {
			case DB_LOCK_DEADLOCK:
			case DB_LOCK_NOTGRANTED:
				goto retry;
			}

			rs->sr_err = LDAP_INSUFFICIENT_ACCESS;
#ifdef NEW_LOGGING
			LDAP_LOG ( OPERATION, ERR, 
				"==>bdb_modrdn: no access to parent\n", 0, 0, 0 );
#else
			Debug( LDAP_DEBUG_TRACE, "no access to parent\n", 0,
				0, 0 );
#endif
			rs->sr_text = "no write access to old parent's children";
			goto return_results;
		}

#ifdef NEW_LOGGING
		LDAP_LOG ( OPERATION, DETAIL1, 
			"==>bdb_modrdn: wr to children %s is OK\n", p_ndn.bv_val, 0, 0 );
#else
		Debug( LDAP_DEBUG_TRACE,
			"bdb_modrdn: wr to children of entry %s OK\n",
			p_ndn.bv_val, 0, 0 );
#endif
		
		if ( p_ndn.bv_val == slap_empty_bv.bv_val ) {
			p_dn = slap_empty_bv;
		} else {
			dnParent( &e->e_name, &p_dn );
		}

#ifdef NEW_LOGGING
		LDAP_LOG ( OPERATION, DETAIL1, 
			"==>bdb_modrdn: parent dn=%s\n", p_dn.bv_val, 0, 0 );
#else
		Debug( LDAP_DEBUG_TRACE,
			"bdb_modrdn: parent dn=%s\n",
			p_dn.bv_val, 0, 0 );
#endif

	} else {
		/* no parent, modrdn entry directly under root */
		isroot = be_isroot( op->o_bd, &op->o_ndn );
		if ( ! isroot ) {
			if ( be_issuffix( op->o_bd, (struct berval *)&slap_empty_bv )
				|| be_isupdate( op->o_bd, &op->o_ndn ) ) {

				p = (Entry *)&slap_entry_root;

				/* check parent for "children" acl */
				rs->sr_err = access_allowed( op, p,
					children, NULL, ACL_WRITE, NULL );

				p = NULL;

				if ( ! rs->sr_err ) {
					switch( opinfo.boi_err ) {
					case DB_LOCK_DEADLOCK:
					case DB_LOCK_NOTGRANTED:
						goto retry;
					}

					rs->sr_err = LDAP_INSUFFICIENT_ACCESS;
#ifdef NEW_LOGGING
					LDAP_LOG ( OPERATION, ERR, 
						"==>bdb_modrdn: no access to parent\n", 0, 0, 0 );
#else
					Debug( LDAP_DEBUG_TRACE, 
						"no access to parent\n", 
						0, 0, 0 );
#endif
					rs->sr_text = "no write access to old parent";
					goto return_results;
				}

#ifdef NEW_LOGGING
				LDAP_LOG ( OPERATION, DETAIL1, 
					"==>bdb_modrdn: wr to children of entry \"%s\" OK\n", 
					p_dn.bv_val, 0, 0 );
#else
				Debug( LDAP_DEBUG_TRACE,
					"bdb_modrdn: wr to children of entry \"\" OK\n",
					0, 0, 0 );
#endif
		
				p_dn.bv_val = "";
				p_dn.bv_len = 0;

#ifdef NEW_LOGGING
				LDAP_LOG ( OPERATION, DETAIL1, 
					"==>bdb_modrdn: parent dn=\"\" \n", 0, 0, 0 );
#else
				Debug( LDAP_DEBUG_TRACE,
					"bdb_modrdn: parent dn=\"\"\n",
					0, 0, 0 );
#endif

			} else {
#ifdef NEW_LOGGING
				LDAP_LOG ( OPERATION, ERR, 
					"==>bdb_modrdn: no parent, not root &\"\" is not "
					"suffix\n", 0, 0, 0 );
#else
				Debug( LDAP_DEBUG_TRACE,
					"bdb_modrdn: no parent, not root "
					"& \"\" is not suffix\n",
					0, 0, 0);
#endif
				rs->sr_text = "no write access to old parent";
				rs->sr_err = LDAP_INSUFFICIENT_ACCESS;
				goto return_results;
			}
		}
	}

	new_parent_dn = &p_dn;	/* New Parent unless newSuperior given */

	if ( op->oq_modrdn.rs_newSup != NULL ) {
#ifdef NEW_LOGGING
		LDAP_LOG ( OPERATION, DETAIL1, 
			"==>bdb_modrdn: new parent \"%s\" requested...\n", 
			op->oq_modrdn.rs_newSup->bv_val, 0, 0 );
#else
		Debug( LDAP_DEBUG_TRACE, 
			"bdb_modrdn: new parent \"%s\" requested...\n",
			op->oq_modrdn.rs_newSup->bv_val, 0, 0 );
#endif

		/*  newSuperior == oldParent? */
		if( dn_match( &p_ndn, op->oq_modrdn.rs_nnewSup ) ) {
#ifdef NEW_LOGGING
			LDAP_LOG( BACK_BDB, INFO, "bdb_back_modrdn: "
				"new parent \"%s\" same as the old parent \"%s\"\n",
				op->oq_modrdn.rs_newSup->bv_val, p_dn.bv_val, 0 );
#else
			Debug( LDAP_DEBUG_TRACE, "bdb_back_modrdn: "
				"new parent \"%s\" same as the old parent \"%s\"\n",
				op->oq_modrdn.rs_newSup->bv_val, p_dn.bv_val, 0 );
#endif      
			op->oq_modrdn.rs_newSup = NULL; /* ignore newSuperior */
		}
	}

	if ( op->oq_modrdn.rs_newSup != NULL ) {
		if ( op->oq_modrdn.rs_newSup->bv_len ) {
			np_dn = op->oq_modrdn.rs_newSup;
			np_ndn = op->oq_modrdn.rs_nnewSup;

			/* newSuperior == oldParent?, if so ==> ERROR */
			/* newSuperior == entry being moved?, if so ==> ERROR */
			/* Get Entry with dn=newSuperior. Does newSuperior exist? */

			rs->sr_err = bdb_dn2entry_r( op->o_bd,
				ltid, np_ndn, &np, NULL, 0, locker, &lock );

			switch( rs->sr_err ) {
			case 0:
			case DB_NOTFOUND:
				break;
			case DB_LOCK_DEADLOCK:
			case DB_LOCK_NOTGRANTED:
				goto retry;
			case LDAP_BUSY:
				rs->sr_text = "ldap server busy";
				goto return_results;
			default:
				rs->sr_err = LDAP_OTHER;
				rs->sr_text = "internal error";
				goto return_results;
			}

			if( np == NULL) {
#ifdef NEW_LOGGING
				LDAP_LOG ( OPERATION, DETAIL1, 
					"==>bdb_modrdn: newSup(ndn=%s) not here!\n", 
					np_ndn->bv_val, 0, 0 );
#else
				Debug( LDAP_DEBUG_TRACE,
					"bdb_modrdn: newSup(ndn=%s) not here!\n",
					np_ndn->bv_val, 0, 0);
#endif
				rs->sr_text = "new superior not found";
				rs->sr_err = LDAP_OTHER;
				goto return_results;
			}

#ifdef NEW_LOGGING
			LDAP_LOG ( OPERATION, DETAIL1, 
				"==>bdb_modrdn: wr to new parent OK np=%p, id=%ld\n", 
				np, (long) np->e_id, 0 );
#else
			Debug( LDAP_DEBUG_TRACE,
				"bdb_modrdn: wr to new parent OK np=%p, id=%ld\n",
				np, (long) np->e_id, 0 );
#endif

			/* check newSuperior for "children" acl */
			rs->sr_err = access_allowed( op, np, children,
				NULL, ACL_WRITE, NULL );

			if( ! rs->sr_err ) {
				switch( opinfo.boi_err ) {
				case DB_LOCK_DEADLOCK:
				case DB_LOCK_NOTGRANTED:
					goto retry;
				}

#ifdef NEW_LOGGING
				LDAP_LOG ( OPERATION, DETAIL1, 
					"==>bdb_modrdn: no wr to newSup children\n", 0, 0, 0 );
#else
				Debug( LDAP_DEBUG_TRACE,
					"bdb_modrdn: no wr to newSup children\n",
					0, 0, 0 );
#endif
				rs->sr_text = "no write access to new superior's children";
				rs->sr_err = LDAP_INSUFFICIENT_ACCESS;
				goto return_results;
			}

#ifdef BDB_ALIASES
			if ( is_entry_alias( np ) ) {
				/* parent is an alias, don't allow add */
#ifdef NEW_LOGGING
				LDAP_LOG ( OPERATION, DETAIL1, 
					"==>bdb_modrdn: entry is alias\n", 0, 0, 0 );
#else
				Debug( LDAP_DEBUG_TRACE, "bdb_modrdn: entry is alias\n",
					0, 0, 0 );
#endif
				rs->sr_text = "new superior is an alias";
				rs->sr_err = LDAP_ALIAS_PROBLEM;
				goto return_results;
			}
#endif

			if ( is_entry_referral( np ) ) {
				/* parent is a referral, don't allow add */
#ifdef NEW_LOGGING
				LDAP_LOG ( OPERATION, DETAIL1, 
					"==>bdb_modrdn: entry is referral\n", 0, 0, 0 );
#else
				Debug( LDAP_DEBUG_TRACE, "bdb_modrdn: entry is referral\n",
					0, 0, 0 );
#endif
				rs->sr_text = "new superior is a referral";
				rs->sr_err = LDAP_OTHER;
				goto return_results;
			}

		} else {
			if ( isroot == -1 ) {
				isroot = be_isroot( op->o_bd, &op->o_ndn );
			}
			
			np_dn = NULL;

			/* no parent, modrdn entry directly under root */
			if ( ! isroot ) {
				if ( be_issuffix( op->o_bd, (struct berval *)&slap_empty_bv )
					|| be_isupdate( op->o_bd, &op->o_ndn ) ) {
					np = (Entry *)&slap_entry_root;

					/* check parent for "children" acl */
					rs->sr_err = access_allowed( op, np,
						children, NULL, ACL_WRITE, NULL );

					np = NULL;

					if ( ! rs->sr_err ) {
						switch( opinfo.boi_err ) {
						case DB_LOCK_DEADLOCK:
						case DB_LOCK_NOTGRANTED:
							goto retry;
						}

						rs->sr_err = LDAP_INSUFFICIENT_ACCESS;
#ifdef NEW_LOGGING
						LDAP_LOG ( OPERATION, ERR, 
							"==>bdb_modrdn: no access to superior\n", 0, 0, 0 );
#else
						Debug( LDAP_DEBUG_TRACE, 
							"no access to new superior\n", 
							0, 0, 0 );
#endif
						rs->sr_text = "no write access to new superior's children";
						goto return_results;
					}

#ifdef NEW_LOGGING
					LDAP_LOG ( OPERATION, DETAIL1, 
						"bdb_modrdn: wr to children entry \"\" OK\n", 0, 0, 0 );
#else
					Debug( LDAP_DEBUG_TRACE,
						"bdb_modrdn: wr to children of entry \"\" OK\n",
						0, 0, 0 );
#endif
		
				} else {
#ifdef NEW_LOGGING
					LDAP_LOG ( OPERATION, ERR, 
						"bdb_modrdn: new superior=\"\", not root & \"\" "
						"is not suffix\n", 0, 0, 0 );
#else
					Debug( LDAP_DEBUG_TRACE,
						"bdb_modrdn: new superior=\"\", not root "
						"& \"\" is not suffix\n",
						0, 0, 0);
#endif
					rs->sr_text = "no write access to new superior's children";
					rs->sr_err = LDAP_INSUFFICIENT_ACCESS;
					goto return_results;
				}
			}

#ifdef NEW_LOGGING
			LDAP_LOG ( OPERATION, DETAIL1, 
				"bdb_modrdn: new superior=\"\"\n", 0, 0, 0 );
#else
			Debug( LDAP_DEBUG_TRACE,
				"bdb_modrdn: new superior=\"\"\n",
				0, 0, 0 );
#endif
		}

#ifdef NEW_LOGGING
		LDAP_LOG ( OPERATION, DETAIL1, 
			"bdb_modrdn: wr to new parent's children OK\n", 0, 0, 0 );
#else
		Debug( LDAP_DEBUG_TRACE,
			"bdb_modrdn: wr to new parent's children OK\n",
			0, 0, 0 );
#endif

		new_parent_dn = np_dn;
	}

	/* Build target dn and make sure target entry doesn't exist already. */
	if (!new_dn.bv_val) build_new_dn( &new_dn, new_parent_dn, &op->oq_modrdn.rs_newrdn ); 

	if (!new_ndn.bv_val) dnNormalize2( NULL, &new_dn, &new_ndn, op->o_tmpmemctx );

#ifdef NEW_LOGGING
	LDAP_LOG ( OPERATION, RESULTS, 
		"bdb_modrdn: new ndn=%s\n", new_ndn.bv_val, 0, 0 );
#else
	Debug( LDAP_DEBUG_TRACE, "bdb_modrdn: new ndn=%s\n",
		new_ndn.bv_val, 0, 0 );
#endif

	rs->sr_err = bdb_dn2id ( op->o_bd, ltid, &new_ndn, &id, 0 );
	switch( rs->sr_err ) {
	case DB_LOCK_DEADLOCK:
	case DB_LOCK_NOTGRANTED:
		goto retry;
	case DB_NOTFOUND:
		break;
	case 0:
		rs->sr_err = LDAP_ALREADY_EXISTS;
		goto return_results;
	default:
		rs->sr_err = LDAP_OTHER;
		rs->sr_text = "internal error";
		goto return_results;
	}

	/* Get attribute type and attribute value of our new rdn, we will
	 * need to add that to our new entry
	 */
	if ( !new_rdn && ldap_bv2rdn_x( &op->oq_modrdn.rs_newrdn, &new_rdn, (char **)&rs->sr_text,
		LDAP_DN_FORMAT_LDAP, op->o_tmpmemctx ) )
	{
#ifdef NEW_LOGGING
		LDAP_LOG ( OPERATION, ERR, 
			"bdb_modrdn: can't figure out "
			"type(s)/values(s) of newrdn\n", 
			0, 0, 0 );
#else
		Debug( LDAP_DEBUG_TRACE,
			"bdb_modrdn: can't figure out "
			"type(s)/values(s) of newrdn\n", 
			0, 0, 0 );
#endif
		rs->sr_err = LDAP_INVALID_DN_SYNTAX;
		rs->sr_text = "unknown type(s) used in RDN";
		goto return_results;
	}

#ifdef NEW_LOGGING
	LDAP_LOG ( OPERATION, RESULTS, 
		"bdb_modrdn: new_rdn_type=\"%s\", "
		"new_rdn_val=\"%s\"\n",
		new_rdn[ 0 ]->la_attr.bv_val, 
		new_rdn[ 0 ]->la_value.bv_val, 0 );
#else
	Debug( LDAP_DEBUG_TRACE,
		"bdb_modrdn: new_rdn_type=\"%s\", "
		"new_rdn_val=\"%s\"\n",
		new_rdn[ 0 ]->la_attr.bv_val,
		new_rdn[ 0 ]->la_value.bv_val, 0 );
#endif

	if ( op->oq_modrdn.rs_deleteoldrdn ) {
		if ( !old_rdn && ldap_bv2rdn_x( &op->o_req_dn, &old_rdn, (char **)&rs->sr_text,
			LDAP_DN_FORMAT_LDAP, op->o_tmpmemctx ) )
		{
#ifdef NEW_LOGGING
			LDAP_LOG ( OPERATION, ERR, 
				"bdb_modrdn: can't figure out "
				"type(s)/values(s) of old_rdn\n", 
				0, 0, 0 );
#else
			Debug( LDAP_DEBUG_TRACE,
				"bdb_modrdn: can't figure out "
				"the old_rdn type(s)/value(s)\n", 
				0, 0, 0 );
#endif
			rs->sr_err = LDAP_OTHER;
			rs->sr_text = "cannot parse RDN from old DN";
			goto return_results;		
		}
	}

	/* prepare modlist of modifications from old/new rdn */
	if (!mod) {
		rs->sr_err = slap_modrdn2mods( op, rs, e, old_rdn, new_rdn, &mod );
		if ( rs->sr_err != LDAP_SUCCESS ) {
			goto return_results;
		}
	}
	
	/* delete old one */
	rs->sr_err = bdb_dn2id_delete( op->o_bd, ltid, p_ndn.bv_val, e );
	if ( rs->sr_err != 0 ) {
		switch( rs->sr_err ) {
		case DB_LOCK_DEADLOCK:
		case DB_LOCK_NOTGRANTED:
			goto retry;
		}
		rs->sr_err = LDAP_OTHER;
		rs->sr_text = "DN index delete fail";
		goto return_results;
	}

	(void) bdb_cache_delete_entry(&bdb->bi_cache, e);

	/* Binary format uses a single contiguous block, cannot
	 * free individual fields. But if a previous modrdn has
	 * already happened, must free the names.
	 */
#ifdef BDB_HIER
	ch_free(e->e_name.bv_val);
	e->e_name.bv_val = ch_malloc(new_dn.bv_len + new_ndn.bv_len + 2);
	e->e_name.bv_len = new_dn.bv_len;
	e->e_nname.bv_val = e->e_name.bv_val + new_dn.bv_len + 1;
	e->e_nname.bv_len = new_ndn.bv_len;
	strcpy(e->e_name.bv_val, new_dn.bv_val);
	strcpy(e->e_nname.bv_val, new_ndn.bv_val);
#else
	if( e->e_nname.bv_val < e->e_bv.bv_val || e->e_nname.bv_val >
		e->e_bv.bv_val + e->e_bv.bv_len ) {
		ch_free(e->e_name.bv_val);
		ch_free(e->e_nname.bv_val);
		e->e_name.bv_val = NULL;
		e->e_nname.bv_val = NULL;
	}
	e->e_name = new_dn;
	e->e_nname = new_ndn;
	new_dn.bv_val = NULL;
	new_ndn.bv_val = NULL;
#endif
	/* add new one */
	rs->sr_err = bdb_dn2id_add( op->o_bd, ltid, np_ndn, e );
	if ( rs->sr_err != 0 ) {
		switch( rs->sr_err ) {
		case DB_LOCK_DEADLOCK:
		case DB_LOCK_NOTGRANTED:
			goto retry;
		}
		rs->sr_err = LDAP_OTHER;
		rs->sr_text = "DN index add failed";
		goto return_results;
	}

#if defined(LDAP_CLIENT_UPDATE) || defined(LDAP_SYNC)
	if ( rs->sr_err == LDAP_SUCCESS && !op->o_noop ) {
		LDAP_LIST_FOREACH ( ps_list, &bdb->bi_psearch_list, o_ps_link ) {
			bdb_psearch( op, rs, ps_list, e, LDAP_PSEARCH_BY_PREMODIFY );
		}
	}
#endif

	/* modify entry */
	rs->sr_err = bdb_modify_internal( op, ltid, &mod[0], e,
		&rs->sr_text, textbuf, textlen );

	if( rs->sr_err != LDAP_SUCCESS ) {
		if ( ( rs->sr_err == LDAP_INSUFFICIENT_ACCESS ) && opinfo.boi_err ) {
			rs->sr_err = opinfo.boi_err;
		}
		switch( rs->sr_err ) {
		case DB_LOCK_DEADLOCK:
		case DB_LOCK_NOTGRANTED:
			goto retry;
		}
		goto return_results;
	}
	
	/* id2entry index */
	rs->sr_err = bdb_id2entry_update( op->o_bd, ltid, e );
	if ( rs->sr_err != 0 ) {
		switch( rs->sr_err ) {
		case DB_LOCK_DEADLOCK:
		case DB_LOCK_NOTGRANTED:
			goto retry;
		}
		rs->sr_err = LDAP_OTHER;
		rs->sr_text = "entry update failed";
		goto return_results;
	}

	if( op->o_noop ) {
		if(( rs->sr_err=TXN_ABORT( ltid )) != 0 ) {
			rs->sr_text = "txn_abort (no-op) failed";
		} else {
			noop = 1;
			rs->sr_err = LDAP_SUCCESS;
		}

	} else {
		char gid[DB_XIDDATASIZE];

		snprintf( gid, sizeof( gid ), "%s-%08lx-%08lx",
			bdb_uuid.bv_val, (long) op->o_connid, (long) op->o_opid );

		if(( rs->sr_err=TXN_PREPARE( ltid, gid )) != 0 ) {
			rs->sr_text = "txn_prepare failed";
		} else {
			if( bdb_cache_update_entry(&bdb->bi_cache, e) == -1 ) {
				if(( rs->sr_err=TXN_ABORT( ltid )) != 0 ) {
					rs->sr_text ="cache update & txn_abort failed";
				} else {
					rs->sr_err = LDAP_OTHER;
					rs->sr_text = "cache update failed";
				}

			} else {
				bdb_cache_entry_commit( e );
				if(( rs->sr_err=TXN_COMMIT( ltid, 0 )) != 0 ) {
					rs->sr_text = "txn_commit failed";
				} else {
					rs->sr_err = LDAP_SUCCESS;
				}
			}
		}
	}
 
	ltid = NULL;
	op->o_private = NULL;
 
	if( rs->sr_err == LDAP_SUCCESS ) {
#ifdef NEW_LOGGING
		LDAP_LOG ( OPERATION, RESULTS, 
			"bdb_modrdn: rdn modified%s id=%08lx dn=\"%s\"\n", 
			op->o_noop ? " (no-op)" : "", e->e_id, e->e_dn );
#else
		Debug(LDAP_DEBUG_TRACE,
			"bdb_modrdn: rdn modified%s id=%08lx dn=\"%s\"\n",
			op->o_noop ? " (no-op)" : "", e->e_id, e->e_dn );
#endif
		rs->sr_text = NULL;
	} else {
#ifdef NEW_LOGGING
		LDAP_LOG ( OPERATION, RESULTS, "bdb_modrdn: %s : %s (%d)\n", 
			rs->sr_text, db_strerror(rs->sr_err), rs->sr_err );
#else
		Debug( LDAP_DEBUG_TRACE, "bdb_add: %s : %s (%d)\n",
			rs->sr_text, db_strerror(rs->sr_err), rs->sr_err );
#endif
		rs->sr_err = LDAP_OTHER;
	}

return_results:
	send_ldap_result( op, rs );

#if defined(LDAP_CLIENT_UPDATE) || defined(LDAP_SYNC)
	if ( rs->sr_err == LDAP_SUCCESS && !op->o_noop ) {
		/* Loop through in-scope entries for each psearch spec */
		LDAP_LIST_FOREACH ( ps_list, &bdb->bi_psearch_list, o_ps_link ) {
			bdb_psearch( op, rs, ps_list, e, LDAP_PSEARCH_BY_MODIFY );
		}
		pm_list = LDAP_LIST_FIRST(&op->o_pm_list);
		while ( pm_list != NULL ) {
			bdb_psearch(op, rs, pm_list->ps_op,
						e, LDAP_PSEARCH_BY_SCOPEOUT);
			pm_prev = pm_list;
			LDAP_LIST_REMOVE ( pm_list, ps_link );
			pm_list = LDAP_LIST_NEXT ( pm_list, ps_link );
			ch_free( pm_prev );
		}
	}
#endif

	if( rs->sr_err == LDAP_SUCCESS && bdb->bi_txn_cp ) {
		ldap_pvt_thread_yield();
		TXN_CHECKPOINT( bdb->bi_dbenv,
			bdb->bi_txn_cp_kbyte, bdb->bi_txn_cp_min, 0 );
	}

done:
	if( new_dn.bv_val != NULL ) free( new_dn.bv_val );
	if( new_ndn.bv_val != NULL ) free( new_ndn.bv_val );

	/* LDAP v2 supporting correct attribute handling. */
	if ( new_rdn != NULL ) {
		ldap_rdnfree( new_rdn );
	}
	if ( old_rdn != NULL ) {
		ldap_rdnfree( old_rdn );
	}
	if( mod != NULL ) {
		Modifications *tmp;
		for (; mod; mod=tmp ) {
			tmp = mod->sml_next;
			if ( mod->sml_nvalues ) free( mod->sml_nvalues[0].bv_val );
			free( mod );
		}
	}

	/* LDAP v3 Support */
	if( np != NULL ) {
		/* free new parent and reader lock */
		bdb_unlocked_cache_return_entry_r(&bdb->bi_cache, np);
	}

	if( p != NULL ) {
		/* free parent and reader lock */
		bdb_unlocked_cache_return_entry_r(&bdb->bi_cache, p);
	}

	/* free entry */
	if( e != NULL ) {
		bdb_unlocked_cache_return_entry_w( &bdb->bi_cache, e);
	}

	if( ltid != NULL ) {
#if defined(LDAP_CLIENT_UPDATE) || defined(LDAP_SYNC)
                pm_list = LDAP_LIST_FIRST(&op->o_pm_list);
                while ( pm_list != NULL ) {
                        LDAP_LIST_REMOVE ( pm_list, ps_link );
			pm_prev = pm_list;
                        pm_list = LDAP_LIST_NEXT ( pm_list, ps_link );
			ch_free( pm_prev );
                }
#endif
		TXN_ABORT( ltid );
		op->o_private = NULL;
	}

	return ( ( rs->sr_err == LDAP_SUCCESS ) ? noop : rs->sr_err );
}
