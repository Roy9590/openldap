/* $OpenLDAP$ */
/*
 * Copyright 1998-2003 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */
/* backend.c - routines for dealing with back-end databases */


#include "portable.h"

#include <stdio.h>

#include <ac/string.h>
#include <ac/socket.h>
#include <sys/stat.h>

#include "slap.h"
#include "lutil.h"
#include "lber_pvt.h"

#ifdef LDAP_SLAPI
#include "slapi.h"
#endif

/*
 * If a module is configured as dynamic, its header should not
 * get included into slapd. While this is a general rule and does
 * not have much of an effect in UNIX, this rule should be adhered
 * to for Windows, where dynamic object code should not be implicitly
 * imported into slapd without appropriate __declspec(dllimport) directives.
 */

#if defined(SLAPD_BDB) && !defined(SLAPD_BDB_DYNAMIC)
#include "back-bdb/external.h"
#endif
#if defined(SLAPD_DNSSRV) && !defined(SLAPD_DNSSRV_DYNAMIC)
#include "back-dnssrv/external.h"
#endif
#if defined(SLAPD_HDB) && !defined(SLAPD_HDB_DYNAMIC)
#include "back-hdb/external.h"
#endif
#if defined(SLAPD_LDAP) && !defined(SLAPD_LDAP_DYNAMIC)
#include "back-ldap/external.h"
#endif
#if defined(SLAPD_LDBM) && !defined(SLAPD_LDBM_DYNAMIC)
#include "back-ldbm/external.h"
#endif
#if defined(SLAPD_META) && !defined(SLAPD_META_DYNAMIC)
#include "back-meta/external.h"
#endif
#if defined(SLAPD_MONITOR) && !defined(SLAPD_MONITOR_DYNAMIC)
#include "back-monitor/external.h"
#endif
#if defined(SLAPD_NULL) && !defined(SLAPD_NULL_DYNAMIC)
#include "back-null/external.h"
#endif
#if defined(SLAPD_PASSWD) && !defined(SLAPD_PASSWD_DYNAMIC)
#include "back-passwd/external.h"
#endif
#if defined(SLAPD_PERL) && !defined(SLAPD_PERL_DYNAMIC)
#include "back-perl/external.h"
#endif
#if defined(SLAPD_SHELL) && !defined(SLAPD_SHELL_DYNAMIC)
#include "back-shell/external.h"
#endif
#if defined(SLAPD_TCL) && !defined(SLAPD_TCL_DYNAMIC)
#include "back-tcl/external.h"
#endif
#if defined(SLAPD_SQL) && !defined(SLAPD_SQL_DYNAMIC)
#include "back-sql/external.h"
#endif
#if defined(SLAPD_PRIVATE) && !defined(SLAPD_PRIVATE_DYNAMIC)
#include "private/external.h"
#endif

static BackendInfo binfo[] = {
#if defined(SLAPD_BDB) && !defined(SLAPD_BDB_DYNAMIC)
	{"bdb",	bdb_initialize},
#endif
#if defined(SLAPD_DNSSRV) && !defined(SLAPD_DNSSRV_DYNAMIC)
	{"dnssrv",	dnssrv_back_initialize},
#endif
#if defined(SLAPD_HDB) && !defined(SLAPD_HDB_DYNAMIC)
	{"hdb",	hdb_initialize},
#endif
#if defined(SLAPD_LDAP) && !defined(SLAPD_LDAP_DYNAMIC)
	{"ldap",	ldap_back_initialize},
#endif
#if defined(SLAPD_LDBM) && !defined(SLAPD_LDBM_DYNAMIC)
	{"ldbm",	ldbm_back_initialize},
#endif
#if defined(SLAPD_META) && !defined(SLAPD_META_DYNAMIC)
	{"meta",	meta_back_initialize},
#endif
#if defined(SLAPD_MONITOR) && !defined(SLAPD_MONITOR_DYNAMIC)
	{"monitor",	monitor_back_initialize},
#endif
#if defined(SLAPD_NULL) && !defined(SLAPD_NULL_DYNAMIC)
	{"null",	null_back_initialize},
#endif
#if defined(SLAPD_PASSWD) && !defined(SLAPD_PASSWD_DYNAMIC)
	{"passwd",	passwd_back_initialize},
#endif
#if defined(SLAPD_PERL) && !defined(SLAPD_PERL_DYNAMIC)
	{"perl",	perl_back_initialize},
#endif
#if defined(SLAPD_SHELL) && !defined(SLAPD_SHELL_DYNAMIC)
	{"shell",	shell_back_initialize},
#endif
#if defined(SLAPD_TCL) && !defined(SLAPD_TCL_DYNAMIC)
	{"tcl",		tcl_back_initialize},
#endif
#if defined(SLAPD_SQL) && !defined(SLAPD_SQL_DYNAMIC)
	{"sql",		sql_back_initialize},
#endif
	/* for any private backend */
#if defined(SLAPD_PRIVATE) && !defined(SLAPD_PRIVATE_DYNAMIC)
	{"private",	private_back_initialize},
#endif
	{NULL}
};

