/*
 * Copyright 1998-1999 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */

#include "portable.h"

#include <stdio.h>

#include <ac/ctype.h>
#include <ac/stdarg.h>
#include <ac/string.h>

#include "lber-int.h"

/*
 * Print stuff
 */
static void
ber_error_print( char *data )
{
	assert( data != NULL );

	fputs( data, stderr );
	fflush( stderr );
}

BER_LOG_PRINT_FN ber_pvt_log_print = ber_error_print;

/*
 * lber log 
 */

static int ber_log_check( int errlvl, int loglvl )
{
	return errlvl & loglvl ? 1 : 0;
}

int ber_pvt_log_printf
#ifdef HAVE_STDARG
	(int errlvl, int loglvl, const char *fmt, ...)
#else
	( va_alist )
va_dcl
#endif
{
	char buf[ 1024 ];
	va_list ap;

#ifdef HAVE_STDARG
	va_start( ap, fmt );
#else
	int errlvl, loglvl;
	char *fmt;

	va_start( ap );

	errlvl = va_arg( ap, int );
	loglvl = va_arg( ap, int );
	fmt = va_arg( ap, char * );
#endif

	assert( fmt != NULL );

	if ( !ber_log_check( errlvl, loglvl )) {
		return 0;
	}

#ifdef HAVE_VSNPRINTF
	buf[sizeof(buf) - 1] = '\0';
	vsnprintf( buf, sizeof(buf)-1, fmt, ap );
#elif HAVE_VSPRINTF
	vsprintf( buf, fmt, ap ); /* hope it's not too long */
#else
	/* use doprnt() */
#error "vsprintf() required."
#endif

	va_end(ap);

	(*ber_pvt_log_print)( buf );
	return 1;
}

static int ber_log_puts(int errlvl, int loglvl, char *buf)
{
	assert( buf != NULL );

	if ( !ber_log_check( errlvl, loglvl )) {
		return 0;
	}

	(*ber_pvt_log_print)( buf );
	return 1;
}

/*
 * Print arbitrary stuff, for debugging.
 */

int
ber_log_bprint(int errlvl,
	int loglvl,
	const char *data,
	int len )
{
	assert( data != NULL );

	if ( !ber_log_check( errlvl, loglvl )) {
		return 0;
	}

	ber_bprint(data, len);
	return 1;
}

void
ber_bprint(
	LDAP_CONST char *data,
	int len )
{
    static const char	hexdig[] = "0123456789abcdef";
#define BPLEN	48
    char	out[ BPLEN ];
    char	buf[ BPLEN + sizeof("\t%s\n") ];
    int		i = 0;

	assert( data != NULL );

    memset( out, 0, BPLEN );
    for ( ;; ) {
	if ( len < 1 ) {
	    sprintf( buf, "\t%s\n", ( i == 0 ) ? "(end)" : out );
		(*ber_pvt_log_print)( buf );
	    break;
	}

#ifndef LDAP_HEX
	if ( isgraph( (unsigned char)*data )) {
	    out[ i ] = ' ';
	    out[ i+1 ] = *data;
	} else {
#endif
	    out[ i ] = hexdig[ ( *data & 0xf0U ) >> 4 ];
	    out[ i+1 ] = hexdig[ *data & 0x0fU ];
#ifndef LDAP_HEX
	}
#endif
	i += 2;
	len--;
	data++;

	if ( i > BPLEN - 2 ) {
		char data[128 + BPLEN];
	    sprintf( data, "\t%s\n", out );
		(*ber_pvt_log_print)(data);
	    memset( out, 0, BPLEN );
	    i = 0;
	    continue;
	}
	out[ i++ ] = ' ';
    }
}

int
ber_log_dump(
	int errlvl,
	int loglvl,
	const BerElement *ber,
	int inout )
{
	assert( ber != NULL );
	assert( BER_VALID( ber ) );

	if ( !ber_log_check( errlvl, loglvl )) {
		return 0;
	}

	ber_dump(ber, inout);
	return 1;
}

void
ber_dump(
	LDAP_CONST BerElement *ber,
	int inout )
{
	char buf[132];

	assert( ber != NULL );
	assert( BER_VALID( ber ) );

	sprintf( buf, "ber_dump: buf 0x%lx, ptr 0x%lx, end 0x%lx\n",
	    (long) ber->ber_buf,
		(long) ber->ber_ptr,
		(long) ber->ber_end );

	(*ber_pvt_log_print)( buf );

	if ( inout == 1 ) {
		sprintf( buf, "          current len %ld, contents:\n",
		    (long) (ber->ber_end - ber->ber_ptr) );
		ber_bprint( ber->ber_ptr, ber->ber_end - ber->ber_ptr );

	} else {
		sprintf( buf, "          current len %ld, contents:\n",
		    (long) (ber->ber_ptr - ber->ber_buf) );

		ber_bprint( ber->ber_buf, ber->ber_ptr - ber->ber_buf );
	}
}

int
lber_log_sos_dump(
	int errlvl,
	int loglvl,
	const Seqorset *sos )
{
	assert( sos != NULL );

	if ( !ber_log_check( errlvl, loglvl )) {
		return 0;
	}

	ber_sos_dump( sos );
	return 1;
}

void
ber_sos_dump(
	LDAP_CONST Seqorset *sos )
{
	char buf[132];

	assert( sos != NULL );

	(*ber_pvt_log_print)( "*** sos dump ***\n" );

	while ( sos != NULLSEQORSET ) {
		sprintf( buf, "ber_sos_dump: clen %ld first 0x%lx ptr 0x%lx\n",
		    (long) sos->sos_clen, (long) sos->sos_first, (long) sos->sos_ptr );
		(*ber_pvt_log_print)( buf );

		sprintf( buf, "              current len %ld contents:\n",
		    (long) (sos->sos_ptr - sos->sos_first) );
		(*ber_pvt_log_print)( buf );

		ber_bprint( sos->sos_first, sos->sos_ptr - sos->sos_first );

		sos = sos->sos_next;
	}

	(*ber_pvt_log_print)( "*** end dump ***\n" );
}
