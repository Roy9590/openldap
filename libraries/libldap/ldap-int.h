/*
 * Copyright 1998-1999 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */
/*  Portions
 *  Copyright (c) 1995 Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  ldap-int.h - defines & prototypes internal to the LDAP library
 */

#ifndef	_LDAP_INT_H
#define	_LDAP_INT_H 1

#ifdef LDAP_COMPILING_R
#define LDAP_THREAD_SAFE 1
#endif

#include "../liblber/lber-int.h"

#define ldap_debug	(openldap_ldap_global_options.ldo_debug)
#undef Debug
#define Debug( level, fmt, arg1, arg2, arg3 ) \
	ldap_log_printf( NULL, (level), (fmt), (arg1), (arg2), (arg3) )

#include "ldap_log.h"

#include "ldap.h"

#include "ldap_pvt.h"

LDAP_BEGIN_DECL

#define LDAP_URL_PREFIX         "ldap://"
#define LDAP_URL_PREFIX_LEN     7
#define LDAP_URL_URLCOLON	"URL:"
#define LDAP_URL_URLCOLON_LEN	4

#ifdef LDAP_API_FEATURE_X_OPENLDAP_V2_REFERRALS
#define LDAP_REF_STR		"Referral:\n"
#define LDAP_REF_STR_LEN	10
#define LDAP_LDAP_REF_STR	LDAP_URL_PREFIX
#define LDAP_LDAP_REF_STR_LEN	LDAP_URL_PREFIX_LEN
#ifdef LDAP_API_FEATURE_X_OPENLDAP_V2_DNS
#define LDAP_DX_REF_STR		"dx://"
#define LDAP_DX_REF_STR_LEN	5
#endif /* LDAP_API_FEATURE_X_OPENLDAP_V2_DNS */
#endif /* LDAP_API_FEATURE_X_OPENLDAP_V2_REFERRALS */

#define LDAP_BOOL_REFERRALS		0
#define LDAP_BOOL_RESTART		1
#define LDAP_BOOL_DNS			2

#define LDAP_BOOLEANS	unsigned long
#define LDAP_BOOL(n)	(1 << (n))
#define LDAP_BOOL_GET(lo, bool)	((lo)->ldo_booleans & LDAP_BOOL(bool) \
									?  LDAP_OPT_ON : LDAP_OPT_OFF)
#define LDAP_BOOL_SET(lo, bool) ((lo)->ldo_booleans |= LDAP_BOOL(bool))
#define LDAP_BOOL_CLR(lo, bool) ((lo)->ldo_booleans &= ~LDAP_BOOL(bool))
#define LDAP_BOOL_ZERO(lo) ((lo)->ldo_booleans = 0)

/*
 * This structure represents both ldap messages and ldap responses.
 * These are really the same, except in the case of search responses,
 * where a response has multiple messages.
 */

struct ldapmsg {
	int		lm_msgid;	/* the message id */
	int		lm_msgtype;	/* the message type */
	BerElement	*lm_ber;	/* the ber encoded message contents */
	struct ldapmsg	*lm_chain;	/* for search - next msg in the resp */
	struct ldapmsg	*lm_next;	/* next response */
	unsigned int	lm_time;	/* used to maintain cache */
};

/*
 * structure representing get/set'able options
 * which have global defaults.
 */
struct ldapoptions {
	int		ldo_debug;

	int		ldo_version;	/* version to connect at */
	int		ldo_deref;
	int		ldo_timelimit;
	int		ldo_sizelimit;

	int		ldo_defport;
	char*	ldo_defbase;
	char*	ldo_defhost;

	int		ldo_cldaptries;	/* connectionless search retry count */
	int		ldo_cldaptimeout;/* time between retries */
	int		ldo_refhoplimit;	/* limit on referral nesting */

	/* LDAPv3 server and client controls */
	LDAPControl	**ldo_server_controls;
	LDAPControl **ldo_client_controls;
	
	LDAP_BOOLEANS ldo_booleans;	/* boolean options */
};

