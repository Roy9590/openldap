/* idl.c - ldap id list handling routines */
/* $OpenLDAP$ */
/*
 * Copyright 1998-2000 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */

#include "portable.h"

#include <stdio.h>
#include <ac/string.h>

#include "back-bdb.h"
#include "idl.h"

#define IDL_MAX(x,y)	( x > y ? x : y )
#define IDL_MIN(x,y)	( x < y ? x : y )

#define IDL_CMP(x,y)	( x < y ? -1 : ( x > y ? 1 : 0 ) )

#undef IDL_DEBUG
#ifdef IDL_DEBUG
void idl_dump( ID *ids )
{
	if( BDB_IDL_IS_RANGE( ids ) ) {
		Debug( LDAP_DEBUG_ANY,
			"IDL: range %ld - %ld\n", (long) ids[0], 0, 0 );

	} else {
		ID i;
		Debug( LDAP_DEBUG_ANY, "IDL: size %ld", (long) ids[0], 0, 0 );

		for( i=1; i<=ids[0]; i++ ) {
			if( i % 16 == 1 ) {
				Debug( LDAP_DEBUG_ANY, "\n", 0, 0, 0 );
			}
			Debug( LDAP_DEBUG_ANY, "  %02lx", ids[i], 0, 0 );
		}

		Debug( LDAP_DEBUG_ANY, "\n", 0, 0, 0 );
	}
}
#endif

unsigned bdb_idl_search( ID *ids, ID id )
{
#undef IDL_BINARY_SEARCH
#ifdef IDL_BINARY_SEARCH
	/*
	 * binary search of id in ids
	 * if found, returns position of id
	 * if not found, returns first postion greater than id
	 */
	unsigned base = 0;
	unsigned cursor = 0;
	int val;
	unsigned n = ids[0];

	while( 0 < n ) {
		int pivot = n >> 1;
		cursor = base + pivot;
		val = IDL_CMP( id, ids[cursor + 1] );

		if( val < 0 ) {
			n = pivot;

		} else if ( val > 0 ) {
			base = cursor + 1;
			n -= pivot + 1;

		} else {
			return cursor + 1;
		}
	}
	
	if( val > 0 ) {
		return cursor + 2;
	} else {
		return cursor + 1;
	}

#else
	/* (reverse) linear search */
	int i;
	for( i=ids[0]; i; i-- ) {
		if( id > ids[i] ) {
			break;
		}
	}
	return i+1;
#endif
}

static int idl_insert( ID *ids, ID id )
{
	unsigned x = bdb_idl_search( ids, id );

#ifdef IDL_DEBUG
	Debug( LDAP_DEBUG_ANY, "insert: %04lx at %d\n", id, x, 0 );
	idl_dump( ids );
#endif

	assert( x > 0 );

	if( x < 1 ) {
		/* internal error */
		return -2;
	}

	if ( x <= ids[0] && ids[x] == id ) {
		/* duplicate */
		return -1;
	}

	if ( ++ids[0] >= BDB_IDL_DB_MAX ) {
		if( id < ids[1] ) {
			ids[1] = id;
			ids[2] = ids[ids[0]-1];
		} else if ( ids[ids[0]-1] < id ) {
			ids[2] = id;
		} else {
			ids[2] = ids[ids[0]-1];
		}
		ids[0] = NOID;
	
	} else {
		/* insert id */
		AC_MEMCPY( &ids[x+1], &ids[x], (ids[0]-x) * sizeof(ID) );
		ids[x] = id;
	}

#ifdef IDL_DEBUG
	idl_dump( ids );
#endif

	return 0;
}

