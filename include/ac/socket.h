/*
 * Generic socket.h
 */
/*
 * Copyright 1998,1999 The OpenLDAP Foundation, Redwood City, California, USA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted only
 * as authorized by the OpenLDAP Public License.  A copy of this
 * license is available at http://www.OpenLDAP.org/license.html or
 * in file LICENSE in the top-level directory of the distribution.
 */

#ifndef _AC_SOCKET_H_
#define _AC_SOCKET_H_

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#include <netinet/in.h>

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#ifdef HAVE_ARPA_NAMESER_H
#include <arpa/nameser.h>
#endif

#include <netdb.h>

#ifdef HAVE_RESOLV_H
#include <resolv.h>
#endif

#endif /* HAVE_SYS_SOCKET_H */

#ifdef HAVE_WINSOCK2
#include <winsock2.h>
#elif HAVE_WINSOCK
#include <winsock.h>
#else
#define WSACleanup()
#endif

#ifdef HAVE_PCNFS
#include <tklib.h>
#endif /* HAVE_PCNFS */

#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK	(0x7f000001UL)
#endif

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN  64
#endif

#ifdef HAVE_WINSOCK
#	define tcp_close( s )		closesocket( s );
#	define ioctl( s, c, a )		ioctlsocket( (s), (c), (a) )
#	define ioctl_t				u_long

#define EWOULDBLOCK WSAEWOULDBLOCK

#elif MACOS
#	define tcp_close( s )		tcpclose( s )
#elif DOS
#	ifdef PCNFS
#		define tcp_close( s )	close( s )
#	endif /* PCNFS */
#	ifdef NCSA
#		define tcp_close( s )	do { netclose( s ); netshut() } while(0)
#	endif /* NCSA */
#else
#	define tcp_close( s )		close( s )
#endif /* MACOS */

#ifndef ioctl_t
#	define ioctl_t				int
#endif

#if	defined(__WIN32) && defined(_ALPHA)
/* NT on Alpha is hosed. */
#define AC_HTONL( l ) \
        ((((l)&0xff)<<24) + (((l)&0xff00)<<8) + \
         (((l)&0xff0000)>>8) + (((l)&0xff000000)>>24))
#define AC_NTOHL(l) LBER_HTONL(l)

#elif defined(__alpha) && !defined(VMS)
/*
 * htonl and ntohl on the DEC Alpha under OSF 1 seem to only swap the
 * lower-order 32-bits of a (64-bit) long, so we define correct versions
 * here.
 */ 
#define AC_HTONL( l )	(((long)htonl( (l) & 0x00000000FFFFFFFF )) << 32 \
	| htonl( ( (l) & 0xFFFFFFFF00000000 ) >> 32 ))

#define AC_NTOHL( l ) (((long)ntohl( (l) & 0x00000000FFFFFFFF )) << 32 \
	| ntohl( ( (l) & 0xFFFFFFFF00000000 ) >> 32 ))

#else
#define AC_HTONL( l ) htonl( l )
#define AC_NTOHL( l ) ntohl( l )
#endif


#endif /* _AC_SOCKET_H_ */
