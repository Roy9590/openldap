/* add.c - ldap ldbm back-end add routine */

#include "portable.h"

#include <stdio.h>

#include <ac/socket.h>
#include <ac/string.h>

#include "slap.h"
#include "back-ldbm.h"
#include "proto-back-ldbm.h"

int
ldbm_back_add(
    Backend	*be,
    Connection	*conn,
    Operation	*op,
    Entry	*e
)
{
	struct ldbminfo	*li = (struct ldbminfo *) be->be_private;
	char		*pdn;
	Entry		*p = NULL;
	int			rootlock = 0;
	int			rc; 

	Debug(LDAP_DEBUG_ARGS, "==> ldbm_back_add: %s\n", e->e_dn, 0, 0);

	/* nobody else can add until we lock our parent */
	ldap_pvt_thread_mutex_lock(&li->li_add_mutex);

	if ( ( dn2id( be, e->e_ndn ) ) != NOID ) {
		ldap_pvt_thread_mutex_unlock(&li->li_add_mutex);
		entry_free( e );
		send_ldap_result( conn, op, LDAP_ALREADY_EXISTS, "", "" );
		return( -1 );
	}

	if ( global_schemacheck && oc_schema_check( e ) != 0 ) {
		ldap_pvt_thread_mutex_unlock(&li->li_add_mutex);

		Debug( LDAP_DEBUG_TRACE, "entry failed schema check\n",
			0, 0, 0 );

		entry_free( e );
		send_ldap_result( conn, op, LDAP_OBJECT_CLASS_VIOLATION, "",
		    "" );
		return( -1 );
	}

	/*
	 * Get the parent dn and see if the corresponding entry exists.
	 * If the parent does not exist, only allow the "root" user to
	 * add the entry.
	 */

	if ( (pdn = dn_parent( be, e->e_ndn )) != NULL ) {
		char *matched = NULL;

		/* get parent with writer lock */
		if ( (p = dn2entry_w( be, pdn, &matched )) == NULL ) {
			ldap_pvt_thread_mutex_unlock(&li->li_add_mutex);
			Debug( LDAP_DEBUG_TRACE, "parent does not exist\n", 0,
			    0, 0 );
			send_ldap_result( conn, op, LDAP_NO_SUCH_OBJECT,
			    matched, "" );

			if ( matched != NULL ) {
				free( matched );
			}

			entry_free( e );
			free( pdn );
			return -1;
		}

		/* don't need the add lock anymore */
		ldap_pvt_thread_mutex_unlock(&li->li_add_mutex);

		free(pdn);

		if ( matched != NULL ) {
			free( matched );
		}

		if ( ! access_allowed( be, conn, op, p,
			"children", NULL, ACL_WRITE ) )
		{
			Debug( LDAP_DEBUG_TRACE, "no access to parent\n", 0,
			    0, 0 );
			send_ldap_result( conn, op, LDAP_INSUFFICIENT_ACCESS,
			    "", "" );

			/* free parent and writer lock */
			cache_return_entry_w( &li->li_cache, p ); 

			entry_free( e );
			return -1;
		}

	} else {
		/* no parent, must be adding entry to root */
		if ( ! be_isroot( be, op->o_ndn ) ) {
			ldap_pvt_thread_mutex_unlock(&li->li_add_mutex);
			Debug( LDAP_DEBUG_TRACE, "no parent & not root\n", 0,
			    0, 0 );
			send_ldap_result( conn, op, LDAP_INSUFFICIENT_ACCESS,
			    "", "" );

			entry_free( e );
			return -1;
		}

		/*
		 * no parent, acquire the root write lock
		 * and release the add lock.
		 */
		ldap_pvt_thread_mutex_lock(&li->li_root_mutex);
		rootlock = 1;
		ldap_pvt_thread_mutex_unlock(&li->li_add_mutex);
	}

	e->e_id = next_id( be );

	/*
	 * Try to add the entry to the cache, assign it a new dnid.
	 */
	rc = cache_add_entry_rw(&li->li_cache, e, ENTRY_STATE_CREATING, 1);

	if ( rc != 0 ) {
		if( p != NULL) {
			/* free parent and writer lock */
			cache_return_entry_w( &li->li_cache, p ); 
		}

		if ( rootlock ) {
			/* release root lock */
			ldap_pvt_thread_mutex_unlock(&li->li_root_mutex);
		}

		Debug( LDAP_DEBUG_ANY, "cache_add_entry_lock failed\n", 0, 0,
		    0 );

		/* return the id */
		next_id_return( be, e->e_id );

		/* free the entry */
		entry_free( e );

		if(rc > 0) {
			send_ldap_result( conn, op, LDAP_ALREADY_EXISTS, "", "" );
		} else {
			send_ldap_result( conn, op, LDAP_OPERATIONS_ERROR, "", "" );
		}

		return( -1 );
	}

	rc = -1;

	/*
	 * add it to the id2children index for the parent
	 */

	if ( id2children_add( be, p, e ) != 0 ) {
		Debug( LDAP_DEBUG_TRACE, "id2children_add failed\n", 0,
		    0, 0 );
		send_ldap_result( conn, op, LDAP_OPERATIONS_ERROR, "", "" );

		goto return_results;
	}

	/*
	 * Add the entry to the attribute indexes, then add it to
	 * the id2children index, dn2id index, and the id2entry index.
	 */

	/* attribute indexes */
	if ( index_add_entry( be, e ) != 0 ) {
		Debug( LDAP_DEBUG_TRACE, "index_add_entry failed\n", 0,
		    0, 0 );
		send_ldap_result( conn, op, LDAP_OPERATIONS_ERROR, "", "" );

		goto return_results;
	}

	/* dn2id index */
	if ( dn2id_add( be, e->e_ndn, e->e_id ) != 0 ) {
		Debug( LDAP_DEBUG_TRACE, "dn2id_add failed\n", 0,
		    0, 0 );
		send_ldap_result( conn, op, LDAP_OPERATIONS_ERROR, "", "" );

		goto return_results;
	}

	/* id2entry index */
	if ( id2entry_add( be, e ) != 0 ) {
		Debug( LDAP_DEBUG_TRACE, "id2entry_add failed\n", 0,
		    0, 0 );
		(void) dn2id_delete( be, e->e_ndn );
		send_ldap_result( conn, op, LDAP_OPERATIONS_ERROR, "", "" );

		goto return_results;
	}

	send_ldap_result( conn, op, LDAP_SUCCESS, "", "" );
	rc = 0;

return_results:;
	if (p != NULL) {
		/* free parent and writer lock */
		cache_return_entry_w( &li->li_cache, p ); 
	}

	if ( rootlock ) {
		/* release root lock */
		ldap_pvt_thread_mutex_unlock(&li->li_root_mutex);
	}

	cache_set_state( &li->li_cache, e, 0 );

	/* free entry and writer lock */
	cache_return_entry_w( &li->li_cache, e ); 

	return( rc );
}