int			nBackendInfo = 0;
BackendInfo	*backendInfo = NULL;

int			nBackendDB = 0; 
BackendDB	*backendDB = NULL;

#ifdef LDAP_SYNCREPL
ldap_pvt_thread_pool_t	syncrepl_pool;
int			syncrepl_pool_max = SLAP_MAX_SYNCREPL_THREADS;
#endif

int backend_init(void)
{
	int rc = -1;

#ifdef LDAP_SYNCREPL
        ldap_pvt_thread_pool_init( &syncrepl_pool, syncrepl_pool_max, 0 );
#endif

	if((nBackendInfo != 0) || (backendInfo != NULL)) {
		/* already initialized */
#ifdef NEW_LOGGING
		LDAP_LOG( BACKEND, ERR, 
			"backend_init:  backend already initialized\n", 0, 0, 0 );
#else
		Debug( LDAP_DEBUG_ANY,
			"backend_init: already initialized.\n", 0, 0, 0 );
#endif
		return -1;
	}

	for( ;
		binfo[nBackendInfo].bi_type != NULL;
		nBackendInfo++ )
	{
		rc = binfo[nBackendInfo].bi_init( &binfo[nBackendInfo] );

		if(rc != 0) {
#ifdef NEW_LOGGING
			LDAP_LOG( BACKEND, INFO, 
				"backend_init:  initialized for type \"%s\"\n",
				binfo[nBackendInfo].bi_type, 0, 0 );
#else
			Debug( LDAP_DEBUG_ANY,
				"backend_init: initialized for type \"%s\"\n",
				binfo[nBackendInfo].bi_type, 0, 0 );
#endif
			/* destroy those we've already inited */
			for( nBackendInfo--;
				nBackendInfo >= 0 ;
				nBackendInfo-- )
			{ 
				if ( binfo[nBackendInfo].bi_destroy ) {
					binfo[nBackendInfo].bi_destroy(
						&binfo[nBackendInfo] );
				}
			}
			return rc;
		}
	}

	if ( nBackendInfo > 0) {
		backendInfo = binfo;
		return 0;
	}

#ifdef SLAPD_MODULES	
	return 0;
#else

#ifdef NEW_LOGGING
	LDAP_LOG( BACKEND, ERR, "backend_init: failed\n", 0, 0, 0 );
#else
	Debug( LDAP_DEBUG_ANY,
		"backend_init: failed\n",
		0, 0, 0 );
#endif

	return rc;
#endif /* SLAPD_MODULES */
}

int backend_add(BackendInfo *aBackendInfo)
{
   int rc = 0;

   if ((rc = aBackendInfo->bi_init(aBackendInfo)) != 0) {
#ifdef NEW_LOGGING
       	LDAP_LOG( BACKEND, ERR, 
                  "backend_add:  initialization for type \"%s\" failed\n",
                  aBackendInfo->bi_type, 0, 0 );
#else
      Debug( LDAP_DEBUG_ANY,
	     "backend_add: initialization for type \"%s\" failed\n",
	     aBackendInfo->bi_type, 0, 0 );
#endif
      return rc;
   }

   /* now add the backend type to the Backend Info List */
   {
      BackendInfo *newBackendInfo = 0;

      /* if backendInfo == binfo no deallocation of old backendInfo */
      if (backendInfo == binfo) {
	 newBackendInfo = ch_calloc(nBackendInfo + 1, sizeof(BackendInfo));
	 AC_MEMCPY(newBackendInfo, backendInfo, sizeof(BackendInfo) * 
		nBackendInfo);
      } else {
	 newBackendInfo = ch_realloc(backendInfo, sizeof(BackendInfo) * 
				     (nBackendInfo + 1));
      }
      AC_MEMCPY(&newBackendInfo[nBackendInfo], aBackendInfo, 
	     sizeof(BackendInfo));
      backendInfo = newBackendInfo;
      nBackendInfo++;

      return 0;
   }	    
}

