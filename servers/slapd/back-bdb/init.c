/* init.c - initialize bdb backend */
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
#include <ac/string.h>
#include <ac/unistd.h>
#include <ac/stdlib.h>

#include "back-bdb.h"
#include <lutil.h>
#include <ldap_rq.h>

static const struct bdbi_database {
	char *file;
	char *name;
	int type;
	int flags;
} bdbi_databases[] = {
	{ "id2entry" BDB_SUFFIX, "id2entry", DB_BTREE, 0 },
	{ "dn2id" BDB_SUFFIX, "dn2id", DB_BTREE, 0 },
	{ NULL, NULL, 0, 0 }
};

typedef void * db_malloc(size_t);
typedef void * db_realloc(void *, size_t);

static int
bdb_db_init( BackendDB *be )
{
	struct bdb_info	*bdb;

	Debug( LDAP_DEBUG_ANY,
		LDAP_XSTRING(bdb_db_init) ": Initializing "
		BDB_UCTYPE " database\n", 0, 0, 0 );

	/* allocate backend-database-specific stuff */
	bdb = (struct bdb_info *) ch_calloc( 1, sizeof(struct bdb_info) );

	/* DBEnv parameters */
	bdb->bi_dbenv_home = ch_strdup( SLAPD_DEFAULT_DB_DIR );
	bdb->bi_dbenv_xflags = 0;
	bdb->bi_dbenv_mode = SLAPD_DEFAULT_DB_MODE;

	bdb->bi_cache.c_maxsize = DEFAULT_CACHE_SIZE;

	bdb->bi_lock_detect = DB_LOCK_DEFAULT;
	bdb->bi_search_stack_depth = DEFAULT_SEARCH_STACK_DEPTH;
	bdb->bi_search_stack = NULL;

	ldap_pvt_thread_mutex_init( &bdb->bi_database_mutex );
	ldap_pvt_thread_mutex_init( &bdb->bi_lastid_mutex );
	ldap_pvt_thread_mutex_init( &bdb->bi_cache.lru_mutex );
	ldap_pvt_thread_mutex_init( &bdb->bi_cache.c_dntree.bei_kids_mutex );
	ldap_pvt_thread_rdwr_init ( &bdb->bi_cache.c_rwlock );

	be->be_private = bdb;
	be->be_cf_table = be->bd_info->bi_cf_table;

	return 0;
}

static void *
bdb_checkpoint( void *ctx, void *arg )
{
	struct re_s *rtask = arg;
	struct bdb_info *bdb = rtask->arg;
	
	TXN_CHECKPOINT( bdb->bi_dbenv, bdb->bi_txn_cp_kbyte,
		bdb->bi_txn_cp_min, 0 );
	return NULL;
}

