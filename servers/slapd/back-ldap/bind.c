/* bind.c - ldap backend bind function */
/* $OpenLDAP$ */
/* This work is part of OpenLDAP Software <http://www.openldap.org/>.
 *
 * Copyright 1999-2005 The OpenLDAP Foundation.
 * Portions Copyright 2000-2003 Pierangelo Masarati.
 * Portions Copyright 1999-2003 Howard Chu.
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
/* ACKNOWLEDGEMENTS:
 * This work was initially developed by the Howard Chu for inclusion
 * in OpenLDAP Software and subsequently enhanced by Pierangelo
 * Masarati.
 */

#include "portable.h"

#include <stdio.h>

#include <ac/socket.h>
#include <ac/string.h>

#define AVL_INTERNAL
#include "slap.h"
#include "back-ldap.h"

#include <lutil_ldap.h>

#define PRINT_CONNTREE 0

static LDAP_REBIND_PROC	ldap_back_rebind;

#ifdef LDAP_BACK_PROXY_AUTHZ
static int
ldap_back_proxy_authz_bind( struct ldapconn *lc, Operation *op, SlapReply *rs );
#endif /* LDAP_BACK_PROXY_AUTHZ */

int
ldap_back_bind( Operation *op, SlapReply *rs )
{
	struct ldapinfo	*li = (struct ldapinfo *) op->o_bd->be_private;
	struct ldapconn *lc;

	int rc = 0;
	ber_int_t msgid;

	lc = ldap_back_getconn( op, rs );
	if ( !lc ) {
		return( -1 );
	}

	if ( !BER_BVISNULL( &lc->lc_bound_ndn ) ) {
		ch_free( lc->lc_bound_ndn.bv_val );
		BER_BVZERO( &lc->lc_bound_ndn );
	}
	lc->lc_bound = 0;

	/* method is always LDAP_AUTH_SIMPLE if we got here */
	rs->sr_err = ldap_sasl_bind( lc->lc_ld, op->o_req_dn.bv_val,
			LDAP_SASL_SIMPLE,
			&op->orb_cred, op->o_ctrls, NULL, &msgid );
	rc = ldap_back_op_result( lc, op, rs, msgid, 1 );

	if ( rc == LDAP_SUCCESS ) {
#if defined(LDAP_BACK_PROXY_AUTHZ)
		if ( li->idassert_flags & LDAP_BACK_AUTH_OVERRIDE ) {
			ldap_back_proxy_authz_bind( lc, op, rs );
			if ( lc->lc_bound == 0 ) {
				rc = 1;
				goto done;
			}
		}
#endif /* LDAP_BACK_PROXY_AUTHZ */

		lc->lc_bound = 1;
		ber_dupbv( &lc->lc_bound_ndn, &op->o_req_ndn );

		if ( li->savecred ) {
			if ( !BER_BVISNULL( &lc->lc_cred ) ) {
				memset( lc->lc_cred.bv_val, 0,
						lc->lc_cred.bv_len );
				ch_free( lc->lc_cred.bv_val );
			}
			ber_dupbv( &lc->lc_cred, &op->orb_cred );
			ldap_set_rebind_proc( lc->lc_ld, ldap_back_rebind, lc );
		}
	}
done:;

	/* must re-insert if local DN changed as result of bind */
	if ( lc->lc_bound && !dn_match( &op->o_req_ndn, &lc->lc_local_ndn ) ) {
		int	lerr;

		ldap_pvt_thread_mutex_lock( &li->conn_mutex );
		lc = avl_delete( &li->conntree, (caddr_t)lc,
				ldap_back_conn_cmp );
		if ( !BER_BVISNULL( &lc->lc_local_ndn ) ) {
			ch_free( lc->lc_local_ndn.bv_val );
		}
		ber_dupbv( &lc->lc_local_ndn, &op->o_req_ndn );
		lerr = avl_insert( &li->conntree, (caddr_t)lc,
			ldap_back_conn_cmp, ldap_back_conn_dup );
		ldap_pvt_thread_mutex_unlock( &li->conn_mutex );
		if ( lerr == -1 ) {
			ldap_back_conn_free( lc );
		}
	}

	return( rc );
}

