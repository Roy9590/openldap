/* Generic string.h */
/*
 * Copyright 1998,1999 The OpenLDAP Foundation, Redwood City, California, USA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted only
 * as authorized by the OpenLDAP Public License.  A copy of this
 * license is available at http://www.OpenLDAP.org/license.html or
 * in file LICENSE in the top-level directory of the distribution.
 */

#ifndef _AC_STRING_H
#define _AC_STRING_H

#ifdef STDC_HEADERS
#	include <string.h>

#else
#	ifdef HAVE_STRING_H
#		include <string.h>
#	elif HAVE_STRINGS_H
#		include <strings.h>
#	endif

#	ifdef HAVE_MEMORY_H
#		include <memory.h>
#	endif

	/* we should actually create <ac/stdlib.h> */
#	ifdef HAVE_MALLOC_H
#		include <malloc.h>
#	endif

#	ifndef HAVE_STRRCHR
#		define strchr index
#		define strrchr rindex
#	endif

#	ifndef HAVE_MEMCPY
#		define memcpy(d, s, n)		((void) bcopy ((s), (d), (n)))
#		define memmove(d, s, n)		((void) bcopy ((s), (d), (n)))
#	endif
#endif

#ifndef HAVE_STRDUP
	/* strdup() is missing, declare our own version */
	extern char *strdup( const char *s );
#else
	/* some systems have strdup, but fail to declare it */
	extern char *strdup();
#endif

/*
 * some systems fail to declare strcasecmp() and strncasecmp()
 * we need them declared so we can obtain pointers to them
 */
extern int strcasecmp(), strncasecmp();

#ifndef SAFEMEMCPY
#	if defined( HAVE_MEMMOVE )
#		define SAFEMEMCPY( d, s, n ) 	memmove((d), (s), (n))
#	elif defined( HAVE_BCOPY )
#		define SAFEMEMCPY( d, s, n ) 	bcopy((s), (d), (n))
#	else
		/* nothing left but memcpy() */
#		define SAFEMEMCPY( d, s, n )	memcpy((d), (s), (n))
#	endif
#endif


#endif /* _AC_STRING_H */
