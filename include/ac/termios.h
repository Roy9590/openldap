/* Generic termios.h */

#ifndef _AC_TERMIOS_H
#define _AC_TERMIOS_H

#ifdef HAVE_POSIX_TERMIOS
#include <termios.h>

#ifdef GCWINSZ_IN_SYS_IOCTL
#include <sys/ioctl.h>
#endif

#define TERMIO_TYPE	struct termios
#define TERMFLAG_TYPE	tcflag_t
#define GETATTR( fd, tiop )	tcgetattr((fd), (tiop))
#define SETATTR( fd, tiop )	tcsetattr((fd), TCSANOW /* 0 */, (tiop))
#define GETFLAGS( tio )		((tio).c_lflag)
#define SETFLAGS( tio, flags )	((tio).c_lflag = (flags))

#elif defined( HAVE_SGTTY_H )
#include <sgtty.h>

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#define TERMIO_TYPE	struct sgttyb
#define TERMFLAG_TYPE	int
#define GETATTR( fd, tiop )	ioctl((fd), TIOCGETP, (caddr_t)(tiop))
#define SETATTR( fd, tiop )	ioctl((fd), TIOCSETP, (caddr_t)(tiop))
#define GETFLAGS( tio )     ((tio).sg_flags)
#define SETFLAGS( tio, flags )  ((tio).sg_flags = (flags))

#endif /* HAVE_SGTTY_H */

#endif /* _AC_TERMIOS_H */