/*
 * ldap_back_conn_cmp
 *
 * compares two struct ldapconn based on the value of the conn pointer;
 * used by avl stuff
 */
int
ldap_back_conn_cmp( const void *c1, const void *c2 )
{
	const struct ldapconn *lc1 = (const struct ldapconn *)c1;
	const struct ldapconn *lc2 = (const struct ldapconn *)c2;
	int rc;

	/* If local DNs don't match, it is definitely not a match */
	rc = ber_bvcmp( &lc1->lc_local_ndn, &lc2->lc_local_ndn );
	if ( rc ) {
		return rc;
	}

	/* For shared sessions, conn is NULL. Only explicitly
	 * bound sessions will have non-NULL conn.
	 */
	return SLAP_PTRCMP( lc1->lc_conn, lc2->lc_conn );
}

/*
 * ldap_back_conn_dup
 *
 * returns -1 in case a duplicate struct ldapconn has been inserted;
 * used by avl stuff
 */
int
ldap_back_conn_dup( void *c1, void *c2 )
{
	struct ldapconn *lc1 = (struct ldapconn *)c1;
	struct ldapconn *lc2 = (struct ldapconn *)c2;

	/* Cannot have more than one shared session with same DN */
	if ( dn_match( &lc1->lc_local_ndn, &lc2->lc_local_ndn ) &&
       			lc1->lc_conn == lc2->lc_conn )
	{
		return -1;
	}
		
	return 0;
}

#if PRINT_CONNTREE > 0
static void
ravl_print( Avlnode *root, int depth )
{
	int     i;
	struct ldapconn *lc;
	
	if ( root == 0 ) {
		return;
	}
	
	ravl_print( root->avl_right, depth+1 );
	
	for ( i = 0; i < depth; i++ ) {
		printf( "   " );
	}

	lc = root->avl_data;
	printf( "lc(%lx) local(%s) conn(%lx) %d\n",
			lc, lc->lc_local_ndn.bv_val, lc->lc_conn, root->avl_bf );
	
	ravl_print( root->avl_left, depth+1 );
}

static void
myprint( Avlnode *root )
{
	printf( "********\n" );
	
	if ( root == 0 ) {
		printf( "\tNULL\n" );

	} else {
		ravl_print( root, 0 );
	}
	
	printf( "********\n" );
}
#endif /* PRINT_CONNTREE */

int
ldap_back_freeconn( Operation *op, struct ldapconn *lc )
{
	struct ldapinfo	*li = (struct ldapinfo *) op->o_bd->be_private;

	ldap_pvt_thread_mutex_lock( &li->conn_mutex );
	lc = avl_delete( &li->conntree, (caddr_t)lc,
			ldap_back_conn_cmp );
	ldap_back_conn_free( (void *)lc );
	ldap_pvt_thread_mutex_unlock( &li->conn_mutex );

	return 0;
}

