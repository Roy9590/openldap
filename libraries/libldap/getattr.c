/*
 * Copyright 1998-1999 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */
/*  Portions
 *  Copyright (c) 1990 Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  getattr.c
 */

#include "portable.h"

#include <stdio.h>
#include <stdlib.h>

#include <ac/ctype.h>
#include <ac/socket.h>
#include <ac/string.h>
#include <ac/time.h>

#include "ldap-int.h"

char *
ldap_first_attribute( LDAP *ld, LDAPMessage *entry, BerElement **ber )
{
	char *attr;

	assert( ld != NULL );
	assert( entry != NULL );
	assert( ber != NULL );

	Debug( LDAP_DEBUG_TRACE, "ldap_first_attribute\n", 0, 0, 0 );

	if ( (*ber = ldap_alloc_ber_with_options( ld )) == NULLBER ) {
		*ber = NULL;
		return( NULL );
	}

	**ber = *entry->lm_ber;

	/* 
	 * Skip past the sequence, dn, sequence of sequence, snarf the
	 * attribute type, and skip the set of values, leaving us
	 * positioned right before the next attribute type/value sequence.
	 */

	if ( ber_scanf( *ber, "{x{{ax}", &attr )
	    == LBER_ERROR ) {
		ld->ld_errno = LDAP_DECODING_ERROR;
		ber_free( *ber, 0 );
		*ber = NULL;
		return( NULL );
	}

	return( attr );
}

/* ARGSUSED */
char *
ldap_next_attribute( LDAP *ld, LDAPMessage *entry, BerElement *ber )
{
	char *attr;

	assert( ld != NULL );
	assert( entry != NULL );
	assert( ber != NULL );

	Debug( LDAP_DEBUG_TRACE, "ldap_next_attribute\n", 0, 0, 0 );

	/* skip sequence, snarf attribute type, skip values */
	if ( ber_scanf( ber, "{ax}", &attr ) 
	    == LBER_ERROR ) {
		ld->ld_errno = LDAP_DECODING_ERROR;
		return( NULL );
	}

	return( attr );
}
