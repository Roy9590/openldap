/* dn2entry.c - routines to deal with the dn2id / id2entry glue */
/* $OpenLDAP$ */
/*
 * Copyright 1998-2003 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */

#include "portable.h"

#include <stdio.h>
#include <ac/string.h>

#include "back-bdb.h"

/*
 * dn2entry - look up dn in the cache/indexes and return the corresponding
 * entry. If the requested DN is not found and matched is TRUE, return info
 * for the closest ancestor of the DN. Otherwise e is NULL.
 */

int
bdb_dn2entry(
	Operation *op,
	DB_TXN *tid,
	struct berval *dn,
	EntryInfo **e,
	int matched,
	u_int32_t locker,
	DB_LOCK *lock )
{
	EntryInfo *ei = NULL;
	int rc;

#ifdef NEW_LOGGING
	LDAP_LOG ( CACHE, ARGS, "bdb_dn2entry(\"%s\")\n", dn->bv_val, 0, 0 );
#else
	Debug(LDAP_DEBUG_TRACE, "bdb_dn2entry(\"%s\")\n",
		dn->bv_val, 0, 0 );
#endif

	*e = NULL;

	rc = bdb_cache_find_ndn( op, tid, dn, &ei, locker );
	if ( rc ) {
		if ( matched && rc == DB_NOTFOUND ) {
			/* Set the return value, whether we have its entry
			 * or not.
			 */
			*e = ei;
			if ( ei && ei->bei_id )
				bdb_cache_find_id( op, tid, ei->bei_id,
					&ei, 1, locker, lock );
			else if ( ei )
				bdb_cache_entryinfo_unlock( ei );
		} else if ( ei ) {
			bdb_cache_entryinfo_unlock( ei );
		}
	} else {
		rc = bdb_cache_find_id( op, tid, ei->bei_id, &ei, 1,
			locker, lock );
		if ( rc == 0 ) {
			*e = ei;
		} else if ( matched && rc == DB_NOTFOUND ) {
			/* always return EntryInfo */
			ei = ei->bei_parent;
			bdb_cache_find_id( op, tid, ei->bei_id, &ei, 1,
				locker, lock );
			*e = ei;
		}
	}

	return rc;
}