int backend_startup(Backend *be)
{
	int i;
	int rc = 0;

#ifdef LDAP_SYNCREPL
	init_syncrepl();
#endif

	if( ! ( nBackendDB > 0 ) ) {
		/* no databases */
#ifdef NEW_LOGGING
		LDAP_LOG( BACKEND, INFO, 
			"backend_startup: %d databases to startup. \n", nBackendDB, 0, 0 );
#else
		Debug( LDAP_DEBUG_ANY,
			"backend_startup: %d databases to startup.\n",
			nBackendDB, 0, 0 );
#endif
		return 1;
	}

	if(be != NULL) {
		/* startup a specific backend database */
#ifdef NEW_LOGGING
		LDAP_LOG( BACKEND, DETAIL1, "backend_startup:  starting \"%s\"\n",
			   be->be_suffix[0].bv_val, 0, 0 );
#else
		Debug( LDAP_DEBUG_TRACE,
			"backend_startup: starting \"%s\"\n",
			be->be_suffix[0].bv_val, 0, 0 );
#endif

		if ( be->bd_info->bi_open ) {
			rc = be->bd_info->bi_open( be->bd_info );
			if ( rc != 0 ) {
#ifdef NEW_LOGGING
				LDAP_LOG( BACKEND, CRIT, "backend_startup: bi_open failed!\n", 0, 0, 0 );
#else
				Debug( LDAP_DEBUG_ANY,
					"backend_startup: bi_open failed!\n",
					0, 0, 0 );
#endif

				return rc;
			}
		}

		if ( be->bd_info->bi_db_open ) {
			rc = be->bd_info->bi_db_open( be );
			if ( rc != 0 ) {
#ifdef NEW_LOGGING
				LDAP_LOG( BACKEND, CRIT, 
					"backend_startup: bi_db_open failed! (%d)\n", rc, 0, 0 );
#else
				Debug( LDAP_DEBUG_ANY,
					"backend_startup: bi_db_open failed! (%d)\n",
					rc, 0, 0 );
#endif
				return rc;
			}
		}

		return rc;
	}

	/* open each backend type */
	for( i = 0; i < nBackendInfo; i++ ) {
		if( backendInfo[i].bi_nDB == 0) {
			/* no database of this type, don't open */
			continue;
		}

		if( backendInfo[i].bi_open ) {
			rc = backendInfo[i].bi_open(
				&backendInfo[i] );
			if ( rc != 0 ) {
#ifdef NEW_LOGGING
				LDAP_LOG( BACKEND, CRIT, 
					"backend_startup: bi_open %d failed!\n", i, 0, 0 );
#else
				Debug( LDAP_DEBUG_ANY,
					"backend_startup: bi_open %d failed!\n",
					i, 0, 0 );
#endif
				return rc;
			}
		}
	}

	/* open each backend database */
	for( i = 0; i < nBackendDB; i++ ) {
		/* append global access controls */
		acl_append( &backendDB[i].be_acl, global_acl );

		if ( backendDB[i].bd_info->bi_db_open ) {
			rc = backendDB[i].bd_info->bi_db_open(
				&backendDB[i] );
			if ( rc != 0 ) {
#ifdef NEW_LOGGING
				LDAP_LOG( BACKEND, CRIT, 
					"backend_startup: bi_db_open(%d) failed! (%d)\n", i, rc, 0 );
#else
				Debug( LDAP_DEBUG_ANY,
					"backend_startup: bi_db_open(%d) failed! (%d)\n",
					i, rc, 0 );
#endif
				return rc;
			}
		}

#ifdef LDAP_SYNCREPL
		if ( backendDB[i].syncinfo != NULL ) {
			syncinfo_t *si = ( syncinfo_t * ) backendDB[i].syncinfo;
			ldap_pvt_runqueue_insert( &syncrepl_rq, si->interval,
							(void *) &backendDB[i] );
		}
#endif
	}

	return rc;
}

int backend_num( Backend *be )
{
	int i;

	if( be == NULL ) return -1;

	for( i = 0; i < nBackendDB; i++ ) {
		if( be == &backendDB[i] ) return i;
	}
	return -1;
}

int backend_shutdown( Backend *be )
{
	int i;
	int rc = 0;

	if( be != NULL ) {
		/* shutdown a specific backend database */

		if ( be->bd_info->bi_nDB == 0 ) {
			/* no database of this type, we never opened it */
			return 0;
		}

		if ( be->bd_info->bi_db_close ) {
			be->bd_info->bi_db_close( be );
		}

		if( be->bd_info->bi_close ) {
			be->bd_info->bi_close( be->bd_info );
		}

		return 0;
	}

	/* close each backend database */
	for( i = 0; i < nBackendDB; i++ ) {
		if ( backendDB[i].bd_info->bi_db_close ) {
			backendDB[i].bd_info->bi_db_close(
				&backendDB[i] );
		}

		if(rc != 0) {
#ifdef NEW_LOGGING
			LDAP_LOG( BACKEND, NOTICE, 
				"backend_shutdown: bi_close %s failed!\n",
				backendDB[i].be_type, 0, 0 );
#else
			Debug( LDAP_DEBUG_ANY,
				"backend_close: bi_close %s failed!\n",
				backendDB[i].be_type, 0, 0 );
#endif
		}
	}

	/* close each backend type */
	for( i = 0; i < nBackendInfo; i++ ) {
		if( backendInfo[i].bi_nDB == 0 ) {
			/* no database of this type */
			continue;
		}

		if( backendInfo[i].bi_close ) {
			backendInfo[i].bi_close(
				&backendInfo[i] );
		}
	}

	return 0;
}

