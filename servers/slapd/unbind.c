/* unbind.c - decode an ldap unbind operation and pass it to a backend db */
/* $OpenLDAP$ */
/*
 * Copyright 1998-2003 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */

/*
 * Copyright (c) 1995 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 *
 */

#include "portable.h"

#include <stdio.h>

#include <ac/socket.h>

#include "slap.h"

int
do_unbind( Operation *op, SlapReply *rs )
{
#ifdef NEW_LOGGING
	LDAP_LOG( OPERATION, ENTRY, 
		"do_unbind: conn %d\n", op->o_connid ? op->o_connid : -1, 0, 0 );
#else
	Debug( LDAP_DEBUG_TRACE, "do_unbind\n", 0, 0, 0 );
#endif

	/*
	 * Parse the unbind request.  It looks like this:
	 *
	 *	UnBindRequest ::= NULL
	 */

	Statslog( LDAP_DEBUG_STATS, "conn=%lu op=%lu UNBIND\n", op->o_connid,
	    op->o_opid, 0, 0, 0 );

	/* pass the unbind to all backends */
	backend_unbind( op, rs );

	return 0;
}