static int idl_delete( ID *ids, ID id )
{
	unsigned x = bdb_idl_search( ids, id );

#ifdef IDL_DEBUG
	Debug( LDAP_DEBUG_ANY, "delete: %04lx at %d\n", id, x, 0 );
	idl_dump( ids );
#endif

	assert( x > 0 );

	if( x <= 0 ) {
		/* internal error */
		return -2;
	}

	if( x > ids[0] || ids[x] != id ) {
		/* not found */
		return -1;

	} else if ( --ids[0] == 0 ) {
		if( x != 1 ) {
			return -3;
		}

	} else {
		AC_MEMCPY( &ids[x], &ids[x+1], (1+ids[0]-x) * sizeof(ID) );
	}

#ifdef IDL_DEBUG
	idl_dump( ids );
#endif

	return 0;
}

int
bdb_idl_insert_key(
	BackendDB	*be,
	DB			*db,
	DB_TXN		*tid,
	DBT			*key,
	ID			id )
{
	int	rc;
	ID ids[BDB_IDL_DB_SIZE];
	DBT data;

	/* for printable keys only */
	Debug( LDAP_DEBUG_ARGS,
		"=> bdb_idl_insert_key: %s %ld\n",
		key->data, (long) id, 0 );

	assert( id != NOID );

	data.data = ids;
	data.ulen = sizeof( ids );
	data.flags = DB_DBT_USERMEM;

	/* fetch the key for read/modify/write */
	rc = db->get( db, tid, key, &data, DB_RMW );

	if( rc == DB_NOTFOUND ) {
		ids[0] = 1;
		ids[1] = id;
		data.size = 2 * sizeof( ID );

	} else if ( rc != 0 ) {
		Debug( LDAP_DEBUG_ANY,
			"=> bdb_idl_insert_key: get failed: %s (%d)\n",
			db_strerror(rc), rc, 0 );
		return rc;

	} else if ( data.size == 0 || data.size % sizeof( ID ) ) {
		/* size not multiple of ID size */
		Debug( LDAP_DEBUG_ANY,
			"=> bdb_idl_insert_key: odd size: expected %ld multiple, got %ld\n",
			(long) sizeof( ID ), (long) data.size, 0 );
		return -1;
	
	} else if ( BDB_IDL_IS_RANGE(ids) ) {
		if( id < ids[1] ) {
			ids[1] = id;
		} else if ( ids[2] > id ) {
			ids[2] = id;
		} else {
			return 0;
		}

	} else if ( data.size != (1 + ids[0]) * sizeof( ID ) ) {
		/* size mismatch */
		Debug( LDAP_DEBUG_ANY,
			"=> bdb_idl_insert_key: get size mismatch: expected %ld, got %ld\n",
			(long) ((1 + ids[0]) * sizeof( ID )), (long) data.size, 0 );
		return -1;

	} else {
		rc = idl_insert( ids, id );

		if( rc != 0 ) {
			Debug( LDAP_DEBUG_ANY,
				"=> bdb_idl_insert_key: idl_insert failed (%d)\n",
				rc, 0, 0 );
			return rc;
		}

		if( BDB_IDL_IS_RANGE( ids ) ) {
			data.size = BDB_IDL_RANGE_SIZE;
		} else {
			data.size = (ids[0]+1) * sizeof( ID );
		}
	}

	/* store the key */
	rc = db->put( db, tid, key, &data, 0 );

	if( rc != 0 ) {
		Debug( LDAP_DEBUG_ANY,
			"=> bdb_idl_insert_key: get failed: %s (%d)\n",
			db_strerror(rc), rc, 0 );
	}
	return rc;
}

