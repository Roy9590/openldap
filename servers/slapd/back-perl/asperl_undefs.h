/* This file is necessary because both PERL headers */
/* and OpenLDAP define a number of macros without   */
/* checking wether they're already defined */

#ifndef ASPERL_UNDEFS_H
#define ASPERL_UNDEFS_H

/* ActiveState Win32 PERL port support */
/* set in ldap/include/portable.h */
#  ifdef HAVE_WIN32_ASPERL
/* The following macros are undefined to prevent */
/* redefinition in PERL headers*/
#    undef gid_t
#    undef uid_t
#    undef mode_t
#    undef caddr_t
#    undef WIN32_LEAN_AND_MEAN
#  endif
#endif

