/*
 * Copyright 1998-1999 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */
/* charray.c - routines for dealing with char * arrays */

#include "portable.h"

#include <stdio.h>

#include <ac/string.h>
#include <ac/socket.h>

#include "ldap-int.h"

int
ldap_charray_add(
    char	***a,
    char	*s
)
{
	int	n;

	if ( *a == NULL ) {
		*a = (char **) LDAP_MALLOC( 2 * sizeof(char *) );
		n = 0;
	} else {
		for ( n = 0; *a != NULL && (*a)[n] != NULL; n++ ) {
			;	/* NULL */
		}

		*a = (char **) LDAP_REALLOC( (char *) *a,
		    (n + 2) * sizeof(char *) );
	}

	if( *a == NULL ) {
		return -1;
	}

	(*a)[n] = LDAP_STRDUP(s);

	if( (*a)[n] == NULL ) {
		return 1;
	}

	(*a)[++n] = NULL;

	return 0;
}

int
ldap_charray_merge(
    char	***a,
    char	**s
)
{
	int	i, n, nn;
	char **aa;

	for ( n = 0; *a != NULL && (*a)[n] != NULL; n++ ) {
		;	/* NULL */
	}
	for ( nn = 0; s[nn] != NULL; nn++ ) {
		;	/* NULL */
	}

	aa = (char **) LDAP_REALLOC( (char *) *a, (n + nn + 1) * sizeof(char *) );

	if( aa == NULL )
		return -1;

	*a = aa;

	for ( i = 0; i < nn; i++ ) {
		(*a)[n + i] = LDAP_STRDUP(s[i]);

		if( (*a)[n + i] == NULL ) {
			for( --i ; i >= 0 ; i-- ) {
				LDAP_FREE( (*a)[n + i] );
				(*a)[n + i] = NULL;
			}
			return -1;
		}
	}

	(*a)[n + nn] = NULL;
	return 0;
}

void
ldap_charray_free( char **a )
{
	char	**p;

	if ( a == NULL ) {
		return;
	}

	for ( p = a; *p != NULL; p++ ) {
		if ( *p != NULL ) {
			LDAP_FREE( *p );
		}
	}

	LDAP_FREE( (char *) a );
}

int
ldap_charray_inlist(
    char	**a,
    char	*s
)
{
	int	i;

	for ( i = 0; a[i] != NULL; i++ ) {
		if ( strcasecmp( s, a[i] ) == 0 ) {
			return( 1 );
		}
	}

	return( 0 );
}

char **
ldap_charray_dup( char **a )
{
	int	i;
	char	**new;

	for ( i = 0; a[i] != NULL; i++ )
		;	/* NULL */

	new = (char **) LDAP_MALLOC( (i + 1) * sizeof(char *) );

	if( new == NULL ) {
		return NULL;
	}

	for ( i = 0; a[i] != NULL; i++ ) {
		new[i] = LDAP_STRDUP( a[i] );

		if( new[i] == NULL ) {
			for( --i ; i >= 0 ; i-- ) {
				LDAP_FREE( new[i] );
			}
			LDAP_FREE( new );
			return NULL;
		}
	}
	new[i] = NULL;

	return( new );
}

char **
ldap_str2charray( char *str, char *brkstr )
{
	char	**res;
	char	*s;
	char	*lasts;
	int	i;

	/* protect the input string from strtok */
	str = LDAP_STRDUP( str );
	if( str == NULL ) {
		return NULL;
	}

	i = 1;
	for ( s = str; *s; s++ ) {
		if ( strchr( brkstr, *s ) != NULL ) {
			i++;
		}
	}

	res = (char **) LDAP_MALLOC( (i + 1) * sizeof(char *) );

	if( res == NULL ) {
		LDAP_FREE( str );
		return NULL;
	}

	i = 0;

	for ( s = ldap_pvt_strtok( str, brkstr, &lasts );
		s != NULL;
		s = ldap_pvt_strtok( NULL, brkstr, &lasts ) )
	{
		res[i] = LDAP_STRDUP( s );

		if(res[i] == NULL) {
			for( --i ; i >= 0 ; i-- ) {
				LDAP_FREE( res[i] );
			}
			LDAP_FREE( res );
			LDAP_FREE( str );
			return NULL;
		}

		i++;
	}

	res[i] = NULL;

	LDAP_FREE( str );
	return( res );
}