static int
bdb_db_open( BackendDB *be )
{
	int rc, i;
	struct bdb_info *bdb = (struct bdb_info *) be->be_private;
	u_int32_t flags;
#ifdef HAVE_EBCDIC
	char path[MAXPATHLEN];
#endif

	Debug( LDAP_DEBUG_ARGS,
		"bdb_db_open: %s\n",
		be->be_suffix[0].bv_val, 0, 0 );

#ifndef BDB_MULTIPLE_SUFFIXES
	if ( be->be_suffix[1].bv_val ) {
	Debug( LDAP_DEBUG_ANY,
		"bdb_db_open: only one suffix allowed\n", 0, 0, 0 );
		return -1;
	}
#endif
	/* we should check existance of dbenv_home and db_directory */

	rc = db_env_create( &bdb->bi_dbenv, 0 );
	if( rc != 0 ) {
		Debug( LDAP_DEBUG_ANY,
			"bdb_db_open: db_env_create failed: %s (%d)\n",
			db_strerror(rc), rc, 0 );
		return rc;
	}

	flags = DB_INIT_MPOOL | DB_THREAD | DB_CREATE;

	if ( !( slapMode & SLAP_TOOL_QUICK ))
		flags |= DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_TXN;
	
#if 0
	/* Never do automatic recovery, must perform it manually.
	 * Otherwise restarting with gentlehup will corrupt the
	 * database.
	 */
	if( !(slapMode & SLAP_TOOL_MODE) ) flags |= DB_RECOVER;
#endif

	/* If a key was set, use shared memory for the BDB environment */
	if ( bdb->bi_shm_key ) {
		bdb->bi_dbenv->set_shm_key( bdb->bi_dbenv, bdb->bi_shm_key );
		flags |= DB_SYSTEM_MEM;
	}

	bdb->bi_dbenv->set_errpfx( bdb->bi_dbenv, be->be_suffix[0].bv_val );
	bdb->bi_dbenv->set_errcall( bdb->bi_dbenv, bdb_errcall );
	bdb->bi_dbenv->set_lk_detect( bdb->bi_dbenv, bdb->bi_lock_detect );

	/* One long-lived TXN per thread, two TXNs per write op */
	bdb->bi_dbenv->set_tx_max( bdb->bi_dbenv, connection_pool_max * 3 );

#ifdef SLAP_ZONE_ALLOC
	if ( bdb->bi_cache.c_maxsize ) {
		bdb->bi_cache.c_zctx = slap_zn_mem_create(
								SLAP_ZONE_INITSIZE,
								SLAP_ZONE_MAXSIZE,
								SLAP_ZONE_DELTA,
								SLAP_ZONE_SIZE);
	}
#endif

	if ( bdb->bi_idl_cache_max_size ) {
		bdb->bi_idl_tree = NULL;
		ldap_pvt_thread_rdwr_init( &bdb->bi_idl_tree_rwlock );
		ldap_pvt_thread_mutex_init( &bdb->bi_idl_tree_lrulock );
		bdb->bi_idl_cache_size = 0;
	}

#ifdef BDB_SUBDIRS
	{
		char dir[MAXPATHLEN], *ptr;
		
		if (bdb->bi_dbenv_home[0] == '.') {
			/* If home is a relative path, relative subdirs
			 * are just concat'd by BDB. We don't want the
			 * path to be concat'd twice, e.g.
			 * ./test-db/./test-db/tmp
			 */
			ptr = dir;
		} else {
			ptr = lutil_strcopy( dir, bdb->bi_dbenv_home );
			*ptr++ = LDAP_DIRSEP[0];
#ifdef HAVE_EBCDIC
			__atoe( dir );
#endif
		}

		strcpy( ptr, BDB_TMP_SUBDIR );
#ifdef HAVE_EBCDIC
		__atoe( ptr );
#endif
		rc = bdb->bi_dbenv->set_tmp_dir( bdb->bi_dbenv, dir );
		if( rc != 0 ) {
			Debug( LDAP_DEBUG_ANY,
				"bdb_db_open: set_tmp_dir(%s) failed: %s (%d)\n",
				dir, db_strerror(rc), rc );
			return rc;
		}

		strcpy( ptr, BDB_LG_SUBDIR );
#ifdef HAVE_EBCDIC
		__atoe( ptr );
#endif
		rc = bdb->bi_dbenv->set_lg_dir( bdb->bi_dbenv, dir );
		if( rc != 0 ) {
			Debug( LDAP_DEBUG_ANY,
				"bdb_db_open: set_lg_dir(%s) failed: %s (%d)\n",
				dir, db_strerror(rc), rc );
			return rc;
		}

		strcpy( ptr, BDB_DATA_SUBDIR );
#ifdef HAVE_EBCDIC
		__atoe( ptr );
#endif
		rc = bdb->bi_dbenv->set_data_dir( bdb->bi_dbenv, dir );
		if( rc != 0 ) {
			Debug( LDAP_DEBUG_ANY,
				"bdb_db_open: set_data_dir(%s) failed: %s (%d)\n",
				dir, db_strerror(rc), rc );
			return rc;
		}
	}
#endif

	if( bdb->bi_dbenv_xflags != 0 ) {
		rc = bdb->bi_dbenv->set_flags( bdb->bi_dbenv,
			bdb->bi_dbenv_xflags, 1);
		if( rc != 0 ) {
			Debug( LDAP_DEBUG_ANY,
				"bdb_db_open: dbenv_set_flags failed: %s (%d)\n",
				db_strerror(rc), rc, 0 );
			return rc;
		}
	}

	Debug( LDAP_DEBUG_TRACE,
		"bdb_db_open: dbenv_open(%s)\n",
		bdb->bi_dbenv_home, 0, 0);

#ifdef HAVE_EBCDIC
	strcpy( path, bdb->bi_dbenv_home );
	__atoe( path );
	rc = bdb->bi_dbenv->open( bdb->bi_dbenv,
		path,
		flags,
		bdb->bi_dbenv_mode );
#else
	rc = bdb->bi_dbenv->open( bdb->bi_dbenv,
		bdb->bi_dbenv_home,
		flags,
		bdb->bi_dbenv_mode );
#endif
	if( rc != 0 ) {
		Debug( LDAP_DEBUG_ANY,
			"bdb_db_open: dbenv_open failed: %s (%d)\n",
			db_strerror(rc), rc, 0 );
		return rc;
	}

	flags = DB_THREAD | bdb->bi_db_opflags;

#ifdef DB_AUTO_COMMIT
	if ( !( slapMode & SLAP_TOOL_QUICK ))
		flags |= DB_AUTO_COMMIT;
#endif

	bdb->bi_databases = (struct bdb_db_info **) ch_malloc(
		BDB_INDICES * sizeof(struct bdb_db_info *) );

	/* open (and create) main database */
	for( i = 0; bdbi_databases[i].name; i++ ) {
		struct bdb_db_info *db;

		db = (struct bdb_db_info *) ch_calloc(1, sizeof(struct bdb_db_info));

		rc = db_create( &db->bdi_db, bdb->bi_dbenv, 0 );
		if( rc != 0 ) {
			Debug( LDAP_DEBUG_ANY,
				"bdb_db_open: db_create(%s) failed: %s (%d)\n",
				bdb->bi_dbenv_home, db_strerror(rc), rc );
			return rc;
		}

		if( i == BDB_ID2ENTRY ) {
			rc = db->bdi_db->set_pagesize( db->bdi_db,
				BDB_ID2ENTRY_PAGESIZE );
			if ( slapMode & SLAP_TOOL_READMAIN ) {
				flags |= DB_RDONLY;
			} else {
				flags |= DB_CREATE;
			}
		} else {
			rc = db->bdi_db->set_flags( db->bdi_db, 
				DB_DUP | DB_DUPSORT );
#ifndef BDB_HIER
			if ( slapMode & SLAP_TOOL_READONLY ) {
				flags |= DB_RDONLY;
			} else {
				flags |= DB_CREATE;
			}
#else
			if ( slapMode & (SLAP_TOOL_READONLY|SLAP_TOOL_READMAIN) ) {
				flags |= DB_RDONLY;
			} else {
				flags |= DB_CREATE;
			}
#endif
			rc = db->bdi_db->set_pagesize( db->bdi_db,
				BDB_PAGESIZE );
		}

#ifdef HAVE_EBCDIC
		strcpy( path, bdbi_databases[i].file );
		__atoe( path );
		rc = DB_OPEN( db->bdi_db,
			path,
		/*	bdbi_databases[i].name, */ NULL,
			bdbi_databases[i].type,
			bdbi_databases[i].flags | flags,
			bdb->bi_dbenv_mode );
#else
		rc = DB_OPEN( db->bdi_db,
			bdbi_databases[i].file,
		/*	bdbi_databases[i].name, */ NULL,
			bdbi_databases[i].type,
			bdbi_databases[i].flags | flags,
			bdb->bi_dbenv_mode );
#endif

		if ( rc != 0 ) {
			char	buf[SLAP_TEXT_BUFLEN];

			snprintf( buf, sizeof(buf), "%s/%s", 
				bdb->bi_dbenv_home, bdbi_databases[i].file );
			Debug( LDAP_DEBUG_ANY,
				"bdb_db_open: db_open(%s) failed: %s (%d)\n",
				buf, db_strerror(rc), rc );
			return rc;
		}

		flags &= ~(DB_CREATE | DB_RDONLY);
		db->bdi_name = bdbi_databases[i].name;
		bdb->bi_databases[i] = db;
	}

	bdb->bi_databases[i] = NULL;
	bdb->bi_ndatabases = i;

	/* get nextid */
	rc = bdb_last_id( be, NULL );
	if( rc != 0 ) {
		Debug( LDAP_DEBUG_ANY,
			"bdb_db_open: last_id(%s) failed: %s (%d)\n",
			bdb->bi_dbenv_home, db_strerror(rc), rc );
		return rc;
	}

	if ( !( slapMode & SLAP_TOOL_QUICK )) {
		XLOCK_ID(bdb->bi_dbenv, &bdb->bi_cache.c_locker);
	}

	/* If we're in server mode and time-based checkpointing is enabled,
	 * submit a task to perform periodic checkpoints.
	 */
	if ( slapMode & SLAP_SERVER_MODE && bdb->bi_txn_cp &&
		bdb->bi_txn_cp_min )  {
		ldap_pvt_thread_mutex_lock( &slapd_rq.rq_mutex );
		ldap_pvt_runqueue_insert( &slapd_rq, bdb->bi_txn_cp_min*60,
			bdb_checkpoint, bdb );
		ldap_pvt_thread_mutex_unlock( &slapd_rq.rq_mutex );
	}

	return 0;
}