int backend_destroy(void)
{
	int i;
	BackendDB *bd;

#ifdef LDAP_SYNCREPL
        ldap_pvt_thread_pool_destroy( &syncrepl_pool, 1 );
#endif

	/* destroy each backend database */
	for( i = 0, bd = backendDB; i < nBackendDB; i++, bd++ ) {
		if ( bd->bd_info->bi_db_destroy ) {
			bd->bd_info->bi_db_destroy( bd );
		}
		ber_bvarray_free( bd->be_suffix );
		ber_bvarray_free( bd->be_nsuffix );
		if ( bd->be_rootdn.bv_val ) free( bd->be_rootdn.bv_val );
		if ( bd->be_rootndn.bv_val ) free( bd->be_rootndn.bv_val );
		if ( bd->be_rootpw.bv_val ) free( bd->be_rootpw.bv_val );
		acl_destroy( bd->be_acl, global_acl );
	}
	free( backendDB );

	/* destroy each backend type */
	for( i = 0; i < nBackendInfo; i++ ) {
		if( backendInfo[i].bi_destroy ) {
			backendInfo[i].bi_destroy(
				&backendInfo[i] );
		}
	}

#ifdef SLAPD_MODULES
	if (backendInfo != binfo) {
	   free(backendInfo);
	}
#endif /* SLAPD_MODULES */

	nBackendInfo = 0;
	backendInfo = NULL;

	return 0;
}

BackendInfo* backend_info(const char *type)
{
	int i;

	/* search for the backend type */
	for( i = 0; i < nBackendInfo; i++ ) {
		if( strcasecmp(backendInfo[i].bi_type, type) == 0 ) {
			return &backendInfo[i];
		}
	}

	return NULL;
}


BackendDB *
backend_db_init(
    const char	*type
)
{
	Backend	*be;
	BackendInfo *bi = backend_info(type);
	int	rc = 0;

	if( bi == NULL ) {
		fprintf( stderr, "Unrecognized database type (%s)\n", type );
		return NULL;
	}

	backendDB = (BackendDB *) ch_realloc(
			(char *) backendDB,
		    (nBackendDB + 1) * sizeof(Backend) );

	memset( &backendDB[nbackends], '\0', sizeof(Backend) );

	be = &backends[nbackends++];

	be->bd_info = bi;
	be->be_def_limit = deflimit;
	be->be_dfltaccess = global_default_access;

	be->be_restrictops = global_restrictops;
	be->be_requires = global_requires;
	be->be_ssf_set = global_ssf_set;

#ifdef LDAP_SYNCREPL
        be->syncinfo = NULL;
#endif

 	/* assign a default depth limit for alias deref */
	be->be_max_deref_depth = SLAPD_DEFAULT_MAXDEREFDEPTH; 

	if(bi->bi_db_init) {
		rc = bi->bi_db_init( be );
	}

	if(rc != 0) {
		fprintf( stderr, "database init failed (%s)\n", type );
		nbackends--;
		return NULL;
	}

	bi->bi_nDB++;
	return( be );
}

void
be_db_close( void )
{
	int	i;

	for ( i = 0; i < nbackends; i++ ) {
		if ( backends[i].bd_info->bi_db_close ) {
			(*backends[i].bd_info->bi_db_close)( &backends[i] );
		}
	}
}

Backend *
select_backend(
	struct berval * dn,
	int manageDSAit,
	int noSubs )
{
	int	i, j;
	ber_len_t len, dnlen = dn->bv_len;
	Backend *be = NULL;

	for ( i = 0; i < nbackends; i++ ) {
		for ( j = 0; backends[i].be_nsuffix != NULL &&
		    backends[i].be_nsuffix[j].bv_val != NULL; j++ )
		{
			if ( ( SLAP_GLUE_SUBORDINATE( &backends[i] ) )
				&& noSubs )
			{
			  	continue;
			}

			len = backends[i].be_nsuffix[j].bv_len;

			if ( len > dnlen ) {
				/* suffix is longer than DN */
				continue;
			}
			
			/*
			 * input DN is normalized, so the separator check
			 * need not look at escaping
			 */
			if ( len && len < dnlen &&
				!DN_SEPARATOR( dn->bv_val[(dnlen-len)-1] ))
			{
				continue;
			}

			if ( strcmp( backends[i].be_nsuffix[j].bv_val,
				&dn->bv_val[dnlen-len] ) == 0 )
			{
				if( be == NULL ) {
					be = &backends[i];

					if( manageDSAit && len == dnlen ) {
						continue;
					}
				} else {
					be = &backends[i];
				}
				return be;
			}
		}
	}

	return be;
}

int
be_issuffix(
    Backend	*be,
    struct berval	*bvsuffix
)
{
	int	i;

	for ( i = 0; be->be_nsuffix != NULL && be->be_nsuffix[i].bv_val != NULL; i++ ) {
		if ( bvmatch( &be->be_nsuffix[i], bvsuffix ) ) {
			return( 1 );
		}
	}

	return( 0 );
}

int
be_isroot( Backend *be, struct berval *ndn )
{
	if ( !ndn->bv_len ) {
		return( 0 );
	}

	if ( !be->be_rootndn.bv_len ) {
		return( 0 );
	}

	return dn_match( &be->be_rootndn, ndn );
}

int
be_isupdate( Backend *be, struct berval *ndn )
{
	if ( !ndn->bv_len ) {
		return( 0 );
	}

	if ( !be->be_update_ndn.bv_len ) {
		return( 0 );
	}

	return dn_match( &be->be_update_ndn, ndn );
}