#ifdef LDAP_API_FEATURE_X_OPENLDAP_V2_REFERRALS
/*
 * structure for tracking LDAP server host, ports, DNs, etc.
 */
typedef struct ldap_server {
	char			*lsrv_host;
	char			*lsrv_dn;	/* if NULL, use default */
	int			lsrv_port;
	struct ldap_server	*lsrv_next;
} LDAPServer;


/*
 * structure for representing an LDAP server connection
 */
typedef struct ldap_conn {
	Sockbuf			*lconn_sb;
	int			lconn_refcnt;
	time_t		lconn_lastused;	/* time */
	int			lconn_status;
#define LDAP_CONNST_NEEDSOCKET		1
#define LDAP_CONNST_CONNECTING		2
#define LDAP_CONNST_CONNECTED		3
	LDAPServer		*lconn_server;
	char			*lconn_krbinstance;
	struct ldap_conn	*lconn_next;
} LDAPConn;


/*
 * structure used to track outstanding requests
 */
typedef struct ldapreq {
	int		lr_msgid;	/* the message id */
	int		lr_status;	/* status of request */
#define LDAP_REQST_INPROGRESS	1
#define LDAP_REQST_CHASINGREFS	2
#define LDAP_REQST_NOTCONNECTED	3
#define LDAP_REQST_WRITING	4
	int		lr_outrefcnt;	/* count of outstanding referrals */
	int		lr_origid;	/* original request's message id */
	int		lr_parentcnt;	/* count of parent requests */
	int		lr_res_msgtype;	/* result message type */
	int		lr_res_errno;	/* result LDAP errno */
	char		*lr_res_error;	/* result error string */
	char		*lr_res_matched;/* result matched DN string */
	BerElement	*lr_ber;	/* ber encoded request contents */
	LDAPConn	*lr_conn;	/* connection used to send request */
	struct ldapreq	*lr_parent;	/* request that spawned this referral */
	struct ldapreq	*lr_refnext;	/* next referral spawned */
	struct ldapreq	*lr_prev;	/* previous request */
	struct ldapreq	*lr_next;	/* next request */
} LDAPRequest;
#endif /* LDAP_API_FEATURE_X_OPENLDAP_V2_REFERRALS */

/*
 * structure for client cache
 */
#define LDAP_CACHE_BUCKETS	31	/* cache hash table size */
typedef struct ldapcache {
	LDAPMessage	*lc_buckets[LDAP_CACHE_BUCKETS];/* hash table */
	LDAPMessage	*lc_requests;			/* unfulfilled reqs */
	long		lc_timeout;			/* request timeout */
	long		lc_maxmem;			/* memory to use */
	long		lc_memused;			/* memory in use */
	int		lc_enabled;			/* enabled? */
	unsigned long	lc_options;			/* options */
#define LDAP_CACHE_OPT_CACHENOERRS	0x00000001
#define LDAP_CACHE_OPT_CACHEALLERRS	0x00000002
}  LDAPCache;
#define NULLLDCACHE ((LDAPCache *)NULL)


/*
 * structure representing an ldap connection
 */

struct ldap {
	Sockbuf		ld_sb;		/* socket descriptor & buffer */

	struct ldapoptions ld_options;

#define ld_deref		ld_options.ldo_deref
#define ld_timelimit	ld_options.ldo_timelimit
#define ld_sizelimit	ld_options.ldo_sizelimit

#define ld_defbase		ld_options.ldo_defbase
#define ld_defhost		ld_options.ldo_defhost
#define ld_defport		ld_options.ldo_defport

#define ld_cldaptries	ld_options.ldo_cldaptries
#define ld_cldaptimeout	ld_options.ldo_cldaptimeout
#define ld_refhoplimit	ld_options.ldo_refhoplimit

	int		ld_version;		/* version connected at */
	char	*ld_host;
	int		ld_port;

	char	ld_lberoptions;

	LDAPFiltDesc	*ld_filtd;	/* from getfilter for ufn searches */
	char		*ld_ufnprefix;	/* for incomplete ufn's */

