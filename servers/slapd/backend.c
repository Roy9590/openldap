/* backend.c - routines for dealing with back-end databases */


#include "portable.h"

#include <stdio.h>

#include <ac/string.h>
#include <ac/socket.h>

#include <sys/stat.h>

#include "slap.h"
#include "lutil.h"

#ifdef SLAPD_LDBM
#include "back-ldbm/external.h"
#endif
#ifdef SLAPD_BDB2
#include "back-bdb2/external.h"
#endif
#ifdef SLAPD_PASSWD
#include "back-passwd/external.h"
#endif
#ifdef SLAPD_PERL
#include "back-perl/external.h"
#endif
#ifdef SLAPD_SHELL
#include "back-shell/external.h"
#endif
#ifdef SLAPD_TCL
#include "back-tcl/external.h"
#endif

static BackendInfo binfo[] = {
#ifdef SLAPD_LDBM
	{"ldbm",	ldbm_back_initialize},
#endif
#ifdef SLAPD_BDB2
	{"bdb2",	bdb2_back_initialize},
#endif
#ifdef SLAPD_PASSWD
	{"passwd",	passwd_back_initialize},
#endif
#ifdef SLAPD_PERL
	{"perl",	perl_back_initialize},
#endif
#ifdef SLAPD_SHELL
	{"shell",	shell_back_initialize},
#endif
#ifdef SLAPD_TCL
	{"tcl",		tcl_back_initialize},
#endif
	{NULL}
};

int			nBackendInfo = 0;
BackendInfo	*backendInfo = NULL;

int			nBackendDB = 0; 
BackendDB	*backendDB = NULL;

