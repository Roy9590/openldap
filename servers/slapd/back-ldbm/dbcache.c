/* ldbmcache.c - maintain a cache of open ldbm files */
/*
 * Copyright 1998-1999 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */

#include "portable.h"

#include <stdio.h>

#include <ac/errno.h>
#include <ac/socket.h>
#include <ac/string.h>
#include <ac/time.h>

#include <sys/stat.h>

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#include "ldap_defaults.h"
#include "slap.h"
#include "back-ldbm.h"

DBCache *
ldbm_cache_open(
    Backend	*be,
    char	*name,
    char	*suffix,
    int		flags
)
{
	struct ldbminfo	*li = (struct ldbminfo *) be->be_private;
	int		i, lru;
	time_t		oldtime, curtime;
	char		buf[MAXPATHLEN];
#ifdef HAVE_ST_BLKSIZE
	struct stat	st;
#endif

	sprintf( buf, "%s" LDAP_DIRSEP "%s%s",
		li->li_directory, name, suffix );

	Debug( LDAP_DEBUG_TRACE, "=> ldbm_cache_open( \"%s\", %d, %o )\n", buf,
	    flags, li->li_mode );

	lru = 0;
	curtime = slap_get_time();
	oldtime = curtime;

	ldap_pvt_thread_mutex_lock( &li->li_dbcache_mutex );
	for ( i = 0; i < MAXDBCACHE && li->li_dbcache[i].dbc_name != NULL;
	    i++ ) {
		/* already open - return it */
		if ( strcmp( li->li_dbcache[i].dbc_name, buf ) == 0 ) {
			li->li_dbcache[i].dbc_refcnt++;
			Debug( LDAP_DEBUG_TRACE,
			    "<= ldbm_cache_open (cache %d)\n", i, 0, 0 );
			ldap_pvt_thread_mutex_unlock( &li->li_dbcache_mutex );
			return( &li->li_dbcache[i] );
		}

		/* keep track of lru db */
		if ( li->li_dbcache[i].dbc_lastref < oldtime &&
		    li->li_dbcache[i].dbc_refcnt == 0 ) {
			lru = i;
			oldtime = li->li_dbcache[i].dbc_lastref;
		}
	}

	/* no empty slots, not already open - close lru and use that slot */
	if ( i == MAXDBCACHE ) {
		i = lru;
		if ( li->li_dbcache[i].dbc_refcnt != 0 ) {
			Debug( LDAP_DEBUG_ANY,
			    "ldbm_cache_open no unused db to close - waiting\n",
			    0, 0, 0 );
			lru = -1;
			while ( lru == -1 ) {
				ldap_pvt_thread_cond_wait( &li->li_dbcache_cv,
				    &li->li_dbcache_mutex );
				for ( i = 0; i < MAXDBCACHE; i++ ) {
					if ( li->li_dbcache[i].dbc_refcnt
					    == 0 ) {
						lru = i;
						break;
					}
				}
			}
			i = lru;
		}
		ldbm_close( li->li_dbcache[i].dbc_db );
		free( li->li_dbcache[i].dbc_name );
		li->li_dbcache[i].dbc_name = NULL;
	}

	if ( (li->li_dbcache[i].dbc_db = ldbm_open( buf, flags, li->li_mode,
	    li->li_dbcachesize )) == NULL ) {
		int err = errno;
		Debug( LDAP_DEBUG_TRACE,
		    "<= ldbm_cache_open NULL \"%s\" errno=%d reason=\"%s\")\n",
		    buf, err, err > -1 && err < sys_nerr ?
		    sys_errlist[err] : "unknown" );
		ldap_pvt_thread_mutex_unlock( &li->li_dbcache_mutex );
		return( NULL );
	}
	li->li_dbcache[i].dbc_name = ch_strdup( buf );
	li->li_dbcache[i].dbc_refcnt = 1;
	li->li_dbcache[i].dbc_lastref = curtime;
#ifdef HAVE_ST_BLKSIZE
	if ( stat( buf, &st ) == 0 ) {
		li->li_dbcache[i].dbc_blksize = st.st_blksize;
	} else
#endif
	{
		li->li_dbcache[i].dbc_blksize = DEFAULT_BLOCKSIZE;
	}
	li->li_dbcache[i].dbc_maxids = (li->li_dbcache[i].dbc_blksize /
	    sizeof(ID)) - ID_BLOCK_IDS_OFFSET;
	li->li_dbcache[i].dbc_maxindirect = (SLAPD_LDBM_MIN_MAXIDS /
	    li->li_dbcache[i].dbc_maxids) + 1;

	Debug( LDAP_DEBUG_ARGS,
	    "ldbm_cache_open (blksize %ld) (maxids %d) (maxindirect %d)\n",
	    li->li_dbcache[i].dbc_blksize, li->li_dbcache[i].dbc_maxids,
	    li->li_dbcache[i].dbc_maxindirect );
	Debug( LDAP_DEBUG_TRACE, "<= ldbm_cache_open (opened %d)\n", i, 0, 0 );
	ldap_pvt_thread_mutex_unlock( &li->li_dbcache_mutex );
	return( &li->li_dbcache[i] );
}