	int		ld_errno;
	char	*ld_error;
	char	*ld_matched;
	int		ld_msgid;

	/* do not mess with these */
#ifdef LDAP_API_FEATURE_X_OPENLDAP_V2_REFERRALS
	LDAPRequest	*ld_requests;	/* list of outstanding requests */
#else /* LDAP_API_FEATURE_X_OPENLDAP_V2_REFERRALS */
	LDAPMessage	*ld_requests;	/* list of outstanding requests */
#endif /* LDAP_API_FEATURE_X_OPENLDAP_V2_REFERRALS */
	LDAPMessage	*ld_responses;	/* list of outstanding responses */
	int		*ld_abandoned;	/* array of abandoned requests */
	char		ld_attrbuffer[LDAP_MAX_ATTR_LEN];
	LDAPCache	*ld_cache;	/* non-null if cache is initialized */
	char		*ld_cldapdn;	/* DN used in connectionless search */

	/* do not mess with the rest though */
	BERTranslateProc ld_lber_encode_translate_proc;
	BERTranslateProc ld_lber_decode_translate_proc;

#ifdef LDAP_API_FEATURE_X_OPENLDAP_V2_REFERRALS
	LDAPConn	*ld_defconn;	/* default connection */
	LDAPConn	*ld_conns;	/* list of server connections */
	void		*ld_selectinfo;	/* platform specifics for select */
	int		(*ld_rebindproc)( struct ldap *ld, char **dnp,
				char **passwdp, int *authmethodp, int freeit );
				/* routine to get info needed for re-bind */
#endif /* LDAP_API_FEATURE_X_OPENLDAP_V2_REFERRALS */
};

/*
 * in init.c
 */
extern int openldap_ldap_initialized;
extern struct ldapoptions openldap_ldap_global_options;
void openldap_ldap_initialize LDAP_P((void));

/*
 * in print.c
 */
int ldap_log_printf LDAP_P((LDAP *ld, int level, char *fmt, ...));

/*
 * in cache.c
 */
void ldap_add_request_to_cache LDAP_P(( LDAP *ld, unsigned long msgtype,
        BerElement *request ));
void ldap_add_result_to_cache LDAP_P(( LDAP *ld, LDAPMessage *result ));
int ldap_check_cache LDAP_P(( LDAP *ld, unsigned long msgtype, BerElement *request ));

/*
 * in controls.c
 */
LDAPControl *ldap_control_dup LDAP_P(( LDAPControl *ctrl ));
LDAPControl **ldap_controls_dup LDAP_P(( LDAPControl **ctrl ));
int ldap_get_ber_controls LDAP_P(( BerElement *be, LDAPControl ***cp));

/*
 * in dsparse.c
 */
int next_line_tokens LDAP_P(( char **bufp, long *blenp, char ***toksp ));
void free_strarray LDAP_P(( char **sap ));

#ifdef HAVE_KERBEROS
/*
 * in kerberos.c
 */
char *ldap_get_kerberosv4_credentials LDAP_P(( LDAP *ld, char *who, char *service,
        int *len ));

#endif /* HAVE_KERBEROS */


/*
 * in open.c
 */
int open_ldap_connection( LDAP *ld, Sockbuf *sb, char *host, int defport,
	char **krbinstancep, int async );


/*
 * in os-ip.c
 */
int ldap_connect_to_host( Sockbuf *sb, char *host, unsigned long address, int port,
	int async );
void ldap_close_connection( Sockbuf *sb );

#ifdef HAVE_KERBEROS
char *ldap_host_connected_to( Sockbuf *sb );
#endif /* HAVE_KERBEROS */