static int
bdb_db_close( BackendDB *be )
{
	int rc;
	struct bdb_info *bdb = (struct bdb_info *) be->be_private;
	struct bdb_db_info *db;
	bdb_idl_cache_entry_t *entry, *next_entry;

	while( bdb->bi_ndatabases-- ) {
		db = bdb->bi_databases[bdb->bi_ndatabases];
		rc = db->bdi_db->close( db->bdi_db, 0 );
		/* Lower numbered names are not strdup'd */
		if( bdb->bi_ndatabases >= BDB_NDB )
			free( db->bdi_name );
		free( db );
	}
	free( bdb->bi_databases );
	bdb_attr_index_destroy( bdb->bi_attrs );

	bdb_cache_release_all (&bdb->bi_cache);

	if ( bdb->bi_idl_cache_max_size ) {
		ldap_pvt_thread_rdwr_wlock ( &bdb->bi_idl_tree_rwlock );
		avl_free( bdb->bi_idl_tree, NULL );
		entry = bdb->bi_idl_lru_head;
		while ( entry != NULL ) {
			next_entry = entry->idl_lru_next;
			if ( entry->idl )
				free( entry->idl );
			free( entry->kstr.bv_val );
			free( entry );
			entry = next_entry;
		}
		ldap_pvt_thread_rdwr_wunlock ( &bdb->bi_idl_tree_rwlock );
	}

	if ( !( slapMode & SLAP_TOOL_QUICK )) {
		XLOCK_ID_FREE(bdb->bi_dbenv, bdb->bi_cache.c_locker);
	}

	return 0;
}