struct ldapconn *
ldap_back_getconn( Operation *op, SlapReply *rs )
{
	struct ldapinfo	*li = (struct ldapinfo *)op->o_bd->be_private;
	struct ldapconn	*lc, lc_curr;
	LDAP		*ld;
	int		is_priv = 0;

	/* Searches for a ldapconn in the avl tree */

	/* Explicit binds must not be shared */
	if ( op->o_tag == LDAP_REQ_BIND
		|| ( op->o_conn
			&& op->o_conn->c_authz_backend
			&& op->o_bd->be_private == op->o_conn->c_authz_backend->be_private ) )
	{
		lc_curr.lc_conn = op->o_conn;

	} else {
		lc_curr.lc_conn = NULL;
	}
	
	/* Internal searches are privileged and shared. So is root. */
	if ( op->o_do_not_cache || be_isroot( op ) ) {
		lc_curr.lc_local_ndn = op->o_bd->be_rootndn;
		lc_curr.lc_conn = NULL;
		is_priv = 1;

	} else {
		lc_curr.lc_local_ndn = op->o_ndn;
	}

	ldap_pvt_thread_mutex_lock( &li->conn_mutex );
	lc = (struct ldapconn *)avl_find( li->conntree, 
			(caddr_t)&lc_curr, ldap_back_conn_cmp );
	ldap_pvt_thread_mutex_unlock( &li->conn_mutex );

	/* Looks like we didn't get a bind. Open a new session... */
	if ( !lc ) {
		int vers = op->o_protocol;
		rs->sr_err = ldap_initialize( &ld, li->url );
		
		if ( rs->sr_err != LDAP_SUCCESS ) {
			rs->sr_err = slap_map_api2result( rs );
			if ( rs->sr_text == NULL ) {
				rs->sr_text = "ldap_initialize() failed";
			}
			if ( op->o_conn ) {
				send_ldap_result( op, rs );
			}
			rs->sr_text = NULL;
			return( NULL );
		}
		/* Set LDAP version. This will always succeed: If the client
		 * bound with a particular version, then so can we.
		 */
		ldap_set_option( ld, LDAP_OPT_PROTOCOL_VERSION,
				(const void *)&vers );
		/* FIXME: configurable? */
		ldap_set_option( ld, LDAP_OPT_REFERRALS, LDAP_OPT_ON );

		lc = (struct ldapconn *)ch_malloc( sizeof( struct ldapconn ) );
		lc->lc_conn = lc_curr.lc_conn;
		lc->lc_ld = ld;
		ber_dupbv( &lc->lc_local_ndn, &lc_curr.lc_local_ndn );

		ldap_pvt_thread_mutex_init( &lc->lc_mutex );

		if ( is_priv ) {
			ber_dupbv( &lc->lc_cred, &li->acl_passwd );
			ber_dupbv( &lc->lc_bound_ndn, &li->acl_authcDN );

		} else {
			BER_BVZERO( &lc->lc_cred );
			BER_BVZERO( &lc->lc_bound_ndn );
			if ( op->o_conn && !BER_BVISEMPTY( &op->o_ndn )
					&& op->o_bd == op->o_conn->c_authz_backend )
			{
				ber_dupbv( &lc->lc_bound_ndn, &op->o_ndn );
			}
		}

		lc->lc_bound = 0;

		/* Inserts the newly created ldapconn in the avl tree */
		ldap_pvt_thread_mutex_lock( &li->conn_mutex );
		rs->sr_err = avl_insert( &li->conntree, (caddr_t)lc,
			ldap_back_conn_cmp, ldap_back_conn_dup );

#if PRINT_CONNTREE > 0
		myprint( li->conntree );
#endif /* PRINT_CONNTREE */
	
		ldap_pvt_thread_mutex_unlock( &li->conn_mutex );

		Debug( LDAP_DEBUG_TRACE,
			"=>ldap_back_getconn: conn %p inserted\n", (void *) lc, 0, 0 );
	
		/* Err could be -1 in case a duplicate ldapconn is inserted */
		if ( rs->sr_err != 0 ) {
			ldap_back_conn_free( lc );
			if ( op->o_conn ) {
				send_ldap_error( op, rs, LDAP_OTHER,
				"internal server error" );
			}
			return( NULL );
		}
	} else {
		Debug( LDAP_DEBUG_TRACE,
			"=>ldap_back_getconn: conn %p fetched\n", (void *) lc, 0, 0 );
	}
	
	return( lc );
}

/*
 * ldap_back_dobind
 *
 * Note: as the check for the value of lc->lc_bound was already here, I removed
 * it from all the callers, and I made the function return the flag, so
 * it can be used to simplify the check.
 */