void
ldbm_cache_close( Backend *be, DBCache *db )
{
	struct ldbminfo	*li = (struct ldbminfo *) be->be_private;

	ldap_pvt_thread_mutex_lock( &li->li_dbcache_mutex );
	if ( --db->dbc_refcnt == 0 ) {
		ldap_pvt_thread_cond_signal( &li->li_dbcache_cv );
	}
	ldap_pvt_thread_mutex_unlock( &li->li_dbcache_mutex );
}

void
ldbm_cache_really_close( Backend *be, DBCache *db )
{
	struct ldbminfo	*li = (struct ldbminfo *) be->be_private;

	ldap_pvt_thread_mutex_lock( &li->li_dbcache_mutex );
	if ( --db->dbc_refcnt == 0 ) {
		ldap_pvt_thread_cond_signal( &li->li_dbcache_cv );
		ldbm_close( db->dbc_db );
		free( db->dbc_name );
		db->dbc_name = NULL;
	}
	ldap_pvt_thread_mutex_unlock( &li->li_dbcache_mutex );
}

void
ldbm_cache_flush_all( Backend *be )
{
	struct ldbminfo	*li = (struct ldbminfo *) be->be_private;
	int		i;

	ldap_pvt_thread_mutex_lock( &li->li_dbcache_mutex );
	for ( i = 0; i < MAXDBCACHE; i++ ) {
		if ( li->li_dbcache[i].dbc_name != NULL ) {
			Debug( LDAP_DEBUG_TRACE, "ldbm flushing db (%s)\n",
			    li->li_dbcache[i].dbc_name, 0, 0 );
			ldbm_sync( li->li_dbcache[i].dbc_db );
			if ( li->li_dbcache[i].dbc_refcnt != 0 ) {
				Debug( LDAP_DEBUG_TRACE,
				       "refcnt = %d, couldn't close db (%s)\n",
				       li->li_dbcache[i].dbc_refcnt,
				       li->li_dbcache[i].dbc_name, 0 );
			} else {
				Debug( LDAP_DEBUG_TRACE,
				       "ldbm closing db (%s)\n",
				       li->li_dbcache[i].dbc_name, 0, 0 );
				ldap_pvt_thread_cond_signal( &li->li_dbcache_cv );
				ldbm_close( li->li_dbcache[i].dbc_db );
				free( li->li_dbcache[i].dbc_name );
				li->li_dbcache[i].dbc_name = NULL;
			}
		}
	}
	ldap_pvt_thread_mutex_unlock( &li->li_dbcache_mutex );
}

Datum
ldbm_cache_fetch(
    DBCache	*db,
    Datum		key
)
{
	Datum	data;

	return ldbm_fetch( db->dbc_db, key );

	return( data );
}

int
ldbm_cache_store(
    DBCache	*db,
    Datum		key,
    Datum		data,
    int			flags
)
{
	int	rc;

#ifdef LDBM_DEBUG
	Statslog( LDAP_DEBUG_STATS,
		"=> ldbm_cache_store(): key.dptr=%s, key.dsize=%d\n",
		key.dptr, key.dsize, 0, 0, 0 );

	Statslog( LDAP_DEBUG_STATS,
		"=> ldbm_cache_store(): key.dptr=0x%08x, data.dptr=0x%0 8x\n",
		key.dptr, data.dptr, 0, 0, 0 );

	Statslog( LDAP_DEBUG_STATS,
		"=> ldbm_cache_store(): data.dptr=%s, data.dsize=%d\n",
		data.dptr, data.dsize, 0, 0, 0 );

	Statslog( LDAP_DEBUG_STATS,
		"=> ldbm_cache_store(): flags=0x%08x\n",
		flags, 0, 0, 0, 0 );
#endif /* LDBM_DEBUG */

	rc = ldbm_store( db->dbc_db, key, data, flags );

	return( rc );
}

int
ldbm_cache_delete(
    DBCache	*db,
    Datum		key
)
{
	int	rc;

	rc = ldbm_delete( db->dbc_db, key );

	return( rc );
}
