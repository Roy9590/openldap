/* search.c - monitor backend search function */
/*
 * Copyright 1998-2003 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */
/*
 * Copyright 2001, Pierangelo Masarati, All rights reserved. <ando@sys-net.it>
 * 
 * This work has beed deveolped for the OpenLDAP Foundation 
 * in the hope that it may be useful to the Open Source community, 
 * but WITHOUT ANY WARRANTY.
 * 
 * Permission is granted to anyone to use this software for any purpose
 * on any computer system, and to alter it and redistribute it, subject
 * to the following restrictions:
 * 
 * 1. The author and SysNet s.n.c. are not responsible for the consequences
 *    of use of this software, no matter how awful, even if they arise from
 *    flaws in it.
 * 
 * 2. The origin of this software must not be misrepresented, either by
 *    explicit claim or by omission.  Since few users ever read sources,
 *    credits should appear in the documentation.
 * 
 * 3. Altered versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.  Since few users
 *    ever read sources, credits should appear in the documentation.
 *    SysNet s.n.c. cannot be responsible for the consequences of the
 *    alterations.
 * 
 * 4. This notice may not be removed or altered.
 */

#include "portable.h"

#include <stdio.h>

#include <ac/string.h>
#include <ac/socket.h>

#include "slap.h"
#include "back-monitor.h"
#include "proto-back-monitor.h"

static int
monitor_send_children(
	/*
	Backend		*be,
    	Connection	*conn,
    	Operation	*op,
    	Filter		*filter,
    	AttributeName	*attrs,
    	int		attrsonly,
	*/
	Operation	*op,
	SlapReply	*rs,
	Entry		*e_parent,
	int		sub
)
{
	struct monitorinfo	*mi =
		(struct monitorinfo *) op->o_bd->be_private;
	Entry 			*e, *e_tmp, *e_ch;
	struct monitorentrypriv *mp;
	int			rc;

	mp = ( struct monitorentrypriv * )e_parent->e_private;
	e = mp->mp_children;

	e_ch = NULL;
	if ( MONITOR_HAS_VOLATILE_CH( mp ) ) {
		monitor_entry_create( mi, NULL, e_parent, &e_ch );
	}
	monitor_cache_release( mi, e_parent );

	/* no volatile entries? */
	if ( e_ch == NULL ) {
		/* no persistent entries? return */
		if ( e == NULL ) {
			return( 0 );
		}
	
	/* volatile entries */
	} else {
		/* if no persistent, return only volatile */
		if ( e == NULL ) {
			e = e_ch;
			monitor_cache_lock( e_ch );

		/* else append persistent to volatile */
		} else {
			e_tmp = e_ch;
			do {
				mp = ( struct monitorentrypriv * )e_tmp->e_private;
				e_tmp = mp->mp_next;
	
				if ( e_tmp == NULL ) {
					mp->mp_next = e;
					break;
				}
			} while ( e_tmp );
			e = e_ch;
		}
	}

	/* return entries */
	for ( ; e != NULL; ) {
		mp = ( struct monitorentrypriv * )e->e_private;

		monitor_entry_update( mi, e );
		
		rc = test_filter( op, e, op->oq_search.rs_filter );
		if ( rc == LDAP_COMPARE_TRUE ) {
			rs->sr_entry = e;
			send_search_entry( op, rs );
			rs->sr_entry = NULL;
		}

		if ( ( mp->mp_children || MONITOR_HAS_VOLATILE_CH( mp ) )
				&& sub ) {
			rc = monitor_send_children( op, rs, e, sub );
			if ( rc ) {
				return( rc );
			}
		}

		e_tmp = mp->mp_next;
		if ( e_tmp != NULL ) {
			monitor_cache_lock( e_tmp );
		}
		monitor_cache_release( mi, e );
		e = e_tmp;
	}
	
	return( 0 );
}

int
monitor_back_search( Operation *op, SlapReply *rs )
{
	struct monitorinfo	*mi
		= (struct monitorinfo *) op->o_bd->be_private;
	int		rc = LDAP_SUCCESS;
	Entry		*e, *matched = NULL;

#ifdef NEW_LOGGING
	LDAP_LOG( BACK_MON, ENTRY,
		   "monitor_back_search: enter\n", 0, 0, 0 );
#else
	Debug(LDAP_DEBUG_TRACE, "=> monitor_back_search\n%s%s%s", "", "", "");
#endif


	/* get entry with reader lock */
	monitor_cache_dn2entry( mi, &op->o_req_ndn, &e, &matched );
	if ( e == NULL ) {
		rs->sr_err = LDAP_NO_SUCH_OBJECT;
		if ( matched ) {
			rs->sr_matched = ch_strdup( matched->e_dn );
			monitor_cache_release( mi, matched );
		}

		send_ldap_result( op, rs );
		rs->sr_matched = NULL;

		return( 0 );
	}

	rs->sr_attrs = op->oq_search.rs_attrs;
	switch ( op->oq_search.rs_scope ) {
	case LDAP_SCOPE_BASE:
		monitor_entry_update( mi, e );
		rc = test_filter( op, e, op->oq_search.rs_filter );
 		if ( rc == LDAP_COMPARE_TRUE ) {
			rs->sr_entry = e;
			send_search_entry( op, rs );
			rs->sr_entry = NULL;
		}
		rc = LDAP_SUCCESS;
		monitor_cache_release( mi, e );
		break;

	case LDAP_SCOPE_ONELEVEL:
		rc = monitor_send_children( op, rs, e, 0 );
		if ( rc ) {
			rc = LDAP_OTHER;
		}
		
		break;

	case LDAP_SCOPE_SUBTREE:
		monitor_entry_update( mi, e );
		rc = test_filter( op, e, op->oq_search.rs_filter );
		if ( rc == LDAP_COMPARE_TRUE ) {
			rs->sr_entry = e;
			send_search_entry( op, rs );
			rs->sr_entry = NULL;
		}

		rc = monitor_send_children( op, rs, e, 1 );
		if ( rc ) {
			rc = LDAP_OTHER;
		}

		break;
	}
	
	rs->sr_attrs = NULL;
	rs->sr_err = rc;
	send_ldap_result( op, rs );

	return( rc == LDAP_SUCCESS ? 0 : 1 );
}