static int
bdb_db_destroy( BackendDB *be )
{
	int rc;
	struct bdb_info *bdb = (struct bdb_info *) be->be_private;

	/* close db environment */
	if( bdb->bi_dbenv ) {
		/* force a checkpoint */
		if ( !( slapMode & SLAP_TOOL_QUICK )) {
			rc = TXN_CHECKPOINT( bdb->bi_dbenv, 0, 0, DB_FORCE );
			if( rc != 0 ) {
				Debug( LDAP_DEBUG_ANY,
					"bdb_db_destroy: txn_checkpoint failed: %s (%d)\n",
					db_strerror(rc), rc, 0 );
			}
		}

		rc = bdb->bi_dbenv->close( bdb->bi_dbenv, 0 );
		bdb->bi_dbenv = NULL;
		if( rc != 0 ) {
			Debug( LDAP_DEBUG_ANY,
				"bdb_db_destroy: close failed: %s (%d)\n",
				db_strerror(rc), rc, 0 );
			return rc;
		}
	}

	if( bdb->bi_dbenv_home ) ch_free( bdb->bi_dbenv_home );

	ldap_pvt_thread_rdwr_destroy ( &bdb->bi_cache.c_rwlock );
	ldap_pvt_thread_mutex_destroy( &bdb->bi_cache.lru_mutex );
	ldap_pvt_thread_mutex_destroy( &bdb->bi_cache.c_dntree.bei_kids_mutex );
	ldap_pvt_thread_mutex_destroy( &bdb->bi_lastid_mutex );
	ldap_pvt_thread_mutex_destroy( &bdb->bi_database_mutex );
	if ( bdb->bi_idl_cache_max_size ) {
		ldap_pvt_thread_rdwr_destroy( &bdb->bi_idl_tree_rwlock );
		ldap_pvt_thread_mutex_destroy( &bdb->bi_idl_tree_lrulock );
	}

	ch_free( bdb );
	be->be_private = NULL;

	return 0;
}