int backend_init(void)
{
	int rc = -1;

	if((nBackendInfo != 0) || (backendInfo != NULL)) {
		/* already initialized */
		Debug( LDAP_DEBUG_ANY,
			"backend_init: already initialized.\n", 0, 0, 0 );
		return -1;
	}

	for( ;
		binfo[nBackendInfo].bi_type !=  NULL;
		nBackendInfo++ )
	{
		rc = binfo[nBackendInfo].bi_init(
			&binfo[nBackendInfo] );

		if(rc != 0) {
			Debug( LDAP_DEBUG_ANY,
				"backend_init: initialized for type \"%s\"\n",
					binfo[nBackendInfo].bi_type, 0, 0 );

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

	Debug( LDAP_DEBUG_ANY,
		"backend_init: failed\n",
		0, 0, 0 );

	return rc;
}

int backend_startup(int n)
{
	int i;
	int rc = 0;

	if( ! ( nBackendDB > 0 ) ) {
		/* no databases */
		Debug( LDAP_DEBUG_ANY,
			"backend_startup: %d databases to startup.\n",
			nBackendDB, 0, 0 );
		return 1;
	}

	if(n >= 0) {
		/* startup a specific backend database */
		Debug( LDAP_DEBUG_TRACE,
			"backend_startup: starting database %d\n",
			n, 0, 0 );

		/* make sure, n does not exceed the number of backend databases */
		if ( n >= nbackends ) {

			Debug( LDAP_DEBUG_ANY,
				"backend_startup: database number %d exceeding maximum (%d)\n",
				n, nbackends, 0 );
			return 1;
		}

		if ( backendDB[n].bd_info->bi_open ) {
			rc = backendDB[n].bd_info->bi_open(
				backendDB[n].bd_info );
		}

		if(rc != 0) {
			Debug( LDAP_DEBUG_ANY,
				"backend_startup: bi_open failed!\n",
				0, 0, 0 );
			return rc;
		}

		if ( backendDB[n].bd_info->bi_db_open ) {
			rc = backendDB[n].bd_info->bi_db_open(
				&backendDB[n] );
		}

		if(rc != 0) {
			Debug( LDAP_DEBUG_ANY,
				"backend_startup: bi_db_open failed!\n",
				0, 0, 0 );
			return rc;
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
		}

		if(rc != 0) {
			Debug( LDAP_DEBUG_ANY,
				"backend_startup: bi_open %d failed!\n",
				i, 0, 0 );
			return rc;
		}
	}

	/* open each backend database */
	for( i = 0; i < nBackendDB; i++ ) {
		if ( backendDB[i].bd_info->bi_db_open ) {
			rc = backendDB[i].bd_info->bi_db_open(
				&backendDB[i] );
		}

		if(rc != 0) {
			Debug( LDAP_DEBUG_ANY,
				"backend_startup: bi_db_open %d failed!\n",
				i, 0, 0 );
			return rc;
		}
	}

	return rc;
}

int backend_shutdown(int n)
{
	int i;
	int rc = 0;

	if(n >= 0) {
		/* shutdown a specific backend database */

		/* make sure, n does not exceed the number of backend databases */
		if ( n >= nbackends ) {

			Debug( LDAP_DEBUG_ANY,
				"backend_startup: database number %d exceeding maximum (%d)\n",
				n, nbackends, 0 );
			return 1;
		}

		if ( backendDB[n].bd_info->bi_nDB == 0 ) {
			/* no database of this type, we never opened it */
			return 0;
		}

		if ( backendDB[n].bd_info->bi_db_close ) {
			backendDB[n].bd_info->bi_db_close(
				&backendDB[n] );
		}

		if( backendDB[n].bd_info->bi_close ) {
			backendDB[n].bd_info->bi_close(
				backendDB[n].bd_info );
		}

		return 0;
	}

	/* close each backend database */
	for( i = 0; i < nBackendDB; i++ ) {
		BackendInfo  *bi;

		if ( backendDB[i].bd_info->bi_db_close ) {
			backendDB[i].bd_info->bi_db_close(
				&backendDB[i] );
		}

		if(rc != 0) {
			Debug( LDAP_DEBUG_ANY,
				"backend_close: bi_close %s failed!\n",
				bi->bi_type, 0, 0 );
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

	/* destroy each backend database */
	for( i = 0; i < nBackendDB; i++ ) {
		if ( backendDB[i].bd_info->bi_db_destroy ) {
			backendDB[i].bd_info->bi_db_destroy(
				&backendDB[i] );
		}
	}

	/* destroy each backend type */
	for( i = 0; i < nBackendInfo; i++ ) {
		if( backendInfo[i].bi_destroy ) {
			backendInfo[i].bi_destroy(
				&backendInfo[i] );
		}
	}

	return 0;
}

BackendInfo* backend_info(char *type)
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
    char	*type
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
	be->be_sizelimit = defsize;
	be->be_timelimit = deftime;

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
select_backend( char * dn )
{
	int	i, j, len, dnlen;

	dnlen = strlen( dn );
	for ( i = 0; i < nbackends; i++ ) {
		for ( j = 0; backends[i].be_suffix != NULL &&
		    backends[i].be_suffix[j] != NULL; j++ )
		{
			len = strlen( backends[i].be_suffix[j] );

			if ( len > dnlen ) {
				continue;
			}

			if ( strcmp( backends[i].be_suffix[j],
			    dn + (dnlen - len) ) == 0 ) {
				return( &backends[i] );
			}
		}
	}

        /* if no proper suffix could be found then check for aliases */
        for ( i = 0; i < nbackends; i++ ) {
                for ( j = 0; 
		      backends[i].be_suffixAlias != NULL && 
                      backends[i].be_suffixAlias[j] != NULL; 
		      j += 2 )
                {
                        len = strlen( backends[i].be_suffixAlias[j] );

                        if ( len > dnlen ) {
                                continue;
                        }

                        if ( strcmp( backends[i].be_suffixAlias[j],
                            dn + (dnlen - len) ) == 0 ) {
                                return( &backends[i] );
                        }
                }
        }

#ifdef LDAP_ALLOW_NULL_SEARCH_BASE
	/* Add greg@greg.rim.or.jp
	 * It's quick hack for cheap client
	 * Some browser offer a NULL base at ldap_search
	 *
	 * Should only be used as a last resort. -Kdz
	 */
	if(dnlen == 0) {
		Debug( LDAP_DEBUG_TRACE,
			"select_backend: use default backend\n", 0, 0, 0 );
		return( &backends[0] );
	}
#endif /* LDAP_ALLOW_NULL_SEARCH_BASE */

	return( NULL );
}

int
be_issuffix(
    Backend	*be,
    char	*suffix
)
{
	int	i;

	for ( i = 0; be->be_suffix != NULL && be->be_suffix[i] != NULL; i++ ) {
		if ( strcmp( be->be_suffix[i], suffix ) == 0 ) {
			return( 1 );
		}
	}

	return( 0 );
}

int
be_isroot( Backend *be, char *ndn )
{
	int rc;

	if ( ndn == NULL || be->be_root_ndn == NULL ) {
		return( 0 );
	}

	rc = strcmp( be->be_root_ndn, ndn ) ? 0 : 1;

	return(rc);
}

char *
be_root_dn( Backend *be )
{
	if ( be->be_root_dn == NULL ) {
		return( "" );
	}

	return be->be_root_dn;
}

int
be_isroot_pw( Backend *be, char *ndn, struct berval *cred )
{
	int result;

	if ( ! be_isroot( be, ndn ) ) {
		return( 0 );
	}

#ifdef SLAPD_CRYPT
	ldap_pvt_thread_mutex_lock( &crypt_mutex );
#endif

	result = lutil_passwd( cred->bv_val, be->be_root_pw );

#ifdef SLAPD_CRYPT
	ldap_pvt_thread_mutex_unlock( &crypt_mutex );
#endif

	return result == 0;
}

int
be_entry_release_rw( Backend *be, Entry *e, int rw )
{
	int rc;

	if ( be->be_release ) {
		/* free and release entry from backend */
		return be->be_release( be, e, rw );
	} else {
		/* free entry */
		entry_free( e );
		return 0;
	}
}

int
backend_unbind(
	Connection   *conn,
	Operation    *op
)
{
	int	i;

	for ( i = 0; i < nbackends; i++ ) {
		if ( backends[i].be_unbind ) {
			(*backends[i].be_unbind)( &backends[i], conn, op );
		}
	}

	return 0;
}

#ifdef SLAPD_ACLGROUPS
int 
backend_group(
	Backend	*be,
	Entry	*target,
	char	*gr_ndn,
	char	*op_ndn,
	char	*objectclassValue,
	char	*groupattrName
)
{
	if (be->be_group)
		return( be->be_group(be, target, gr_ndn, op_ndn,
			objectclassValue, groupattrName) );
	else
		return(1);
}
#endif
