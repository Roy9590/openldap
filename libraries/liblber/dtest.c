/* dtest.c - lber decoding test program */
/*
 * Copyright 1998-1999 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */
/* Portions
 * Copyright (c) 1990 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

#include "portable.h"

#include <stdio.h>

#include <ac/stdlib.h>
#include <ac/string.h>
#include <ac/socket.h>
#include <ac/unistd.h>

#ifdef HAVE_CONSOLE_H
#include <console.h>
#endif

#include <lber.h>

static void usage( char *name )
{
	fprintf( stderr, "usage: %s fmt\n", name );
}

int
main( int argc, char **argv )
{
	char *s;
	int rc;

	unsigned long tag, len;

	BerElement	*ber;
	Sockbuf		*sb;

	/* enable debugging */
	int ival = -1;
	ber_set_option( NULL, LBER_OPT_DEBUG_LEVEL, &ival );

	if ( argc < 2 ) {
		usage( argv[0] );
		return( EXIT_FAILURE );
	}

#ifdef HAVE_CONSOLE_H
	ccommand( &argv );
	cshow( stdout );
#endif

	sb = ber_sockbuf_alloc_fd( fileno(stdin) );

	if( (ber = ber_alloc_t(LBER_USE_DER)) == NULL ) {
		perror( "ber_alloc_t" );
		return( EXIT_FAILURE );
	}

	if(( tag = ber_get_next( sb, &len, ber) ) == LBER_ERROR ) {
		perror( "ber_get_next" );
		return( EXIT_FAILURE );
	}

	printf("decode: message tag 0x%lx and length %ld\n",
		tag, len );

	for( s = argv[1]; *s; s++ ) {
		char buf[128];
		char fmt[2];
		fmt[0] = *s;
		fmt[1] = '\0';

		printf("decode: format %s\n", fmt );
		len = sizeof(buf);
		rc = ber_scanf( ber, fmt, &buf[0], &len );

		if( rc == LBER_ERROR ) {
			perror( "ber_scanf" );
			return( EXIT_FAILURE );
		}
	}

	ber_sockbuf_free( sb );
	return( EXIT_SUCCESS );
}