int
ldap_back_dobind( struct ldapconn *lc, Operation *op, SlapReply *rs )
{	
	int		rc;
	ber_int_t	msgid;

	ldap_pvt_thread_mutex_lock( &lc->lc_mutex );
	if ( !lc->lc_bound ) {
#ifdef LDAP_BACK_PROXY_AUTHZ
		/*
		 * FIXME: we need to let clients use proxyAuthz
		 * otherwise we cannot do symmetric pools of servers;
		 * we have to live with the fact that a user can
		 * authorize itself as any ID that is allowed
		 * by the authzTo directive of the "proxyauthzdn".
		 */
		/*
		 * NOTE: current Proxy Authorization specification
		 * and implementation do not allow proxy authorization
		 * control to be provided with Bind requests
		 */
		/*
		 * if no bind took place yet, but the connection is bound
		 * and the "proxyauthzdn" is set, then bind as 
		 * "proxyauthzdn" and explicitly add the proxyAuthz 
		 * control to every operation with the dn bound 
		 * to the connection as control value.
		 */
		if ( op->o_conn != NULL && BER_BVISNULL( &lc->lc_bound_ndn ) ) {
			(void)ldap_back_proxy_authz_bind( lc, op, rs );
			goto done;
		}
#endif /* LDAP_BACK_PROXY_AUTHZ */

		rs->sr_err = ldap_sasl_bind( lc->lc_ld,
				lc->lc_bound_ndn.bv_val,
				LDAP_SASL_SIMPLE, &lc->lc_cred,
				NULL, NULL, &msgid );
		
		rc = ldap_back_op_result( lc, op, rs, msgid, 0 );
		if ( rc == LDAP_SUCCESS ) {
			lc->lc_bound = 1;
		}
	}

done:;
	rc = lc->lc_bound;
	ldap_pvt_thread_mutex_unlock( &lc->lc_mutex );
	return rc;
}

/*
 * ldap_back_rebind
 *
 * This is a callback used for chasing referrals using the same
 * credentials as the original user on this session.
 */
static int 
ldap_back_rebind( LDAP *ld, LDAP_CONST char *url, ber_tag_t request,
	ber_int_t msgid, void *params )
{
	struct ldapconn *lc = params;

	return ldap_sasl_bind_s( ld, lc->lc_bound_ndn.bv_val,
			LDAP_SASL_SIMPLE, &lc->lc_cred, NULL, NULL, NULL );
}

int
ldap_back_op_result(
		struct ldapconn	*lc,
		Operation	*op,
		SlapReply	*rs,
		ber_int_t	msgid,
		int		sendok )
{
	char		*match = NULL;
	LDAPMessage	*res = NULL;
	char		*text = NULL;

#define	ERR_OK(err) ((err) == LDAP_SUCCESS || (err) == LDAP_COMPARE_FALSE || (err) == LDAP_COMPARE_TRUE)

	rs->sr_text = NULL;
	rs->sr_matched = NULL;

	/* if the error recorded in the reply corresponds
	 * to a successful state, get the error from the
	 * remote server response */
	if ( ERR_OK( rs->sr_err ) ) {
		int		rc;
		struct timeval	tv = { 0, 0 };

retry:;
		/* if result parsing fails, note the failure reason */
		switch ( ldap_result( lc->lc_ld, msgid, 1, &tv, &res ) ) {
		case 0:
			tv.tv_sec = 0;
			tv.tv_usec = 100000;	/* 0.1 s */
			ldap_pvt_thread_yield();
			goto retry;

		case -1:
			ldap_get_option( lc->lc_ld, LDAP_OPT_ERROR_NUMBER,
					&rs->sr_err );
			break;


		/* otherwise get the result; if it is not
		 * LDAP_SUCCESS, record it in the reply
		 * structure (this includes 
		 * LDAP_COMPARE_{TRUE|FALSE}) */
		default:
			rc = ldap_parse_result( lc->lc_ld, res, &rs->sr_err,
					&match, &text, NULL, NULL, 1 );
			rs->sr_text = text;
			if ( rc != LDAP_SUCCESS ) {
				rs->sr_err = rc;
			}
		}
	}

	/* if the error in the reply structure is not
	 * LDAP_SUCCESS, try to map it from client 
	 * to server error */
	if ( !ERR_OK( rs->sr_err ) ) {
		rs->sr_err = slap_map_api2result( rs );

		/* internal ops ( op->o_conn == NULL ) 
		 * must not reply to client */
		if ( op->o_conn && !op->o_do_not_cache && match ) {

			/* record the (massaged) matched
			 * DN into the reply structure */
			rs->sr_matched = match;
		}
	}
	if ( op->o_conn && ( sendok || rs->sr_err != LDAP_SUCCESS ) ) {
		send_ldap_result( op, rs );
	}
	if ( match ) {
		if ( rs->sr_matched != match ) {
			free( (char *)rs->sr_matched );
		}
		rs->sr_matched = NULL;
		ldap_memfree( match );
	}
	if ( text ) {
		ldap_memfree( text );
	}
	rs->sr_text = NULL;
	return( ERR_OK( rs->sr_err ) ? 0 : -1 );
}