int
bdb_idl_delete_key(
	BackendDB	*be,
	DB			*db,
	DB_TXN		*tid,
	DBT			*key,
	ID			id )
{
	int	rc;
	ID ids[BDB_IDL_DB_SIZE];
	DBT data;

	/* for printable keys only */
	Debug( LDAP_DEBUG_ARGS,
		"=> bdb_idl_delete_key: %s %ld\n",
		key->data, (long) id, 0 );

	assert( id != NOID );

	data.data = ids;
	data.ulen = sizeof( ids );
	data.flags = DB_DBT_USERMEM;

	/* fetch the key for read/modify/write */
	rc = db->get( db, tid, key, &data, DB_RMW );

	if ( rc != 0 ) {
		Debug( LDAP_DEBUG_ANY,
			"=> bdb_idl_delete_key: get failed: %s (%d)\n",
			db_strerror(rc), rc, 0 );
		return rc;

	} else if ( data.size == 0 || data.size % sizeof( ID ) ) {
		/* size not multiple of ID size */
		Debug( LDAP_DEBUG_ANY,
			"=> bdb_idl_delete_key: odd size: expected %ld multiple, got %ld\n",
			(long) sizeof( ID ), (long) data.size, 0 );
		return -1;
	
	} else if ( BDB_IDL_IS_RANGE(ids) ) {
		return 0;

	} else if ( data.size != (1 + ids[0]) * sizeof( ID ) ) {
		/* size mismatch */
		Debug( LDAP_DEBUG_ANY,
			"=> bdb_idl_delete_key: get size mismatch: expected %ld, got %ld\n",
			(long) ((1 + ids[0]) * sizeof( ID )), (long) data.size, 0 );
		return -1;

	} else {
		rc = idl_delete( ids, id );

		if( rc != 0 ) {
			Debug( LDAP_DEBUG_ANY,
				"=> bdb_idl_delete_key: idl_delete failed (%d)\n",
				rc, 0, 0 );
			return rc;
		}

		if( ids[0] == 0 ) {
			/* delete the key */
			rc = db->del( db, tid, key, 0 );
			if( rc != 0 ) {
				Debug( LDAP_DEBUG_ANY,
					"=> bdb_idl_delete_key: delete failed: %s (%d)\n",
					db_strerror(rc), rc, 0 );
			}
			return rc;
		}

		data.size = (ids[0]+1) * sizeof( ID );
	}

	/* store the key */
	rc = db->put( db, tid, key, &data, 0 );

	if( rc != 0 ) {
		Debug( LDAP_DEBUG_ANY,
			"=> bdb_idl_delete_key: put failed: %s (%d)\n",
			db_strerror(rc), rc, 0 );
	}

	return rc;
}


/*
 * idl_intersection - return a intersection b
 */
int
bdb_idl_intersection(
	ID *a,
	ID *b,
	ID *ids )
{
	ID ida, idb;
	ID cursora, cursorb;

	if ( BDB_IDL_IS_ZERO( a ) || BDB_IDL_IS_ZERO( b ) ) {
		ids[0] = 0;
		return 0;
	}

	if ( BDB_IDL_IS_RANGE( a ) && BDB_IDL_IS_RANGE(b) ) {
		ids[0] = NOID;
		ids[1] = IDL_MAX( BDB_IDL_FIRST(a), BDB_IDL_FIRST(b) );
		ids[2] = IDL_MIN( BDB_IDL_LAST(a), BDB_IDL_FIRST(b) );

		if ( ids[1] == ids[2] ) {
			ids[0] = 1;
		} else if( ids[1] > ids[2] ) {
			ids[0] = 0;
		}
		return 0;
	}

	if( BDB_IDL_IS_RANGE( a ) ) {
		ID *tmp = a;
		a = b;
		b = tmp;
	}

	ida = bdb_idl_first( a, &cursora ),
	idb = bdb_idl_first( b, &cursorb );

	ids[0] = 0;

	while( ida != NOID && idb != NOID ) {
		if( ida == idb ) {
			ids[++ids[0]] = ida;
			ida = bdb_idl_next( a, &cursora );
			idb = bdb_idl_next( b, &cursorb );
			if( BDB_IDL_IS_RANGE( b ) && idb < ida ) {
				if( ida > BDB_IDL_LAST( b ) ) {
					idb = NOID;
				} else {
					idb = ida;
					cursorb = ida;
				}
			}
		} else if ( ida < idb ) {
			ida = bdb_idl_next( a, &cursora );
		} else {
			idb = bdb_idl_next( b, &cursorb );
		}
	}

	return 0;
}


/*
 * idl_union - return a union b
 */
