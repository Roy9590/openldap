/* dn2id.c - routines to deal with the dn2id index */

#include "portable.h"

#include <stdio.h>

#include <ac/string.h>
#include <ac/socket.h>

#include "slap.h"
#include "back-ldbm.h"
#include "proto-back-ldbm.h"

int
dn2id_add(
    Backend	*be,
    char	*dn,
    ID		id
)
{
	int		rc, flags;
	struct dbcache	*db;
	Datum		key, data;
	struct ldbminfo *li = (struct ldbminfo *) be->be_private;

	ldbm_datum_init( key );
	ldbm_datum_init( data );

	Debug( LDAP_DEBUG_TRACE, "=> dn2id_add( \"%s\", %ld )\n", dn, id, 0 );

	if ( (db = ldbm_cache_open( be, "dn2id", LDBM_SUFFIX, LDBM_WRCREAT ))
	    == NULL ) {
		Debug( LDAP_DEBUG_ANY, "Could not open/create dn2id%s\n",
		    LDBM_SUFFIX, 0, 0 );
		return( -1 );
	}

	dn = ch_strdup( dn );
	dn_normalize_case( dn );

	key.dptr = dn;
	key.dsize = strlen( dn ) + 1;
	data.dptr = (char *) &id;
	data.dsize = sizeof(ID);

	flags = LDBM_INSERT;
	if ( li->li_dbcachewsync ) flags |= LDBM_SYNC;

	rc = ldbm_cache_store( db, key, data, flags );

	free( dn );
	ldbm_cache_close( be, db );

	Debug( LDAP_DEBUG_TRACE, "<= dn2id_add %d\n", rc, 0, 0 );
	return( rc );
}

ID
dn2id(
    Backend	*be,
    char	*dn
)
{
	struct ldbminfo	*li = (struct ldbminfo *) be->be_private;
	struct dbcache	*db;
	ID		id;
	Datum		key, data;

	ldbm_datum_init( key );
	ldbm_datum_init( data );

	dn = ch_strdup( dn );
	Debug( LDAP_DEBUG_TRACE, "=> dn2id( \"%s\" )\n", dn, 0, 0 );
	dn_normalize_case( dn );

	/* first check the cache */
	if ( (id = cache_find_entry_dn2id( be, &li->li_cache, dn )) != NOID ) {
		free( dn );
		Debug( LDAP_DEBUG_TRACE, "<= dn2id %lu (in cache)\n", id,
			0, 0 );
		return( id );
	}

	if ( (db = ldbm_cache_open( be, "dn2id", LDBM_SUFFIX, LDBM_WRCREAT ))
		== NULL ) {
		free( dn );
		Debug( LDAP_DEBUG_ANY, "<= dn2id could not open dn2id%s\n",
			LDBM_SUFFIX, 0, 0 );
		return( NOID );
	}

	key.dptr = dn;
	key.dsize = strlen( dn ) + 1;

	data = ldbm_cache_fetch( db, key );

	ldbm_cache_close( be, db );
	free( dn );

	if ( data.dptr == NULL ) {
		Debug( LDAP_DEBUG_TRACE, "<= dn2id NOID\n", 0, 0, 0 );
		return( NOID );
	}

	(void) memcpy( (char *) &id, data.dptr, sizeof(ID) );

	ldbm_datum_free( db->dbc_db, data );

	Debug( LDAP_DEBUG_TRACE, "<= dn2id %lu\n", id, 0, 0 );
	return( id );
}

int
dn2id_delete(
    Backend	*be,
    char	*dn
)
{
	struct ldbminfo	*li = (struct ldbminfo *) be->be_private;
	struct dbcache	*db;
	Datum		key;
	int		rc;

	ldbm_datum_init( key );

	Debug( LDAP_DEBUG_TRACE, "=> dn2id_delete( \"%s\" )\n", dn, 0, 0 );

	if ( (db = ldbm_cache_open( be, "dn2id", LDBM_SUFFIX, LDBM_WRCREAT ))
	    == NULL ) {
		Debug( LDAP_DEBUG_ANY,
		    "<= dn2id_delete could not open dn2id%s\n", LDBM_SUFFIX,
		    0, 0 );
		return( -1 );
	}

	dn = ch_strdup( dn );
	dn_normalize_case( dn );
	key.dptr = dn;
	key.dsize = strlen( dn ) + 1;

	rc = ldbm_cache_delete( db, key );

	ldbm_cache_close( be, db );

	Debug( LDAP_DEBUG_TRACE, "<= dn2id_delete %d\n", rc, 0, 0 );
	return( rc );
}

/*
 * dn2entry - look up dn in the cache/indexes and return the corresponding
 * entry.
 */

static Entry *
dn2entry(
    Backend	*be,
    char	*dn,
    char	**matched,
    int         rw
)
{
	struct ldbminfo *li = (struct ldbminfo *) be->be_private;
	ID		id;
	Entry		*e = NULL;
	char		*pdn;

	Debug(LDAP_DEBUG_TRACE, "dn2entry_%s: dn: \"%s\"\n",
		rw ? "w" : "r", dn, 0);

	*matched = NULL;

	if ( (id = dn2id( be, dn )) != NOID &&
		(e = id2entry( be, id, rw )) != NULL )
	{
		return( e );
	}

	/* stop when we get to the suffix */
	if ( be_issuffix( be, dn ) ) {
		return( NULL );
	}

	/* entry does not exist - see how much of the dn does exist */
	if ( (pdn = dn_parent( be, dn )) != NULL ) {
		/* get entry with reader lock */
		if ( (e = dn2entry_r( be, pdn, matched )) != NULL ) {
			if(*matched != NULL) {
				free(*matched);
			}
			*matched = pdn;
			/* free entry with reader lock */
			cache_return_entry_r( &li->li_cache, e );
		} else {
			free( pdn );
		}
	}

	return( NULL );
}

Entry *
dn2entry_r(
	Backend	*be,
	char	*dn,
	char	**matched
)
{
	return( dn2entry( be, dn, matched, 0 ) );
}

Entry *
dn2entry_w(
	Backend	*be,
	char	*dn,
	char	**matched
)
{
	return( dn2entry( be, dn, matched, 1 ) );
}



