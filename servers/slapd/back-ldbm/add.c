/* add.c - ldap ldbm back-end add routine */
/* $OpenLDAP$ */
/*
 * Copyright 1998-2003 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */

#include "portable.h"

#include <stdio.h>

#include <ac/socket.h>
#include <ac/string.h>

#include "slap.h"
#include "back-ldbm.h"
#include "proto-back-ldbm.h"

int
ldbm_back_add(
    Operation	*op,
    SlapReply	*rs )
{
	struct ldbminfo	*li = (struct ldbminfo *) op->o_bd->be_private;
	struct berval	pdn;
	Entry		*p = NULL;
	ID               id = NOID;
	AttributeDescription *children = slap_schema.si_ad_children;
	AttributeDescription *entry = slap_schema.si_ad_entry;
	char textbuf[SLAP_TEXT_BUFLEN];
	size_t textlen = sizeof textbuf;

#ifdef NEW_LOGGING
	LDAP_LOG( BACK_LDBM, ENTRY, "ldbm_back_add: %s\n", op->o_req_dn.bv_val, 0, 0 );
#else
	Debug(LDAP_DEBUG_ARGS, "==> ldbm_back_add: %s\n", op->o_req_dn.bv_val, 0, 0);
#endif
	
#ifndef LDAP_CACHING
	rs->sr_err = entry_schema_check( op->o_bd, op->oq_add.rs_e, NULL, &rs->sr_text, textbuf, textlen );
#else /* LDAP_CACHING */
        if ( !op->o_caching_on ) {
		rs->sr_err = entry_schema_check( op->o_bd, op->oq_add.rs_e, NULL, &rs->sr_text, textbuf, textlen );
	} else {
		rs->sr_err = LDAP_SUCCESS;
	}
#endif /* LDAP_CACHING */

	if ( rs->sr_err != LDAP_SUCCESS ) {
#ifdef NEW_LOGGING
		LDAP_LOG( BACK_LDBM, ERR, 
			"ldbm_back_add: entry (%s) failed schema check.\n", op->o_req_dn.bv_val, 0, 0 );
#else
		Debug( LDAP_DEBUG_TRACE, "entry failed schema check: %s\n",
			rs->sr_text, 0, 0 );
#endif

		send_ldap_result( op, rs );
		return( -1 );
	}

#ifdef LDAP_CACHING
	if ( !op->o_caching_on ) {
#endif /* LDAP_CACHING */
	if ( !access_allowed( op, op->oq_add.rs_e,
				entry, NULL, ACL_WRITE, NULL ) )
	{
#ifdef NEW_LOGGING
		LDAP_LOG( BACK_LDBM, ERR, 
			"ldbm_back_add: No write access to entry (%s).\n", 
			op->o_req_dn.bv_val, 0, 0 );
#else
		Debug( LDAP_DEBUG_TRACE, "no write access to entry\n", 0,
		    0, 0 );
#endif

		send_ldap_error( op, rs, LDAP_INSUFFICIENT_ACCESS,
		    "no write access to entry" );

		return -1;
	}
#ifdef LDAP_CACHING
	}
#endif /* LDAP_CACHING */

	/* grab giant lock for writing */
	ldap_pvt_thread_rdwr_wlock(&li->li_giant_rwlock);

	if ( ( rs->sr_err = dn2id( op->o_bd, &op->o_req_ndn, &id ) ) || id != NOID ) {
		/* if (rs->sr_err) something bad happened to ldbm cache */
		ldap_pvt_thread_rdwr_wunlock(&li->li_giant_rwlock);
		rs->sr_err = rs->sr_err ? LDAP_OTHER : LDAP_ALREADY_EXISTS;
		send_ldap_result( op, rs );
		return( -1 );
	}

	/*
	 * Get the parent dn and see if the corresponding entry exists.
	 * If the parent does not exist, only allow the "root" user to
	 * add the entry.
	 */

	if ( be_issuffix( op->o_bd, &op->o_req_ndn ) ) {
		pdn = slap_empty_bv;
	} else {
		dnParent( &op->o_req_ndn, &pdn );
	}

#ifndef LDAP_CACHING
	if( pdn.bv_len )
#else /* LDAP_CACHING */
	if( pdn.bv_len && !op->o_caching_on )
#endif /* LDAP_CACHING */
	{
		Entry *matched = NULL;

		/* get parent with writer lock */
		if ( (p = dn2entry_w( op->o_bd, &pdn, &matched )) == NULL ) {
			if ( matched != NULL ) {
				rs->sr_matched = ch_strdup( matched->e_dn );
				rs->sr_ref = is_entry_referral( matched )
					? get_entry_referrals( op, matched )
					: NULL;
				cache_return_entry_r( &li->li_cache, matched );

			} else {
				rs->sr_ref = referral_rewrite( default_referral,
					NULL, &op->o_req_dn, LDAP_SCOPE_DEFAULT );
			}

			ldap_pvt_thread_rdwr_wunlock(&li->li_giant_rwlock);

#ifdef NEW_LOGGING
			LDAP_LOG( BACK_LDBM, ERR, 
				"ldbm_back_add: Parent of (%s) does not exist.\n", 
				op->o_req_dn.bv_val, 0, 0 );
#else
			Debug( LDAP_DEBUG_TRACE, "parent does not exist\n",
				0, 0, 0 );
#endif

			rs->sr_text = rs->sr_ref ? "parent is referral" : "parent does not exist";
			rs->sr_err = LDAP_REFERRAL;
			send_ldap_result( op, rs );

			ber_bvarray_free( rs->sr_ref );
			free( (char *)rs->sr_matched );

			return -1;
		}

		if ( ! access_allowed( op, p,
			children, NULL, ACL_WRITE, NULL ) )
		{
			/* free parent and writer lock */
			cache_return_entry_w( &li->li_cache, p ); 
			ldap_pvt_thread_rdwr_wunlock(&li->li_giant_rwlock);

#ifdef NEW_LOGGING
			LDAP_LOG( BACK_LDBM, ERR, 
				"ldbm_back_add: No write access to parent (%s).\n", 
				op->o_req_dn.bv_val, 0, 0 );
#else
			Debug( LDAP_DEBUG_TRACE, "no write access to parent\n", 0,
			    0, 0 );
#endif

			send_ldap_error( op, rs, LDAP_INSUFFICIENT_ACCESS,
			    "no write access to parent" );

			return -1;
		}

		if ( is_entry_alias( p ) ) {
			/* parent is an alias, don't allow add */

			/* free parent and writer lock */
			cache_return_entry_w( &li->li_cache, p );
			ldap_pvt_thread_rdwr_wunlock(&li->li_giant_rwlock);

#ifdef NEW_LOGGING
			LDAP_LOG(BACK_LDBM, ERR, 
				"ldbm_back_add:  Parent is an alias.\n", 0, 0, 0 );
#else
			Debug( LDAP_DEBUG_TRACE, "parent is alias\n", 0,
			    0, 0 );
#endif


			send_ldap_error( op, rs, LDAP_ALIAS_PROBLEM,
			    "parent is an alias" );

			return -1;
		}

		if ( is_entry_referral( p ) ) {
			/* parent is a referral, don't allow add */
			rs->sr_matched = ch_strdup( p->e_dn );
			rs->sr_ref = is_entry_referral( p )
				? get_entry_referrals( op, p )
				: NULL;

			/* free parent and writer lock */
			cache_return_entry_w( &li->li_cache, p );
			ldap_pvt_thread_rdwr_wunlock(&li->li_giant_rwlock);

#ifdef NEW_LOGGING
			LDAP_LOG( BACK_LDBM, ERR,
				   "ldbm_back_add: Parent is referral.\n", 0, 0, 0 );
#else
			Debug( LDAP_DEBUG_TRACE, "parent is referral\n", 0,
			    0, 0 );
#endif
			rs->sr_err = LDAP_REFERRAL;
			send_ldap_result( op, rs );

			ber_bvarray_free( rs->sr_ref );
			free( (char *)rs->sr_matched );
			return -1;
		}

	} else {
#ifndef LDAP_CACHING
		if( pdn.bv_val != NULL )
#else /* LDAP_CACHING */
	        if( pdn.bv_val != NULL && !op->o_caching_on )
#endif /* LDAP_CACHING */
		{
			assert( *pdn.bv_val == '\0' );
		}

		/* no parent, must be adding entry to root */
#ifndef LDAP_CACHING
		if ( !be_isroot( op->o_bd, &op->o_ndn ) )
#else /* LDAP_CACHING */
		if ( !be_isroot( op->o_bd, &op->o_ndn ) && !op->o_caching_on )
#endif /* LDAP_CACHING */
		{
			if ( be_issuffix( op->o_bd, (struct berval *)&slap_empty_bv ) || be_isupdate( op->o_bd, &op->o_ndn ) ) {
				p = (Entry *)&slap_entry_root;
				
				rs->sr_err = access_allowed( op, p,
					children, NULL, ACL_WRITE, NULL );
				p = NULL;
				
				if ( ! rs->sr_err ) {
					ldap_pvt_thread_rdwr_wunlock(&li->li_giant_rwlock);

#ifdef NEW_LOGGING
					LDAP_LOG( BACK_LDBM, ERR,
						"ldbm_back_add: No write "
						"access to parent (\"\").\n", 0, 0, 0 );
#else
					Debug( LDAP_DEBUG_TRACE, 
						"no write access to parent\n", 
						0, 0, 0 );
#endif

					send_ldap_error( op, rs,
						LDAP_INSUFFICIENT_ACCESS,
						"no write access to parent" );

					return -1;
				}

			} else {
				ldap_pvt_thread_rdwr_wunlock(&li->li_giant_rwlock);

#ifdef NEW_LOGGING
				LDAP_LOG( BACK_LDBM, ERR,
					   "ldbm_back_add: %s add denied.\n",
					   pdn.bv_val == NULL ? "suffix" 
					   : "entry at root", 0, 0 );
#else
				Debug( LDAP_DEBUG_TRACE, "%s add denied\n",
						pdn.bv_val == NULL ? "suffix" 
						: "entry at root", 0, 0 );
#endif

				send_ldap_error( op, rs,
						LDAP_INSUFFICIENT_ACCESS, NULL );

				return -1;
			}
		}
	}

	if ( next_id( op->o_bd, &op->oq_add.rs_e->e_id ) ) {
		if( p != NULL) {
			/* free parent and writer lock */
			cache_return_entry_w( &li->li_cache, p ); 
		}

		ldap_pvt_thread_rdwr_wunlock(&li->li_giant_rwlock);

#ifdef NEW_LOGGING
		LDAP_LOG( BACK_LDBM, ERR,
			"ldbm_back_add: next_id failed.\n", 0, 0, 0 );
#else
		Debug( LDAP_DEBUG_ANY, "ldbm_add: next_id failed\n",
			0, 0, 0 );
#endif

		send_ldap_error( op, rs, LDAP_OTHER,
			"next_id add failed" );

		return( -1 );
	}

	/*
	 * Try to add the entry to the cache, assign it a new dnid.
	 */
	rs->sr_err = cache_add_entry_rw(&li->li_cache, op->oq_add.rs_e, CACHE_WRITE_LOCK);

	if ( rs->sr_err != 0 ) {
		if( p != NULL) {
			/* free parent and writer lock */
			cache_return_entry_w( &li->li_cache, p ); 
		}

		ldap_pvt_thread_rdwr_wunlock(&li->li_giant_rwlock);

#ifdef NEW_LOGGING
		LDAP_LOG( BACK_LDBM, ERR,
			"ldbm_back_add: cache_add_entry_lock failed.\n", 0, 0, 0 );
#else
		Debug( LDAP_DEBUG_ANY, "cache_add_entry_lock failed\n", 0, 0,
		    0 );
#endif

		rs->sr_text = rs->sr_err > 0 ? NULL : "cache add failed";
		rs->sr_err = rs->sr_err > 0 ? LDAP_ALREADY_EXISTS : LDAP_OTHER;
		send_ldap_result( op, rs );

		return( -1 );
	}

	rs->sr_err = -1;

	/* attribute indexes */
	if ( index_entry_add( op->o_bd, op->oq_add.rs_e ) != LDAP_SUCCESS ) {
#ifdef NEW_LOGGING
		LDAP_LOG( BACK_LDBM, ERR,
			"ldbm_back_add: index_entry_add failed.\n", 0, 0, 0 );
#else
		Debug( LDAP_DEBUG_TRACE, "index_entry_add failed\n", 0,
		    0, 0 );
#endif
		
		send_ldap_error( op, rs, LDAP_OTHER,
			"index generation failed" );

		goto return_results;
	}

	/* dn2id index */
	if ( dn2id_add( op->o_bd, &op->oq_add.rs_e->e_nname, op->oq_add.rs_e->e_id ) != 0 ) {
#ifdef NEW_LOGGING
		LDAP_LOG( BACK_LDBM, ERR,
			"ldbm_back_add: dn2id_add failed.\n", 0, 0, 0 );
#else
		Debug( LDAP_DEBUG_TRACE, "dn2id_add failed\n", 0,
		    0, 0 );
#endif
		/* FIXME: delete attr indices? */

		send_ldap_error( op, rs, LDAP_OTHER,
			"DN index generation failed" );

		goto return_results;
	}

	/* id2entry index */
	if ( id2entry_add( op->o_bd, op->oq_add.rs_e ) != 0 ) {
#ifdef NEW_LOGGING
		LDAP_LOG( BACK_LDBM, ERR,
			   "ldbm_back_add: id2entry_add failed.\n", 0, 0, 0 );
#else
		Debug( LDAP_DEBUG_TRACE, "id2entry_add failed\n", 0,
		    0, 0 );
#endif

		/* FIXME: delete attr indices? */
		(void) dn2id_delete( op->o_bd, &op->oq_add.rs_e->e_nname, op->oq_add.rs_e->e_id );
		
		send_ldap_error( op, rs, LDAP_OTHER,
			"entry store failed" );

		goto return_results;
	}

	rs->sr_err = LDAP_SUCCESS;
	send_ldap_result( op, rs );

	/* marks the entry as committed, so it is added to the cache;
	 * otherwise it is removed from the cache, but not destroyed;
	 * it will be destroyed by the caller */
	cache_entry_commit( op->oq_add.rs_e );

return_results:;
	if (p != NULL) {
		/* free parent and writer lock */
		cache_return_entry_w( &li->li_cache, p ); 
	}

	if ( rs->sr_err ) {
		/*
		 * in case of error, writer lock is freed 
		 * and entry's private data is destroyed.
		 * otherwise, this is done when entry is released
		 */
		cache_return_entry_w( &li->li_cache, op->oq_add.rs_e );
		ldap_pvt_thread_rdwr_wunlock(&li->li_giant_rwlock);
	}

	return( rs->sr_err );
}