/* return true if bound, false if failed */
int
ldap_back_retry( struct ldapconn *lc, Operation *op, SlapReply *rs )
{
	struct ldapinfo	*li = (struct ldapinfo *)op->o_bd->be_private;
	int vers = op->o_protocol;
	LDAP *ld;

	ldap_pvt_thread_mutex_lock( &lc->lc_mutex );
	ldap_unbind_ext_s( lc->lc_ld, NULL, NULL );
	lc->lc_bound = 0;
	rs->sr_err = ldap_initialize( &ld, li->url );
		
	if ( rs->sr_err != LDAP_SUCCESS ) {
		rs->sr_err = slap_map_api2result( rs );
		if ( rs->sr_text == NULL ) {
			rs->sr_text = "ldap_initialize() failed";
		}
		if ( op->o_conn ) {
			send_ldap_result( op, rs );
		}
		rs->sr_text = NULL;
		return 0;
	}
	/* Set LDAP version. This will always succeed: If the client
	 * bound with a particular version, then so can we.
	 */
	ldap_set_option( ld, LDAP_OPT_PROTOCOL_VERSION, (const void *)&vers );
	/* FIXME: configurable? */
	ldap_set_option( ld, LDAP_OPT_REFERRALS, LDAP_OPT_ON );
	lc->lc_ld = ld;
	ldap_pvt_thread_mutex_unlock( &lc->lc_mutex );
	return ldap_back_dobind( lc, op, rs );
}