struct berval *
be_root_dn( Backend *be )
{
	return &be->be_rootdn;
}

int
be_isroot_pw( Operation *op )
{
	int result;
	char *errmsg;

	if ( ! be_isroot( op->o_bd, &op->o_req_ndn ) ) {
		return 0;
	}

	if( op->o_bd->be_rootpw.bv_len == 0 ) {
		return 0;
	}

#if defined( SLAPD_CRYPT ) || defined( SLAPD_SPASSWD )
	ldap_pvt_thread_mutex_lock( &passwd_mutex );
#ifdef SLAPD_SPASSWD
	lutil_passwd_sasl_conn = op->o_conn->c_sasl_authctx;
#endif
#endif

	result = lutil_passwd( &op->o_bd->be_rootpw, &op->orb_cred, NULL, NULL );

#if defined( SLAPD_CRYPT ) || defined( SLAPD_SPASSWD )
#ifdef SLAPD_SPASSWD
	lutil_passwd_sasl_conn = NULL;
#endif
	ldap_pvt_thread_mutex_unlock( &passwd_mutex );
#endif

	return result == 0;
}

int
be_entry_release_rw(
	Operation *op,
	Entry *e,
	int rw )
{
	if ( op->o_bd->be_release ) {
		/* free and release entry from backend */
		return op->o_bd->be_release( op, e, rw );
	} else {
		/* free entry */
		entry_free( e );
		return 0;
	}
}

int
backend_unbind( Operation *op, SlapReply *rs )
{
	int		i;
#if defined( LDAP_SLAPI )
	Slapi_PBlock *pb = op->o_pb;

	int     rc;
	slapi_x_pblock_set_operation( pb, op );
#endif /* defined( LDAP_SLAPI ) */

	for ( i = 0; i < nbackends; i++ ) {
#if defined( LDAP_SLAPI )
		slapi_pblock_set( pb, SLAPI_BACKEND, (void *)&backends[i] );
		rc = doPluginFNs( &backends[i], SLAPI_PLUGIN_PRE_UNBIND_FN,
				(Slapi_PBlock *)pb );
		if ( rc < 0 ) {
			/*
			 * A preoperation plugin failure will abort the
			 * entire operation.
			 */
#ifdef NEW_LOGGING
			LDAP_LOG( OPERATION, INFO, "do_bind: Unbind preoperation plugin "
					"failed\n", 0, 0, 0);
#else
			Debug(LDAP_DEBUG_TRACE, "do_bind: Unbind preoperation plugin "
					"failed.\n", 0, 0, 0);
#endif
			return 0;
		}
#endif /* defined( LDAP_SLAPI ) */

		if ( backends[i].be_unbind ) {
			op->o_bd = &backends[i];
			(*backends[i].be_unbind)( op, rs );
		}

#if defined( LDAP_SLAPI )
		if ( doPluginFNs( &backends[i], SLAPI_PLUGIN_POST_UNBIND_FN,
				(Slapi_PBlock *)pb ) < 0 ) {
#ifdef NEW_LOGGING
			LDAP_LOG( OPERATION, INFO, "do_unbind: Unbind postoperation plugins "
					"failed\n", 0, 0, 0);
#else
			Debug(LDAP_DEBUG_TRACE, "do_unbind: Unbind postoperation plugins "
					"failed.\n", 0, 0, 0);
#endif
		}
#endif /* defined( LDAP_SLAPI ) */
	}

	return 0;
}

int
backend_connection_init(
	Connection   *conn
)
{
	int	i;

	for ( i = 0; i < nbackends; i++ ) {
		if ( backends[i].be_connection_init ) {
			(*backends[i].be_connection_init)( &backends[i], conn);
		}
	}

	return 0;
}

int
backend_connection_destroy(
	Connection   *conn
)
{
	int	i;

	for ( i = 0; i < nbackends; i++ ) {
		if ( backends[i].be_connection_destroy ) {
			(*backends[i].be_connection_destroy)( &backends[i], conn);
		}
	}

	return 0;
}

static int
backend_check_controls(
	Operation *op,
	SlapReply *rs )
{
	LDAPControl **ctrls = op->o_ctrls;
	rs->sr_err = LDAP_SUCCESS;

	if( ctrls ) {
		for( ; *ctrls != NULL ; ctrls++ ) {
			if( (*ctrls)->ldctl_iscritical &&
				!ldap_charray_inlist( op->o_bd->be_controls, (*ctrls)->ldctl_oid ) )
			{
				rs->sr_text = "control unavailable in context";
				rs->sr_err = LDAP_UNAVAILABLE_CRITICAL_EXTENSION;
				break;
			}
		}
	}

	return rs->sr_err;
}

int
backend_check_restrictions(
	Operation *op,
	SlapReply *rs,
	struct berval *opdata )
{
	slap_mask_t restrictops;
	slap_mask_t requires;
	slap_mask_t opflag;
	slap_ssf_set_t *ssf;
	int updateop = 0;
	int starttls = 0;
	int session = 0;