int
bdb_idl_union(
	ID	*a,
	ID	*b,
	ID *ids )
{
	ID ida, idb;
	ID cursora, cursorb;

	if ( BDB_IDL_IS_ZERO( a ) ) {
		BDB_IDL_CPY( ids, b );
		return 0;
	}

	if ( BDB_IDL_IS_ZERO( b ) ) {
		BDB_IDL_CPY( ids, a );
		return 0;
	}

	if ( BDB_IDL_IS_RANGE( a ) || BDB_IDL_IS_RANGE(b) ) {
		ids[0] = NOID;
		ids[1] = IDL_MIN( BDB_IDL_FIRST(a), BDB_IDL_FIRST(b) );
		ids[2] = IDL_MAX( BDB_IDL_LAST(a), BDB_IDL_FIRST(b) );
		return 0;
	}

	ida = bdb_idl_first( a, &cursora ),
	idb = bdb_idl_first( b, &cursorb );

	ids[0] = 0;

	while( ida != NOID && idb != NOID ) {
		if( ++ids[0] > BDB_IDL_MAX ) {
			ids[0] = NOID;
			ids[2] = IDL_MAX( BDB_IDL_LAST(a), BDB_IDL_LAST(b) );
			break;
		}

		if ( ida < idb ) {
			ids[ids[0]] = ida;
			ida = bdb_idl_next( a, &cursora );

		} else if ( ida > idb ) {
			ids[ids[0]] = idb;
			idb = bdb_idl_next( b, &cursorb );

		} else {
			ids[ids[0]] = ida;
			ida = bdb_idl_next( a, &cursora );
			idb = bdb_idl_next( b, &cursorb );
		}
	}

	return 0;
}


/*
 * bdb_idl_notin - return a intersection ~b (or a minus b)
 */
int
bdb_idl_notin(
	ID	*a,
	ID	*b,
	ID *ids )
{
	ID ida, idb;
	ID cursora, cursorb;

	if( BDB_IDL_IS_ZERO( a ) ||
		BDB_IDL_IS_ZERO( b ) ||
		BDB_IDL_IS_RANGE( b ) )
	{
		BDB_IDL_CPY( ids, a );
		return 0;
	}

	if( BDB_IDL_IS_RANGE( a ) ) {
		BDB_IDL_CPY( ids, a );
		return 0;
	}

	ida = bdb_idl_first( a, &cursora ),
	idb = bdb_idl_first( b, &cursorb );

	ids[0] = 0;

	while( ida != NOID ) {
		if ( idb == NOID ) {
			/* we could shortcut this */
			ids[++ids[0]] = ida;
			ida = bdb_idl_next( a, &cursora );

		} else if ( ida < idb ) {
			ids[++ids[0]] = ida;
			ida = bdb_idl_next( a, &cursora );

		} else if ( ida > idb ) {
			idb = bdb_idl_next( b, &cursorb );

		} else {
			ida = bdb_idl_next( a, &cursora );
			idb = bdb_idl_next( b, &cursorb );
		}
	}

	return 0;
}

ID bdb_idl_first( ID *ids, ID *cursor )
{
	ID pos;

	if ( ids[0] == 0 ) {
		*cursor = NOID;
		return NOID;
	}

	if ( BDB_IDL_IS_RANGE( ids ) ) {
		if( *cursor < ids[1] ) {
			*cursor = ids[1];
		}
		return *cursor;
	}

	pos = bdb_idl_search( ids, *cursor );

	if( pos > ids[0] ) {
		return NOID;
	}

	*cursor = pos;
	return ids[pos];
}

ID bdb_idl_next( ID *ids, ID *cursor )
{
	if ( BDB_IDL_IS_RANGE( ids ) ) {
		if( ids[2] < ++(*cursor) ) {
			return NOID;
		}
		return *cursor;
	}

	if ( *cursor < ids[0] ) {
		return ids[(*cursor)++];
	}

	return NOID;
}