#ifdef LDAP_API_FEATURE_X_OPENLDAP_V2_REFERRALS
int do_ldap_select( LDAP *ld, struct timeval *timeout );
void *ldap_new_select_info( void );
void ldap_free_select_info( void *sip );
void ldap_mark_select_write( LDAP *ld, Sockbuf *sb );
void ldap_mark_select_read( LDAP *ld, Sockbuf *sb );
void ldap_mark_select_clear( LDAP *ld, Sockbuf *sb );
int ldap_is_read_ready( LDAP *ld, Sockbuf *sb );
int ldap_is_write_ready( LDAP *ld, Sockbuf *sb );
#endif /* LDAP_API_FEATURE_X_OPENLDAP_V2_REFERRALS */


/*
 * in request.c
 */
int ldap_send_initial_request( LDAP *ld, unsigned long msgtype,
	char *dn, BerElement *ber );
BerElement *ldap_alloc_ber_with_options( LDAP *ld );
void ldap_set_ber_options( LDAP *ld, BerElement *ber );

#if defined( LDAP_API_FEATURE_X_OPENLDAP_V2_REFERRALS ) || defined( LDAP_API_FEATURE_X_OPENLDAP_V2_DNS )
int ldap_send_server_request( LDAP *ld, BerElement *ber, int msgid,
	LDAPRequest *parentreq, LDAPServer *srvlist, LDAPConn *lc,
	int bind );
LDAPConn *ldap_new_connection( LDAP *ld, LDAPServer **srvlistp, int use_ldsb,
	int connect, int bind );
LDAPRequest *ldap_find_request_by_msgid( LDAP *ld, int msgid );
void ldap_free_request( LDAP *ld, LDAPRequest *lr );
void ldap_free_connection( LDAP *ld, LDAPConn *lc, int force, int unbind );
void ldap_dump_connection( LDAP *ld, LDAPConn *lconns, int all );
void ldap_dump_requests_and_responses( LDAP *ld );
#endif /* LDAP_API_FEATURE_X_OPENLDAP_V2_REFERRALS || LDAP_API_FEATURE_X_OPENLDAP_V2_DNS */

#ifdef LDAP_API_FEATURE_X_OPENLDAP_V2_REFERRALS
int ldap_chase_referrals( LDAP *ld, LDAPRequest *lr, char **errstrp, int *hadrefp );
int ldap_append_referral( LDAP *ld, char **referralsp, char *s );
#endif /* LDAP_API_FEATURE_X_OPENLDAP_V2_REFERRALS */

/*
 * in result.c:
 */
#ifdef LDAP_CONNECTIONLESS
LDAP_F int cldap_getmsg	( LDAP *ld, struct timeval *timeout, BerElement *ber );
#endif

/*
 * in search.c
 */
BerElement *ldap_build_search_req( LDAP *ld, char *base, int scope,
	char *filter, char **attrs, int attrsonly );

/*
 * in strdup.c
 */
char *ldap_strdup LDAP_P(( const char * ));

/*
 * in unbind.c
 */
int ldap_ld_free( LDAP *ld, int close );
int ldap_send_unbind( LDAP *ld, Sockbuf *sb );

#ifdef LDAP_API_FEATURE_X_OPENLDAP_V2_DNS
/*
 * in getdxbyname.c
 */
char **ldap_getdxbyname( char *domain );
#endif /* LDAP_API_FEATURE_X_OPENLDAP_V2_DNS */

#if defined( STR_TRANSLATION ) && defined( LDAP_DEFAULT_CHARSET )
/*
 * in charset.c
 *
 * added-in this stuff so that libldap.a would build, i.e. refs to 
 * these routines from open.c would resolve. 
 * hodges@stanford.edu 5-Feb-96
 */
#if LDAP_CHARSET_8859 == LDAP_DEFAULT_CHARSET
extern 
int ldap_t61_to_8859( char **bufp, unsigned long *buflenp, int free_input );
extern 
int ldap_8859_to_t61( char **bufp, unsigned long *buflenp, int free_input );
#endif /* LDAP_CHARSET_8859 == LDAP_DEFAULT_CHARSET */
#endif /* STR_TRANSLATION && LDAP_DEFAULT_CHARSET */

LDAP_END_DECL

#endif /* _LDAP_INT_H */