	if( op->o_bd ) {
		if ( backend_check_controls( op, rs ) != LDAP_SUCCESS ) {
			return rs->sr_err;
		}

		restrictops = op->o_bd->be_restrictops;
		requires = op->o_bd->be_requires;
		ssf = &op->o_bd->be_ssf_set;

	} else {
		restrictops = global_restrictops;
		requires = global_requires;
		ssf = &global_ssf_set;
	}

	switch( op->o_tag ) {
	case LDAP_REQ_ADD:
		opflag = SLAP_RESTRICT_OP_ADD;
		updateop++;
		break;
	case LDAP_REQ_BIND:
		opflag = SLAP_RESTRICT_OP_BIND;
		session++;
		break;
	case LDAP_REQ_COMPARE:
		opflag = SLAP_RESTRICT_OP_COMPARE;
		break;
	case LDAP_REQ_DELETE:
		updateop++;
		opflag = SLAP_RESTRICT_OP_DELETE;
		break;
	case LDAP_REQ_EXTENDED:
		opflag = SLAP_RESTRICT_OP_EXTENDED;

		if( !opdata ) {
			/* treat unspecified as a modify */
			opflag = SLAP_RESTRICT_OP_MODIFY;
			updateop++;
			break;
		}

		{
			if( bvmatch( opdata, &slap_EXOP_START_TLS ) ) {
				session++;
				starttls++;
				break;
			}
		}

		{
			if( bvmatch( opdata, &slap_EXOP_WHOAMI ) ) {
				break;
			}
		}

#ifdef LDAP_EXOP_X_CANCEL
		{
			if ( bvmatch( opdata, &slap_EXOP_CANCEL ) ) {
				break;
			}
		}
#endif

		/* treat everything else as a modify */
		opflag = SLAP_RESTRICT_OP_MODIFY;
		updateop++;
		break;

	case LDAP_REQ_MODIFY:
		updateop++;
		opflag = SLAP_RESTRICT_OP_MODIFY;
		break;
	case LDAP_REQ_RENAME:
		updateop++;
		opflag = SLAP_RESTRICT_OP_RENAME;
		break;
	case LDAP_REQ_SEARCH:
		opflag = SLAP_RESTRICT_OP_SEARCH;
		break;
	case LDAP_REQ_UNBIND:
		session++;
		opflag = 0;
		break;
	default:
		rs->sr_text = "restrict operations internal error";
		rs->sr_err = LDAP_OTHER;
		return rs->sr_err;
	}

	if ( !starttls ) {
		/* these checks don't apply to StartTLS */

		rs->sr_err = LDAP_CONFIDENTIALITY_REQUIRED;
		if( op->o_transport_ssf < ssf->sss_transport ) {
			rs->sr_text = "transport confidentiality required";
			return rs->sr_err;
		}

		if( op->o_tls_ssf < ssf->sss_tls ) {
			rs->sr_text = "TLS confidentiality required";
			return rs->sr_err;
		}


		if( op->o_tag == LDAP_REQ_BIND && opdata == NULL ) {
			/* simple bind specific check */
			if( op->o_ssf < ssf->sss_simple_bind ) {
				rs->sr_text = "confidentiality required";
				return rs->sr_err;
			}
		}

		if( op->o_tag != LDAP_REQ_BIND || opdata == NULL ) {
			/* these checks don't apply to SASL bind */

			if( op->o_sasl_ssf < ssf->sss_sasl ) {
				rs->sr_text = "SASL confidentiality required";
				return rs->sr_err;
			}

			if( op->o_ssf < ssf->sss_ssf ) {
				rs->sr_text = "confidentiality required";
				return rs->sr_err;
			}
		}

		if( updateop ) {
			if( op->o_transport_ssf < ssf->sss_update_transport ) {
				rs->sr_text = "transport update confidentiality required";
				return rs->sr_err;
			}

			if( op->o_tls_ssf < ssf->sss_update_tls ) {
				rs->sr_text = "TLS update confidentiality required";
				return rs->sr_err;
			}

			if( op->o_sasl_ssf < ssf->sss_update_sasl ) {
				rs->sr_text = "SASL update confidentiality required";
				return rs->sr_err;
			}

			if( op->o_ssf < ssf->sss_update_ssf ) {
				rs->sr_text = "update confidentiality required";
				return rs->sr_err;
			}

			if( !( global_allows & SLAP_ALLOW_UPDATE_ANON ) &&
				op->o_ndn.bv_len == 0 )
			{
				rs->sr_text = "modifications require authentication";
				rs->sr_err = LDAP_STRONG_AUTH_REQUIRED;
				return rs->sr_err;
			}

#ifdef SLAP_X_LISTENER_MOD
			if ( op->o_conn->c_listener && ! ( op->o_conn->c_listener->sl_perms & ( op->o_ndn.bv_len > 0 ? S_IWUSR : S_IWOTH ) ) ) {
				/* no "w" mode means readonly */
				rs->sr_text = "modifications not allowed on this listener";
				rs->sr_err = LDAP_UNWILLING_TO_PERFORM;
				return rs->sr_err;
			}
#endif /* SLAP_X_LISTENER_MOD */
		}
	}