int
bdb_back_initialize(
	BackendInfo	*bi )
{
	int rc;

	static char *controls[] = {
		LDAP_CONTROL_ASSERT,
		LDAP_CONTROL_MANAGEDSAIT,
		LDAP_CONTROL_NOOP,
		LDAP_CONTROL_PAGEDRESULTS,
#ifdef LDAP_CONTROL_SUBENTRIES
		LDAP_CONTROL_SUBENTRIES,
#endif
#ifdef LDAP_CONTROL_X_PERMISSIVE_MODIFY
		LDAP_CONTROL_X_PERMISSIVE_MODIFY,
#endif
		NULL
	};

	/* initialize the underlying database system */
	Debug( LDAP_DEBUG_TRACE,
		LDAP_XSTRING(bdb_back_initialize) ": initialize " 
		BDB_UCTYPE " backend\n", 0, 0, 0 );

	bi->bi_flags |=
		SLAP_BFLAG_INCREMENT |
#ifdef BDB_SUBENTRIES
		SLAP_BFLAG_SUBENTRIES |
#endif
		SLAP_BFLAG_ALIASES |
		SLAP_BFLAG_REFERRALS;

	bi->bi_controls = controls;

	{	/* version check */
		int major, minor, patch, ver;
		char *version = db_version( &major, &minor, &patch );
#ifdef HAVE_EBCDIC
		char v2[1024];

		/* All our stdio does an ASCII to EBCDIC conversion on
		 * the output. Strings from the BDB library are already
		 * in EBCDIC; we have to go back and forth...
		 */
		strcpy( v2, version );
		__etoa( v2 );
		version = v2;
#endif

		ver = (major << 24) | (minor << 16) | patch;
		if( ver < DB_VERSION_FULL )
		{
			Debug( LDAP_DEBUG_ANY,
				LDAP_XSTRING(bdb_back_initialize) ": "
				"BDB library version mismatch:"
				" expected " DB_VERSION_STRING ","
				" got %s\n", version, 0, 0 );
		}

		Debug( LDAP_DEBUG_ANY, LDAP_XSTRING(bdb_back_initialize)
			": %s\n", version, 0, 0 );
	}

	db_env_set_func_free( ber_memfree );
	db_env_set_func_malloc( (db_malloc *)ber_memalloc );
	db_env_set_func_realloc( (db_realloc *)ber_memrealloc );
#ifndef NO_THREAD
	/* This is a no-op on a NO_THREAD build. Leave the default
	 * alone so that BDB will sleep on interprocess conflicts.
	 */
	db_env_set_func_yield( ldap_pvt_thread_yield );
#endif

	bi->bi_open = 0;
	bi->bi_close = 0;
	bi->bi_config = 0;
	bi->bi_destroy = 0;

	bi->bi_db_init = bdb_db_init;
	bi->bi_db_config = 0;
	bi->bi_db_open = bdb_db_open;
	bi->bi_db_close = bdb_db_close;
	bi->bi_db_destroy = bdb_db_destroy;

	bi->bi_op_add = bdb_add;
	bi->bi_op_bind = bdb_bind;
	bi->bi_op_compare = bdb_compare;
	bi->bi_op_delete = bdb_delete;
	bi->bi_op_modify = bdb_modify;
	bi->bi_op_modrdn = bdb_modrdn;
	bi->bi_op_search = bdb_search;

	bi->bi_op_unbind = 0;

	bi->bi_extended = bdb_extended;

	bi->bi_chk_referrals = bdb_referrals;
	bi->bi_operational = bdb_operational;
	bi->bi_has_subordinates = bdb_hasSubordinates;
	bi->bi_entry_release_rw = bdb_entry_release;
	bi->bi_entry_get_rw = bdb_entry_get;

	/*
	 * hooks for slap tools
	 */
	bi->bi_tool_entry_open = bdb_tool_entry_open;
	bi->bi_tool_entry_close = bdb_tool_entry_close;
	bi->bi_tool_entry_first = bdb_tool_entry_next;
	bi->bi_tool_entry_next = bdb_tool_entry_next;
	bi->bi_tool_entry_get = bdb_tool_entry_get;
	bi->bi_tool_entry_put = bdb_tool_entry_put;
	bi->bi_tool_entry_reindex = bdb_tool_entry_reindex;
	bi->bi_tool_sync = 0;
	bi->bi_tool_dn2id_get = bdb_tool_dn2id_get;
	bi->bi_tool_id2entry_get = bdb_tool_id2entry_get;
	bi->bi_tool_entry_modify = bdb_tool_entry_modify;

	bi->bi_connection_init = 0;
	bi->bi_connection_destroy = 0;

	rc = bdb_back_init_cf(bi);

	return rc;
}

#if	(SLAPD_BDB == SLAPD_MOD_DYNAMIC && !defined(BDB_HIER)) || \
	(SLAPD_HDB == SLAPD_MOD_DYNAMIC && defined(BDB_HIER))

/* conditionally define the init_module() function */
#ifdef BDB_HIER
SLAP_BACKEND_INIT_MODULE( hdb )
#else /* !BDB_HIER */
SLAP_BACKEND_INIT_MODULE( bdb )
#endif /* !BDB_HIER */

#endif /* SLAPD_[BH]DB == SLAPD_MOD_DYNAMIC */