#ifdef LDAP_BACK_PROXY_AUTHZ
static int
ldap_back_proxy_authz_bind( struct ldapconn *lc, Operation *op, SlapReply *rs )
{
	struct ldapinfo *li = (struct ldapinfo *)op->o_bd->be_private;
	struct berval	binddn = slap_empty_bv;
	struct berval	bindcred = slap_empty_bv;
	int		dobind = 0;
	int		msgid;
	int		rc;

	/*
	 * FIXME: we need to let clients use proxyAuthz
	 * otherwise we cannot do symmetric pools of servers;
	 * we have to live with the fact that a user can
	 * authorize itself as any ID that is allowed
	 * by the authzTo directive of the "proxyauthzdn".
	 */
	/*
	 * NOTE: current Proxy Authorization specification
	 * and implementation do not allow proxy authorization
	 * control to be provided with Bind requests
	 */
	/*
	 * if no bind took place yet, but the connection is bound
	 * and the "proxyauthzdn" is set, then bind as 
	 * "proxyauthzdn" and explicitly add the proxyAuthz 
	 * control to every operation with the dn bound 
	 * to the connection as control value.
	 */

	/* bind as proxyauthzdn only if no idassert mode
	 * is requested, or if the client's identity
	 * is authorized */
	switch ( li->idassert_mode ) {
	case LDAP_BACK_IDASSERT_LEGACY:
		if ( !BER_BVISNULL( &op->o_conn->c_ndn ) && !BER_BVISEMPTY( &op->o_conn->c_ndn ) ) {
			if ( !BER_BVISNULL( &li->idassert_authcDN ) && !BER_BVISEMPTY( &li->idassert_authcDN ) )
			{
				binddn = li->idassert_authcDN;
				bindcred = li->idassert_passwd;
				dobind = 1;
			}
		}
		break;

	default:
		if ( li->idassert_authz ) {
			struct berval authcDN;

			if ( BER_BVISNULL( &op->o_conn->c_ndn ) ) {
				authcDN = slap_empty_bv;
			} else {
				authcDN = op->o_conn->c_ndn;
			}	
			rs->sr_err = slap_sasl_matches( op, li->idassert_authz,
					&authcDN, &authcDN );
			if ( rs->sr_err != LDAP_SUCCESS ) {
				send_ldap_result( op, rs );
				lc->lc_bound = 0;
				goto done;
			}
		}

		binddn = li->idassert_authcDN;
		bindcred = li->idassert_passwd;
		dobind = 1;
		break;
	}

	if ( dobind && li->idassert_authmethod == LDAP_AUTH_SASL ) {
#ifdef HAVE_CYRUS_SASL
		void		*defaults = NULL;
		struct berval	authzID = BER_BVNULL;
		int		freeauthz = 0;

		/* if SASL supports native authz, prepare for it */
		if ( ( !op->o_do_not_cache || !op->o_is_auth_check ) &&
				( li->idassert_flags & LDAP_BACK_AUTH_NATIVE_AUTHZ ) )
		{
			switch ( li->idassert_mode ) {
			case LDAP_BACK_IDASSERT_OTHERID:
			case LDAP_BACK_IDASSERT_OTHERDN:
				authzID = li->idassert_authzID;
				break;

			case LDAP_BACK_IDASSERT_ANONYMOUS:
				BER_BVSTR( &authzID, "dn:" );
				break;

			case LDAP_BACK_IDASSERT_SELF:
				if ( BER_BVISNULL( &op->o_conn->c_ndn ) ) {
					/* connection is not authc'd, so don't idassert */
					BER_BVSTR( &authzID, "dn:" );
					break;
				}
				authzID.bv_len = STRLENOF( "dn:" ) + op->o_conn->c_ndn.bv_len;
				authzID.bv_val = slap_sl_malloc( authzID.bv_len + 1, op->o_tmpmemctx );
				AC_MEMCPY( authzID.bv_val, "dn:", STRLENOF( "dn:" ) );
				AC_MEMCPY( authzID.bv_val + STRLENOF( "dn:" ),
						op->o_conn->c_ndn.bv_val, op->o_conn->c_ndn.bv_len + 1 );
				freeauthz = 1;
				break;

			default:
				break;
			}
		}

#if 0	/* will deal with this later... */
		if ( sasl_secprops != NULL ) {
			rs->sr_err = ldap_set_option( lc->lc_ld, LDAP_OPT_X_SASL_SECPROPS,
				(void *) sasl_secprops );

			if ( rs->sr_err != LDAP_OPT_SUCCESS ) {
				send_ldap_result( op, rs );
				lc->lc_bound = 0;
				goto done;
			}
		}
#endif

		defaults = lutil_sasl_defaults( lc->lc_ld,
				li->idassert_sasl_mech.bv_val,
				li->idassert_sasl_realm.bv_val,
				li->idassert_authcID.bv_val,
				li->idassert_passwd.bv_val,
				authzID.bv_val );

		rs->sr_err = ldap_sasl_interactive_bind_s( lc->lc_ld, binddn.bv_val,
				li->idassert_sasl_mech.bv_val, NULL, NULL,
				li->idassert_sasl_flags, lutil_sasl_interact,
				defaults );

		lutil_sasl_freedefs( defaults );
		if ( freeauthz ) {
			slap_sl_free( authzID.bv_val, op->o_tmpmemctx );
		}

		rs->sr_err = slap_map_api2result( rs );
		if ( rs->sr_err != LDAP_SUCCESS ) {
			lc->lc_bound = 0;
			send_ldap_result( op, rs );

		} else {
			lc->lc_bound = 1;
		}
		goto done;
#endif /* HAVE_CYRUS_SASL */
	}

	switch ( li->idassert_authmethod ) {
	case LDAP_AUTH_SIMPLE:
		rs->sr_err = ldap_sasl_bind( lc->lc_ld,
				binddn.bv_val, LDAP_SASL_SIMPLE,
				&bindcred, NULL, NULL, &msgid );
		break;

	case LDAP_AUTH_NONE:
		lc->lc_bound = 1;
		goto done;

	default:
		/* unsupported! */
		lc->lc_bound = 0;
		rs->sr_err = LDAP_AUTH_METHOD_NOT_SUPPORTED;
		send_ldap_result( op, rs );
		goto done;
	}

	rc = ldap_back_op_result( lc, op, rs, msgid, 0 );
	if ( rc == LDAP_SUCCESS ) {
		lc->lc_bound = 1;
	}
done:;
	return lc->lc_bound;
}

