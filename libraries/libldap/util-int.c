/*
 * Copyright 1998-1999 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */
/*
 * util-int.c	Various functions to replace missing threadsafe ones.
 *				  Without the real *_r funcs, things will work, but won't be
 *				  threadsafe. 
 * 
 * Written by Bart Hartgers.
 *
 * Copyright 1998, A. Hartgers, All rights reserved.
 * This software is not subject to any license of Eindhoven University of
 * Technology, since it was written in my spare time.
 *			
 * Redistribution and use in source and binary forms are permitted only
 * as authorized by the OpenLDAP Public License.  A copy of this
 * license is available at http://www.OpenLDAP.org/license.html or
 * in file LICENSE in the top-level directory of the distribution.
 */ 

#include "portable.h"

#include <stdlib.h>

#include <ac/errno.h>
#include <ac/socket.h>
#include <ac/string.h>
#include <ac/time.h>

#include "ldap-int.h"

#if defined( LDAP_R_COMPILE )
# include <ldap_pvt_thread.h>
#else
# undef HAVE_REENTRANT_FUNCTIONS
#endif

#if (defined( HAVE_CTIME_R ) || defined( HAVE_REENTRANT_FUNCTIONS)) \
	&& defined( CTIME_R_NARGS )
#	define USE_CTIME_R
#endif

#ifdef LDAP_COMPILING_R
# ifndef USE_CTIME_R
	static ldap_pvt_thread_mutex_t ldap_int_ctime_mutex;
# endif
# ifndef HAVE_GETHOSTBYNAME_R
	static ldap_pvt_thread_mutex_t ldap_int_gethostbyname_mutex;
# endif
# ifndef HAVE_GETHOSTBYADDR_R
	static ldap_pvt_thread_mutex_t ldap_int_gethostbyaddr_mutex;
# endif
#endif /* LDAP_R_COMPILE */

char *ldap_pvt_ctime( const time_t *tp, char *buf )
{
#ifdef USE_CTIME_R
# if (CTIME_R_NARGS > 3) || (CTIME_R_NARGS < 2)
	choke me!  nargs should have 2 or 3
# elif CTIME_R_NARGS > 2
	return ctime_r(tp,buf,26);
# else
	return ctime_r(tp,buf);
# endif	  

#else
# ifdef LDAP_COMPILNG_R
	ldap_pvt_thread_mutex_lock( &ldap_int_ctime_mutex );
# endif
	memcpy( buf, ctime(tp), 26 );
# ifdef LDAP_COMPILNG_R
	ldap_pvt_thread_mutex_unlock( &ldap_int_ctime_mutex );
# endif
	return buf;
#endif	
}

#define BUFSTART 1024
#define BUFMAX (32*1024)

static char *safe_realloc( char **buf, int len );
static int copy_hostent( struct hostent *res, char **buf, struct hostent * src );

int ldap_pvt_gethostbyname_a(
	const char *name, 
	struct hostent *resbuf,
	char **buf,
	struct hostent **result,
	int *herrno_ptr )
{
#if defined( HAVE_GETHOSTBYNAME_R )
# define NEED_SAFE_REALLOC 1   
	int r=-1;
	int buflen=BUFSTART;
	*buf = NULL;
	for(;buflen<BUFMAX;) {
		if (safe_realloc( buf, buflen )==NULL)
			return r;
		r = gethostbyname_r( name, resbuf, *buf,
			buflen, result, herrno_ptr );
#ifdef NETDB_INTERNAL
		if ((r<0) &&
			(*herrno_ptr==NETDB_INTERNAL) &&
			(errno==ERANGE))
		{
			buflen*=2;
			continue;
	 	}
#endif
		return r;
	}
	return -1;
#elif defined( LDAP_COMPILING_R )
# define NEED_COPY_HOSTENT   
	struct hostent *he;
	int	retval;
	
	ldap_pvt_thread_mutex_lock( &ldap_int_gethostbyname_mutex );
	
	he = gethostbyname( name );
	
	if (he==NULL) {
		*herrno_ptr = h_errno;
		retval = -1;
	} else if (copy_hostent( resbuf, buf, he )<0) {
		*herrno_ptr = -1;
		retval = -1;
	} else {
		*result = resbuf;
		retval = 0;
	}
	
	ldap_pvt_thread_mutex_unlock( &ldap_int_gethostbyname_mutex );
	
	return retval;
#else	
	*result = gethostbyname( name );

	if (*result!=NULL) {
		return 0;
	}

	*herrno_ptr = h_errno;
	
	return -1;
#endif	
}
	 