	if ( !session ) {
		/* these checks don't apply to Bind, StartTLS, or Unbind */

		if( requires & SLAP_REQUIRE_STRONG ) {
			/* should check mechanism */
			if( ( op->o_transport_ssf < ssf->sss_transport
				&& op->o_authmech.bv_len == 0 ) || op->o_dn.bv_len == 0 )
			{
				rs->sr_text = "strong authentication required";
				rs->sr_err = LDAP_STRONG_AUTH_REQUIRED;
				return rs->sr_err;
			}
		}

		if( requires & SLAP_REQUIRE_SASL ) {
			if( op->o_authmech.bv_len == 0 || op->o_dn.bv_len == 0 ) {
				rs->sr_text = "SASL authentication required";
				rs->sr_err = LDAP_STRONG_AUTH_REQUIRED;
				return rs->sr_err;
			}
		}
			
		if( requires & SLAP_REQUIRE_AUTHC ) {
			if( op->o_dn.bv_len == 0 ) {
				rs->sr_text = "authentication required";
				rs->sr_err = LDAP_UNWILLING_TO_PERFORM;
				return rs->sr_err;
			}
		}

		if( requires & SLAP_REQUIRE_BIND ) {
			int version;
			ldap_pvt_thread_mutex_lock( &op->o_conn->c_mutex );
			version = op->o_conn->c_protocol;
			ldap_pvt_thread_mutex_unlock( &op->o_conn->c_mutex );

			if( !version ) {
				/* no bind has occurred */
				rs->sr_text = "BIND required";
				rs->sr_err = LDAP_OPERATIONS_ERROR;
				return rs->sr_err;
			}
		}

		if( requires & SLAP_REQUIRE_LDAP_V3 ) {
			if( op->o_protocol < LDAP_VERSION3 ) {
				/* no bind has occurred */
				rs->sr_text = "operation restricted to LDAPv3 clients";
				rs->sr_err = LDAP_OPERATIONS_ERROR;
				return rs->sr_err;
			}
		}

#ifdef SLAP_X_LISTENER_MOD
		if ( !starttls && op->o_dn.bv_len == 0 ) {
			if ( op->o_conn->c_listener && ! ( op->o_conn->c_listener->sl_perms & S_IXOTH ) ) {
				/* no "x" mode means bind required */
				rs->sr_text = "bind required on this listener";
				rs->sr_err = LDAP_STRONG_AUTH_REQUIRED;
				return rs->sr_err;
			}
		}

		if ( !starttls && !updateop ) {
			if ( op->o_conn->c_listener && ! ( op->o_conn->c_listener->sl_perms & ( op->o_dn.bv_len > 0 ? S_IRUSR : S_IROTH ) ) ) {
				/* no "r" mode means no read */
				rs->sr_text = "read not allowed on this listener";
				rs->sr_err = LDAP_UNWILLING_TO_PERFORM;
				return rs->sr_err;
			}
		}
#endif /* SLAP_X_LISTENER_MOD */

	}

	if( restrictops & opflag ) {
		if( restrictops == SLAP_RESTRICT_OP_READS ) {
			rs->sr_text = "read operations restricted";
		} else {
			rs->sr_text = "operation restricted";
		}
		rs->sr_err = LDAP_UNWILLING_TO_PERFORM;
		return rs->sr_err;
 	}

	rs->sr_err = LDAP_SUCCESS;
	return rs->sr_err;
}

int backend_check_referrals( Operation *op, SlapReply *rs )
{
	rs->sr_err = LDAP_SUCCESS;

	if( op->o_bd->be_chk_referrals ) {
		rs->sr_err = op->o_bd->be_chk_referrals( op, rs );

		if( rs->sr_err != LDAP_SUCCESS && rs->sr_err != LDAP_REFERRAL ) {
			send_ldap_result( op, rs );
		}
	}

	return rs->sr_err;
}

int
be_entry_get_rw(
	Operation *op,
	struct berval *ndn,
	ObjectClass *oc,
	AttributeDescription *at,
	int rw,
	Entry **e )
{
	Backend *be = op->o_bd;
	int rc;

	*e = NULL;

	op->o_bd = select_backend( ndn, 0, 0 );

	if (op->o_bd == NULL) {
		rc = LDAP_NO_SUCH_OBJECT;
	} else if ( op->o_bd->be_fetch ) {
		rc = ( op->o_bd->be_fetch )( op, ndn,
			oc, at, rw, e );
	} else {
		rc = LDAP_UNWILLING_TO_PERFORM;
	}
	op->o_bd = be;
	return rc;
}