/*
 * ldap_back_proxy_authz_ctrl() prepends a proxyAuthz control
 * to existing server-side controls if required; if not,
 * the existing server-side controls are placed in *pctrls.
 * The caller, after using the controls in client API 
 * operations, if ( *pctrls != op->o_ctrls ), should
 * free( (*pctrls)[ 0 ] ) and free( *pctrls ).
 * The function returns success if the control could
 * be added if required, or if it did nothing; in the future,
 * it might return some error if it failed.
 * 
 * if no bind took place yet, but the connection is bound
 * and the "proxyauthzdn" is set, then bind as "proxyauthzdn" 
 * and explicitly add proxyAuthz the control to every operation
 * with the dn bound to the connection as control value.
 *
 * If no server-side controls are defined for the operation,
 * simply add the proxyAuthz control; otherwise, if the
 * proxyAuthz control is not already set, add it as
 * the first one (FIXME: is controls order significant
 * for security?).
 */
int
ldap_back_proxy_authz_ctrl(
		struct ldapconn	*lc,
		Operation	*op,
		SlapReply	*rs,
		LDAPControl	***pctrls )
{
	struct ldapinfo	*li = (struct ldapinfo *) op->o_bd->be_private;
	LDAPControl	**ctrls = NULL;
	int		i = 0,
			mode;
	struct berval	assertedID;

	*pctrls = NULL;

	rs->sr_err = LDAP_SUCCESS;

	if ( ( BER_BVISNULL( &li->idassert_authcID ) || BER_BVISEMPTY( &li->idassert_authcID ) )
			&& ( BER_BVISNULL( &li->idassert_authcDN ) || BER_BVISEMPTY( &li->idassert_authcDN ) ) ) {
		goto done;
	}

	if ( !op->o_conn ) {
		goto done;
	}

	if ( li->idassert_mode == LDAP_BACK_IDASSERT_LEGACY ) {
		if ( op->o_proxy_authz ) {
			/*
			 * FIXME: we do not want to perform proxyAuthz
			 * on behalf of the client, because this would
			 * be performed with "proxyauthzdn" privileges.
			 *
			 * This might actually be too strict, since
			 * the "proxyauthzdn" authzTo, and each entry's
			 * authzFrom attributes may be crafted
			 * to avoid unwanted proxyAuthz to take place.
			 */
#if 0
			rs->sr_err = LDAP_UNWILLING_TO_PERFORM;
			rs->sr_text = "proxyAuthz not allowed within namingContext";
#endif
			goto done;
		}

		if ( !BER_BVISNULL( &lc->lc_bound_ndn ) ) {
			goto done;
		}

		if ( BER_BVISNULL( &op->o_conn->c_ndn ) ) {
			goto done;
		}

		if ( BER_BVISNULL( &li->idassert_authcDN ) ) {
			goto done;
		}

	} else if ( li->idassert_authmethod == LDAP_AUTH_SASL ) {
		if ( ( li->idassert_flags & LDAP_BACK_AUTH_NATIVE_AUTHZ )
				/* && ( !BER_BVISNULL( &op->o_conn->c_ndn ) || lc->lc_bound ) */ )
		{
			/* already asserted in SASL via native authz */
			/* NOTE: the test on lc->lc_bound is used to trap
			 * native authorization of anonymous users,
			 * since in that case op->o_conn->c_ndn is NULL */
			goto done;
		}

	} else if ( li->idassert_authz ) {
		int		rc;
		struct berval authcDN;

		if ( BER_BVISNULL( &op->o_conn->c_ndn ) ) {
			authcDN = slap_empty_bv;
		} else {
			authcDN = op->o_conn->c_ndn;
		}
		rc = slap_sasl_matches( op, li->idassert_authz,
				&authcDN, & authcDN );
		if ( rc != LDAP_SUCCESS ) {
			/* op->o_conn->c_ndn is not authorized
			 * to use idassert */
			return rc;
		}
	}

	if ( op->o_proxy_authz ) {
		/*
		 * FIXME: we can:
		 * 1) ignore the already set proxyAuthz control
		 * 2) leave it in place, and don't set ours
		 * 3) add both
		 * 4) reject the operation
		 *
		 * option (4) is very drastic
		 * option (3) will make the remote server reject
		 * the operation, thus being equivalent to (4)
		 * option (2) will likely break the idassert
		 * assumptions, so we cannot accept it;
		 * option (1) means that we are contradicting
		 * the client's reques.
		 *
		 * I think (4) is the only correct choice.
		 */
		rs->sr_err = LDAP_UNWILLING_TO_PERFORM;
		rs->sr_text = "proxyAuthz not allowed within namingContext";
	}

	if ( op->o_do_not_cache && op->o_is_auth_check ) {
		mode = LDAP_BACK_IDASSERT_NOASSERT;

	} else {
		mode = li->idassert_mode;
	}

	switch ( mode ) {
	case LDAP_BACK_IDASSERT_LEGACY:
	case LDAP_BACK_IDASSERT_SELF:
		/* original behavior:
		 * assert the client's identity */
		if ( BER_BVISNULL( &op->o_conn->c_ndn ) ) {
			assertedID = slap_empty_bv;
		} else {
			assertedID = op->o_conn->c_ndn;
		}
		break;

	case LDAP_BACK_IDASSERT_ANONYMOUS:
		/* assert "anonymous" */
		assertedID = slap_empty_bv;
		break;

	case LDAP_BACK_IDASSERT_NOASSERT:
		/* don't assert; bind as proxyauthzdn */
		goto done;

	case LDAP_BACK_IDASSERT_OTHERID:
	case LDAP_BACK_IDASSERT_OTHERDN:
		/* assert idassert DN */
		assertedID = li->idassert_authzID;
		break;

	default:
		assert( 0 );
	}

	if ( BER_BVISNULL( &assertedID ) ) {
		assertedID = slap_empty_bv;
	}

	if ( op->o_ctrls ) {
		for ( i = 0; op->o_ctrls[ i ]; i++ )
			/* just count ctrls */ ;
	}

	ctrls = ch_malloc( sizeof( LDAPControl * ) * (i + 2) );
	ctrls[ 0 ] = ch_malloc( sizeof( LDAPControl ) );
	
	ctrls[ 0 ]->ldctl_oid = LDAP_CONTROL_PROXY_AUTHZ;
	ctrls[ 0 ]->ldctl_iscritical = 1;

	switch ( li->idassert_mode ) {
	/* already in u:ID or dn:DN form */
	case LDAP_BACK_IDASSERT_OTHERID:
	case LDAP_BACK_IDASSERT_OTHERDN:
		ber_dupbv( &ctrls[ 0 ]->ldctl_value, &assertedID );
		break;

	/* needs the dn: prefix */
	default:
		ctrls[ 0 ]->ldctl_value.bv_len = assertedID.bv_len + STRLENOF( "dn:" );
		ctrls[ 0 ]->ldctl_value.bv_val = ch_malloc( ctrls[ 0 ]->ldctl_value.bv_len + 1 );
		AC_MEMCPY( ctrls[ 0 ]->ldctl_value.bv_val, "dn:", STRLENOF( "dn:" ) );
		AC_MEMCPY( ctrls[ 0 ]->ldctl_value.bv_val + STRLENOF( "dn:" ),
				assertedID.bv_val, assertedID.bv_len + 1 );
		break;
	}

	if ( op->o_ctrls ) {
		for ( i = 0; op->o_ctrls[ i ]; i++ ) {
			ctrls[ i + 1 ] = op->o_ctrls[ i ];
		}
	}
	ctrls[ i + 1 ] = NULL;

done:;
	if ( ctrls == NULL ) {
		ctrls = op->o_ctrls;
	}

	*pctrls = ctrls;
	
	return rs->sr_err;
}

int
ldap_back_proxy_authz_ctrl_free( Operation *op, LDAPControl ***pctrls )
{
	LDAPControl	**ctrls = *pctrls;

	if ( ctrls && ctrls != op->o_ctrls ) {
		assert( ctrls[ 0 ] );

		if ( !BER_BVISNULL( &ctrls[ 0 ]->ldctl_value ) ) {
			free( ctrls[ 0 ]->ldctl_value.bv_val );
		}

		free( ctrls[ 0 ] );
		free( ctrls );
	} 

	*pctrls = NULL;

	return 0;
}
#endif /* LDAP_BACK_PROXY_AUTHZ */