int ldap_pvt_gethostbyaddr_a(
	const char *addr,
	int len,
	int type,
	struct hostent *resbuf,
	char **buf,
	struct hostent **result,
	int *herrno_ptr )
{
#if defined( HAVE_GETHOSTBYADDR_R )
# undef NEED_SAFE_REALLOC
# define NEED_SAFE_REALLOC   
	int r=-1;
	int buflen=BUFSTART;
	*buf = NULL;   
	for(;buflen<BUFMAX;) {
		if (safe_realloc( buf, buflen )==NULL)
			return r;
		r = gethostbyaddr_r( addr, len, type,
			resbuf, *buf, buflen, 
			result, herrno_ptr );
#ifdef NETDB_INTERNAL
		if ((r<0) &&
			(*herrno_ptr==NETDB_INTERNAL) &&
			(errno==ERANGE))
		{
			buflen*=2;
			continue;
		}
#endif
		return r;
	}
	return -1;
#elif defined( LDAP_COMPILING_R )
# undef NEED_COPY_HOSTENT
# define NEED_COPY_HOSTENT   
	struct hostent *he;
	int	retval;
	
	ldap_pvt_thread_mutex_lock( &ldap_int_gethostbyaddr_mutex );
	
	he = gethostbyaddr( addr, len, type );
	
	if (he==NULL) {
		*herrno_ptr = h_errno;
		retval = -1;
	} else if (copy_hostent( resbuf, buf, he )<0) {
		*herrno_ptr = -1;
		retval = -1;
	} else {
		*result = resbuf;
		retval = 0;
	}
	
	ldap_pvt_thread_mutex_unlock( &ldap_int_gethostbyaddr_mutex );
	
	return retval;   
#else /* gethostbyaddr() */
	*result = gethostbyaddr( addr, len, type );

	if (*result!=NULL) {
		return 0;
	}
	return -1;
#endif	
}
/* 
 * ldap_pvt_init_utils() should be called before any other function.
 */

void ldap_pvt_init_utils( void )
{
	static int done=0;
	if (done)
	  return;
	done=1;

#ifdef LDAP_COMPILING_R

#if !defined( USE_CTIME_R ) && !defined( HAVE_REENTRANT_FUNCTIONS )
	ldap_pvt_thread_mutex_init( &ldap_int_ctime_mutex, NULL );
#endif

#if !defined( HAVE_GETHOSTBYNAME_R )
	ldap_pvt_thread_mutex_init( &ldap_int_gethostbyname_mutex, NULL );
#endif

#if !defined( HAVE_GETHOSTBYADDR_R )
	ldap_pvt_thread_mutex_init( &ldap_int_gethostbyaddr_mutex, NULL );
#endif

	/* call other module init functions here... */
#endif
}

#if defined( NEED_COPY_HOSTENT )
# undef NEED_SAFE_REALLOC
#define NEED_SAFE_REALLOC

static char *cpy_aliases( char ***tgtio, char *buf, char **src )
{
	int len;
	char **tgt=*tgtio;
	for( ; (*src) ; src++ ) {
		len = strlen( *src ) + 1;
		memcpy( buf, *src, len );
		*tgt++=buf;
		buf+=len;
	}
	*tgtio=tgt;   
	return buf;
}

static char *cpy_addresses( char ***tgtio, char *buf, char **src, int len )
{
   	char **tgt=*tgtio;
	for( ; (*src) ; src++ ) {
		memcpy( buf, *src, len );
		*tgt++=buf;
		buf+=len;
	}
	*tgtio=tgt;      
	return buf;
}

static int copy_hostent( struct hostent *res, char **buf, struct hostent * src )
{
	char	**p;
	char	**tp;
	char	*tbuf;
	int	name_len;
	int	n_alias;
	int	total_alias_len;
	int	n_addr;
	int	total_addr_len;
	int	total_len;
	  
	/* calculate the size needed for the buffer */
	name_len = strlen( src->h_name ) + 1;
	
	for( n_alias=total_alias_len=0, p=src->h_aliases; (*p) ; p++ ) {
		total_alias_len += strlen( *p ) + 1;
		n_alias++;
	}

	for( n_addr=0, p=src->h_addr_list; (*p) ; p++ ) {
		n_addr++;
	}
	total_addr_len = n_addr * src->h_length;
	
	total_len = (n_alias + n_addr + 2) * sizeof( char * ) +
		total_addr_len + total_alias_len + name_len;
	
	if (safe_realloc( buf, total_len )) {			 
		tp = (char **) *buf;
		tbuf = *buf + (n_alias + n_addr + 2) * sizeof( char * );
		memcpy( res, src, sizeof( struct hostent ) );
		/* first the name... */
		memcpy( tbuf, src->h_name, name_len );
		res->h_name = tbuf; tbuf+=name_len;
		/* now the aliases */
		res->h_aliases = tp;
		tbuf = cpy_aliases( &tp, tbuf, src->h_aliases );
		*tp++=NULL;
		/* finally the addresses */
		res->h_addr_list = tp;
		tbuf = cpy_addresses( &tp, tbuf, src->h_addr_list, src->h_length );
		*tp++=NULL;
		return 0;
	}
	return -1;
}
#endif

#if defined( NEED_SAFE_REALLOC )
static char *safe_realloc( char **buf, int len )
{
	char *tmpbuf;
	tmpbuf = realloc( *buf, len );
	if (tmpbuf) {
		*buf=tmpbuf;
	} 
	return tmpbuf;
}
#endif


