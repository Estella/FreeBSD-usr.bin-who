#	@(#)Makefile	8.1 (Berkeley) 6/6/93

PROG=	who
LDADD=	/usr/local/lib/libGeoIP.so
CFLAGS=	-I/usr/local/include

.include <bsd.prog.mk>