int 
backend_group(
	Operation *op,
	Entry	*target,
	struct berval *gr_ndn,
	struct berval *op_ndn,
	ObjectClass *group_oc,
	AttributeDescription *group_at
)
{
	Entry *e;
	Attribute *a;
	int rc;
	GroupAssertion *g;

	if ( op->o_abandon ) return SLAPD_ABANDON;

	ldap_pvt_thread_mutex_lock( &op->o_conn->c_mutex );

	for (g = op->o_conn->c_groups; g; g=g->ga_next) {
		if (g->ga_be != op->o_bd || g->ga_oc != group_oc ||
			g->ga_at != group_at || g->ga_len != gr_ndn->bv_len)
			continue;
		if (strcmp( g->ga_ndn, gr_ndn->bv_val ) == 0)
			break;
	}

	ldap_pvt_thread_mutex_unlock( &op->o_conn->c_mutex );

	if (g) {
		return g->ga_res;
	}

	if ( target && dn_match( &target->e_nname, gr_ndn ) ) {
		e = target;
	} else {
		rc = be_entry_get_rw(op, gr_ndn, group_oc, group_at, 0, &e );
	}
	if ( e ) {
		a = attr_find( e->e_attrs, group_at );
		if ( a ) {
			rc = value_find_ex( group_at,
				SLAP_MR_ATTRIBUTE_VALUE_NORMALIZED_MATCH |
				SLAP_MR_ASSERTED_VALUE_NORMALIZED_MATCH,
				a->a_nvals, op_ndn, op->o_tmpmemctx );
		} else {
			rc = LDAP_NO_SUCH_ATTRIBUTE;
		}
		if (e != target ) {
			be_entry_release_r( op, e );
		}
	} else {
		rc = LDAP_NO_SUCH_OBJECT;
	}

	if ( op->o_tag != LDAP_REQ_BIND && !op->o_do_not_cache ) {
		g = ch_malloc(sizeof(GroupAssertion) + gr_ndn->bv_len);
		g->ga_be = op->o_bd;
		g->ga_oc = group_oc;
		g->ga_at = group_at;
		g->ga_res = rc;
		g->ga_len = gr_ndn->bv_len;
		strcpy(g->ga_ndn, gr_ndn->bv_val);
		ldap_pvt_thread_mutex_lock( &op->o_conn->c_mutex );
		g->ga_next = op->o_conn->c_groups;
		op->o_conn->c_groups = g;
		ldap_pvt_thread_mutex_unlock( &op->o_conn->c_mutex );
	}

	return rc;
}

int 
backend_attribute(
	Operation *op,
	Entry	*target,
	struct berval	*edn,
	AttributeDescription *entry_at,
	BerVarray *vals
)
{
	Entry *e;
	Attribute *a;
	int i, j, rc = LDAP_SUCCESS;
	AccessControlState acl_state = ACL_STATE_INIT;

	if ( target && dn_match( &target->e_nname, edn ) ) {
		e = target;
	} else {
		rc = be_entry_get_rw(op, edn, NULL, entry_at, 0, &e );
		if ( rc != LDAP_SUCCESS ) return rc;
	} 

	if ( e ) {
		a = attr_find( e->e_attrs, entry_at );
		if ( a ) {
			BerVarray v;

			if ( op->o_conn && access_allowed( op,
				e, entry_at, NULL, ACL_AUTH,
				&acl_state ) == 0 ) {
				rc = LDAP_INSUFFICIENT_ACCESS;
				goto freeit;
			}

			for ( i=0; a->a_vals[i].bv_val; i++ ) ;
			
			v = op->o_tmpalloc( sizeof(struct berval) * (i+1), op->o_tmpmemctx );
			for ( i=0,j=0; a->a_vals[i].bv_val; i++ ) {
				if ( op->o_conn && access_allowed( op,
					e, entry_at,
					&a->a_nvals[i],
					ACL_AUTH, &acl_state ) == 0 ) {
					continue;
				}
				ber_dupbv_x( &v[j],
					&a->a_nvals[i], op->o_tmpmemctx );
				if (v[j].bv_val ) j++;
			}
			if (j == 0) {
				op->o_tmpfree( v, op->o_tmpmemctx );
				*vals = NULL;
				rc = LDAP_INSUFFICIENT_ACCESS;
			} else {
				v[j].bv_val = NULL;
				v[j].bv_len = 0;
				*vals = v;
				rc = LDAP_SUCCESS;
			}
		}
freeit:		if (e != target ) {
			be_entry_release_r( op, e );
		}
	}

	return rc;
}

Attribute *backend_operational(
	Operation *op,
	SlapReply *rs,
	int opattrs	)
{
	Attribute *a = NULL, **ap = &a;

	/*
	 * If operational attributes (allegedly) are required, 
	 * and the backend supports specific operational attributes, 
	 * add them to the attribute list
	 */
	if ( opattrs || ( op->ors_attrs &&
		ad_inlist( slap_schema.si_ad_subschemaSubentry, op->ors_attrs )) ) {
		*ap = slap_operational_subschemaSubentry( op->o_bd );
		ap = &(*ap)->a_next;
	}

	if ( ( opattrs || op->ors_attrs ) && op->o_bd && op->o_bd->be_operational != NULL ) {
		( void )op->o_bd->be_operational( op, rs, opattrs, ap );
	}

	return a;
}

