/* slap.h - stand alone ldap server include file */
/*
 * Copyright 1998-1999 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */

#ifndef _SLDAPD_H_
#define _SLDAPD_H_

#include <ac/stdlib.h>

#include <sys/types.h>
#include <ac/syslog.h>
#include <ac/regex.h>
#include <ac/socket.h>
#include <ac/time.h>

#ifdef HAVE_CYRUS_SASL
#include <sasl.h>
#endif

#include "avl.h"

#ifndef ldap_debug
#define ldap_debug slap_debug
#endif


#include "ldap_log.h"

#include <ldap.h>
#include <ldap_schema.h>

#include "ldap_pvt_thread.h"
#include "ldif.h"

LDAP_BEGIN_DECL

#ifdef f_next
#undef f_next /* name conflict between sys/file.h on SCO and struct filter */
#endif

#define SERVICE_NAME  OPENLDAP_PACKAGE "-slapd"

/* LDAPMod.mod_op value ===> Must be kept in sync with ldap.h!
 *
 * This is a value used internally by the backends. It is needed to allow
 * adding values that already exist without getting an error as required by
 * modrdn when the new rdn was already an attribute value itself.
 * JCG 05/1999 (gomez@engr.sgi.com)
 */
#define LDAP_MOD_SOFTADD	0x1000

#ifdef DNS_DN
#define DN_DNS	0
#define DN_X500	1
#endif

#define ON	1
#define OFF	(-1)
#define UNDEFINED 0

#define MAXREMATCHES 10

/* XXYYZ: these macros assume 'x' is an ASCII x */
#define DNSEPARATOR(c)	((c) == ',' || (c) == ';')
#define SEPARATOR(c)	((c) == ',' || (c) == ';' || (c) == '+')
#define SPACE(c)	((c) == ' ' || (c) == '\n')

#define ASCII_LOWER(c)	( (c) >= 'a' && (c) <= 'z' )
#define ASCII_UPPER(c)	( (c) >= 'A' && (c) <= 'Z' )
#define ASCII_ALPHA(c)	( ASCII_LOWER(c) || ASCII_UPPER(c) )
#define ASCII_DIGIT(c)	( (c) >= '0' && (c) <= '9' )
#define ASCII_ALNUM(c)	( ASCII_ALPHA(c) || ASCII_DIGIT(c) )

#define LEADKEYCHAR(c)	( ASCII_ALPHA(c) )
#define KEYCHAR(c)	( ASCII_ALNUM(c) || (c) == '-' )
#define LEADOIDCHAR(c)	( ASCII_DIGIT(c) )
#define OIDCHAR(c)	( ASCII_DIGIT(c) || (c) == '.' )

#define LEADATTRCHAR(c)	( LEADKEYCHAR(c) || LEADOIDCHAR(c) )
#define ATTRCHAR(c)	( KEYCHAR((c)) || (c) == '.' )

#define NEEDSESCAPE(c)	((c) == '\\' || (c) == '"')

#define SLAP_SCHERR_OUTOFMEM		1
#define SLAP_SCHERR_CLASS_NOT_FOUND	2
#define SLAP_SCHERR_ATTR_NOT_FOUND	3
#define SLAP_SCHERR_DUP_CLASS		4
#define SLAP_SCHERR_DUP_ATTR		5
#define SLAP_SCHERR_DUP_SYNTAX		6
#define SLAP_SCHERR_DUP_RULE		7
#define SLAP_SCHERR_NO_NAME		8
#define SLAP_SCHERR_ATTR_INCOMPLETE	9
#define SLAP_SCHERR_MR_NOT_FOUND	10
#define SLAP_SCHERR_SYN_NOT_FOUND	11
#define SLAP_SCHERR_MR_INCOMPLETE	12

extern int slap_debug;

struct slap_op;
struct slap_conn;

struct replog_moddn {
	char *newrdn;
	int	deloldrdn;
	char *newsup;
};

/*
 * represents an attribute value assertion (i.e., attr=value)
 */
typedef struct slap_ava {
	char		*ava_type;
	struct berval	ava_value;
} Ava;

typedef struct slap_mra {
	char	*mra_rule;
	char	*mra_type;
	char	*mra_value;
	int		mra_dnattrs;
} Mra;

/*
 * represents a search filter
 */
typedef struct slap_filter {
	ber_tag_t	f_choice;	/* values taken from ldap.h */

	union f_un_u {
		/* present */
		char		*f_un_type;

		/* equality, lessorequal, greaterorequal, approx */
		Ava		f_un_ava;

		/* extensible */
		Mra		f_un_fra;	

		/* and, or, not */
		struct slap_filter	*f_un_complex;

		/* substrings */
		struct sub {
			char	*f_un_sub_type;
			char	*f_un_sub_initial;
			char	**f_un_sub_any;
			char	*f_un_sub_final;
		} f_un_sub;
	} f_un;

#define f_dn		f_un.f_un_type  /* used for DN indices */
#define f_type		f_un.f_un_type
#define f_ava		f_un.f_un_ava
#define f_avtype	f_un.f_un_ava.ava_type
#define f_avvalue	f_un.f_un_ava.ava_value
#define f_mra		f_un.f_un_mra
#define f_mrtype	f_un.f_un_mra.mra_type
#define f_mrvalue	f_un.f_un_mra.mra_value
#define	f_mrdnaddrs	f_un.f_un_mra.mra_dnattrs
#define f_and		f_un.f_un_complex
#define f_or		f_un.f_un_complex
#define f_not		f_un.f_un_complex
#define f_list		f_un.f_un_complex
#define f_sub		f_un.f_un_sub
#define f_sub_type	f_un.f_un_sub.f_un_sub_type
#define f_sub_initial	f_un.f_un_sub.f_un_sub_initial
#define f_sub_any	f_un.f_un_sub.f_un_sub_any
#define f_sub_final	f_un.f_un_sub.f_un_sub_final

	struct slap_filter	*f_next;
} Filter;

/*
 * represents an attribute (type + values + syntax)
 */
typedef struct slap_attr {
	char		*a_type;
	struct berval	**a_vals;
	int		a_syntax;
	struct slap_attr	*a_next;
} Attribute;

/*
 * the attr_syntax() routine returns one of these values
 * telling what kind of syntax an attribute supports.
 */
#define SYNTAX_CIS	0x01	/* case insensitive string		*/
#define SYNTAX_CES	0x02	/* case sensitive string		*/
#define SYNTAX_BIN	0x04	/* binary data 				*/
#define SYNTAX_TEL	0x08	/* telephone number string		*/
#define SYNTAX_DN	0x10	/* dn string				*/

/*
 * the id used in the indexes to refer to an entry
 */
typedef unsigned long	ID;
#define NOID	((ID)~0)

/*
 * represents an entry in core
 */
typedef struct slap_entry {
	/*
	 * The ID field should only be changed before entry is
	 * inserted into a cache.  The ID value is backend
	 * specific.
	 */
	ID		e_id;

	char		*e_dn;		/* DN of this entry */
	char		*e_ndn;		/* normalized DN of this entry */
	Attribute	*e_attrs;	/* list of attributes + values */

	/* for use by the backend for any purpose */
	void*	e_private;
} Entry;

/*
 * represents an access control list
 */

/* the "by" part */
typedef struct slap_access {

#define ACL_NONE		0x0001
#define ACL_AUTH		0x0004
#define ACL_COMPARE		0x0008
#define ACL_SEARCH		0x0010
#define ACL_READ		0x0020
#define ACL_WRITE		0x0040
#define ACL_PRIV_MASK	0x00ff

#define ACL_SELF		0x4000
#define ACL_INVALID		(-1)

#define ACL_IS(a,lvl)	(((a) & (lvl)) == (lvl))

#define ACL_IS_NONE(a)		ACL_IS((a),ACL_SELF)
#define ACL_IS_AUTH(a)		ACL_IS((a),ACL_AUTH)
#define ACL_IS_COMPARE(a)	ACL_IS((a),ACL_COMPARE)
#define ACL_IS_SEARCH(a)	ACL_IS((a),ACL_SEARCH)
#define ACL_IS_READ(a)		ACL_IS((a),ACL_READ)
#define ACL_IS_WRITE(a)		ACL_IS((a),ACL_WRITE)
#define ACL_IS_SELF(a)		ACL_IS((a),ACL_SELF)
#define ACL_IS_INVALID(a)	((a) == ACL_INVALID)

#define ACL_CLR(a)			((a) = 0)
#define ACL_SET(a,lvl)		((a) |= (lvl))
#define ACL_SET_NONE(a)		ACL_SET((a),ACL_SELF)
#define ACL_SET_AUTH(a)		ACL_SET((a),ACL_AUTH)
#define ACL_SET_COMPARE(a)	ACL_SET((a),ACL_COMPARE)
#define ACL_SET_SEARCH(a)	ACL_SET((a),ACL_SEARCH)
#define ACL_SET_READ(a)		ACL_SET((a),ACL_READ)
#define ACL_SET_WRITE(a)	ACL_SET((a),ACL_WRITE)
#define ACL_SET_SELF(a)		ACL_SET((a),ACL_SELF)
#define ACL_SET_INVALID(a)	((a) = ACL_INVALID)

#define	ACL_PRIV(a)			((a) & ACL_PRIV_MASK)
#define ACL_GRANT(a,lvl)	(ACL_PRIV(a) >= (lvl))

	int			a_access;

	char		*a_dn_pat;
	char		*a_dn_at;

	char		*a_peername_pat;
	char		*a_sockname_pat;

	char		*a_domain_pat;
	char		*a_sockurl_pat;

	/* ACL Groups */
	char		*a_group_pat;
	char		*a_group_oc;
	char		*a_group_at;

	struct slap_access	*a_next;
} Access;

/* the "to" part */
typedef struct slap_acl {
	/* "to" part: the entries this acl applies to */
	Filter		*acl_filter;
	regex_t		acl_dn_re;
	char		*acl_dn_pat;
	char		**acl_attrs;

	/* "by" part: list of who has what access to the entries */
	Access	*acl_access;

	struct slap_acl	*acl_next;
} AccessControl;

/*
 * A list of LDAPMods
 */
typedef struct ldapmodlist {
	struct ldapmod ml_mod;
	struct ldapmodlist *ml_next;
#define ml_op		ml_mod.mod_op
#define ml_type		ml_mod.mod_type
#define ml_values	ml_mod.mod_values
#define ml_bvalues	ml_mod.mod_bvalues
} LDAPModList;

/*
 * represents schema information for a database
 */
typedef int slap_syntax_check_func LDAP_P((struct berval * val));

typedef struct slap_syntax {
	LDAP_SYNTAX			ssyn_syn;
	slap_syntax_check_func		*ssyn_check;
	struct slap_syntax		*ssyn_next;
} Syntax;
#define ssyn_oid			ssyn_syn.syn_oid
#define ssyn_desc			ssyn_syn.syn_desc

typedef int slap_mr_normalize_func LDAP_P((struct berval * val, struct berval **normalized));
typedef int slap_mr_compare_func LDAP_P((struct berval * val1, struct berval * val2));

typedef struct slap_matching_rule {
	LDAP_MATCHING_RULE		smr_mrule;
	slap_mr_normalize_func		*smr_normalize;
	slap_mr_compare_func		*smr_compare;
	Syntax				*smr_syntax;
	struct slap_matching_rule	*smr_next;
} MatchingRule;
#define smr_oid				smr_mrule.mr_oid
#define smr_names			smr_mrule.mr_names
#define smr_desc			smr_mrule.mr_desc
#define smr_obsolete			smr_mrule.mr_obsolete
#define smr_syntax_oid			smr_mrule.mr_syntax_oid

typedef struct slap_attribute_type {
	LDAP_ATTRIBUTE_TYPE		sat_atype;
	struct slap_attribute_type	*sat_sup;
	struct slap_attribute_type	**sat_subtypes;
	MatchingRule			*sat_equality;
	MatchingRule			*sat_ordering;
	MatchingRule			*sat_substr;
	Syntax				*sat_syntax;
	/* The next one is created to help in the transition */
	int				sat_syntax_compat;
	struct slap_attribute_type	*sat_next;
} AttributeType;
#define sat_oid			sat_atype.at_oid
#define sat_names		sat_atype.at_names
#define sat_desc		sat_atype.at_desc
#define sat_obsolete		sat_atype.at_obsolete
#define sat_sup_oid		sat_atype.at_sup_oid
#define sat_equality_oid	sat_atype.at_equality_oid
#define sat_ordering_oid	sat_atype.at_ordering_oid
#define sat_substr_oid		sat_atype.at_substr_oid
#define sat_syntax_oid		sat_atype.at_syntax_oid
#define sat_syntax_len		sat_atype.at_syntax_len
#define sat_single_value	sat_atype.at_single_value
#define sat_collective		sat_atype.at_collective
#define sat_no_user_mods	sat_atype.at_no_user_mods
#define sat_usage		sat_atype.at_usage

typedef struct slap_object_class {
	LDAP_OBJECT_CLASS		soc_oclass;
	struct slap_object_class	**soc_sups;
	AttributeType			**soc_required;
	AttributeType			**soc_allowed;
	struct slap_object_class	*soc_next;
} ObjectClass;
#define soc_oid			soc_oclass.oc_oid
#define soc_names		soc_oclass.oc_names
#define soc_desc		soc_oclass.oc_desc
#define soc_obsolete		soc_oclass.oc_obsolete
#define soc_sup_oids		soc_oclass.oc_sup_oids
#define soc_kind		soc_oclass.oc_kind
#define soc_at_oids_must	soc_oclass.oc_at_oids_must
#define soc_at_oids_may		soc_oclass.oc_at_oids_may

/*
 * Backend-info
 * represents a backend 
 */

typedef struct slap_backend_info BackendInfo;	/* per backend type */
typedef struct slap_backend_db BackendDB;		/* per backend database */

extern int nBackendInfo;
extern int nBackendDB;
extern BackendInfo	*backendInfo;
extern BackendDB	*backendDB;

extern int			slapMode;	
#define SLAP_UNDEFINED_MODE	0
#define SLAP_SERVER_MODE	1
#define SLAP_TOOL_MODE		2
#ifdef SLAPD_BDB2
#  define SLAP_TIMEDSERVER_MODE  3
#endif
#define SLAP_TOOLID_MODE    4

/* temporary aliases */
typedef BackendDB Backend;
#define nbackends nBackendDB
#define backends backendDB

struct slap_backend_db {
	BackendInfo	*bd_info;	/* pointer to shared backend info */

	/* BackendInfo accessors */
#define		be_config	bd_info->bi_db_config
#define		be_type		bd_info->bi_type

#define		be_bind		bd_info->bi_op_bind
#define		be_unbind	bd_info->bi_op_unbind
#define		be_add		bd_info->bi_op_add
#define		be_compare	bd_info->bi_op_compare
#define		be_delete	bd_info->bi_op_delete
#define		be_modify	bd_info->bi_op_modify
#define		be_modrdn	bd_info->bi_op_modrdn
#define		be_search	bd_info->bi_op_search

#define		be_release	bd_info->bi_entry_release_rw
#define		be_group	bd_info->bi_acl_group

#define		be_connection_init	bd_info->bi_connection_init
#define		be_connection_destroy	bd_info->bi_connection_destroy


	/* these should be renamed from be_ to bd_ */
	char	**be_suffix;	/* the DN suffixes of data in this backend */
	char	**be_nsuffix;	/* the normalized DN suffixes in this backend */
	char	**be_suffixAlias; /* pairs of DN suffix aliases and deref values */
	char	*be_root_dn;	/* the magic "root" dn for this db 	*/
	char	*be_root_ndn;	/* the magic "root" normalized dn for this db	*/
	char	*be_root_pw;	/* the magic "root" password for this db	*/
	int	be_readonly;	/* 1 => db is in "read only" mode	   */
	unsigned int be_max_deref_depth;       /* limit for depth of an alias deref  */
	int	be_sizelimit;	/* size limit for this backend   	   */
	int	be_timelimit;	/* time limit for this backend       	   */
	AccessControl *be_acl;	/* access control list for this backend	   */
	int	be_dfltaccess;	/* access given if no acl matches	   */
	char	**be_replica;	/* replicas of this backend (in master)	   */
	char	*be_replogfile;	/* replication log file (in master)	   */
	char	*be_update_ndn;	/* allowed to make changes (in replicas)   */
	struct berval **be_update_refs;	/* where to refer modifying clients to */
	int	be_lastmod;	/* keep track of lastmodified{by,time}	   */

	char	*be_realm;

	void	*be_private;	/* anything the backend database needs 	   */
};

struct slap_backend_info {
	char	*bi_type;	/* type of backend */

	/*
	 * per backend type routines:
	 * bi_init: called to allocate a backend_info structure,
	 *		called once BEFORE configuration file is read.
	 *		bi_init() initializes this structure hence is
	 *		called directly from be_initialize()
	 * bi_config: called per 'backend' specific option
	 *		all such options must before any 'database' options
	 *		bi_config() is called only from read_config()
	 * bi_open: called to open each database, called
	 *		once AFTER configuration file is read but
	 *		BEFORE any bi_db_open() calls.
	 *		bi_open() is called from backend_startup()
	 * bi_close: called to close each database, called
	 *		once during shutdown after all bi_db_close calls.
	 *		bi_close() is called from backend_shutdown()
	 * bi_destroy: called to destroy each database, called
	 *		once during shutdown after all bi_db_destroy calls.
	 *		bi_destory() is called from backend_destroy()
	 */
	int (*bi_init)	LDAP_P((BackendInfo *bi));
	int	(*bi_config) LDAP_P((BackendInfo *bi,
		char *fname, int lineno, int argc, char **argv ));
	int (*bi_open) LDAP_P((BackendInfo *bi));
	int (*bi_close) LDAP_P((BackendInfo *bi));
	int (*bi_destroy) LDAP_P((BackendInfo *bi));

	/*
	 * per database routines:
	 * bi_db_init: called to initialize each database,
	 *	called upon reading 'database <type>' 
	 *	called only from backend_db_init()
	 * bi_db_config: called to configure each database,
	 *  called per database to handle per database options
	 *	called only from read_config()
	 * bi_db_open: called to open each database
	 *	called once per database immediately AFTER bi_open()
	 *	calls but before daemon startup.
	 *  called only by backend_startup()
	 * bi_db_close: called to close each database
	 *	called once per database during shutdown but BEFORE
	 *  any bi_close call.
	 *  called only by backend_shutdown()
	 * bi_db_destroy: called to destroy each database
	 *  called once per database during shutdown AFTER all
	 *  bi_close calls but before bi_destory calls.
	 *  called only by backend_destory()
	 */
	int (*bi_db_init) LDAP_P((Backend *bd));
	int	(*bi_db_config) LDAP_P((Backend *bd,
		char *fname, int lineno, int argc, char **argv ));
	int (*bi_db_open) LDAP_P((Backend *bd));
	int (*bi_db_close) LDAP_P((Backend *bd));
	int (*bi_db_destroy) LDAP_P((Backend *db));

	/* LDAP Operations Handling Routines */
	int	(*bi_op_bind)  LDAP_P(( BackendDB *bd,
		struct slap_conn *c, struct slap_op *o,
		char *dn, int method, char* mechanism,
		struct berval *cred, char** edn ));
	int (*bi_op_unbind) LDAP_P((BackendDB *bd,
		struct slap_conn *c, struct slap_op *o ));
	int	(*bi_op_search) LDAP_P((BackendDB *bd,
		struct slap_conn *c, struct slap_op *o,
		char *base, int scope, int deref,
		int slimit, int tlimit,
		Filter *f, char *filterstr, char **attrs,
		int attrsonly));
	int	(*bi_op_compare)LDAP_P((BackendDB *bd,
		struct slap_conn *c, struct slap_op *o,
		char *dn, Ava *ava));
	int	(*bi_op_modify) LDAP_P((BackendDB *bd,
		struct slap_conn *c, struct slap_op *o,
		char *dn, LDAPModList *m));
	int	(*bi_op_modrdn) LDAP_P((BackendDB *bd,
		struct slap_conn *c, struct slap_op *o,
		char *dn, char *newrdn, int deleteoldrdn,
		char *newSuperior));
	int	(*bi_op_add)    LDAP_P((BackendDB *bd,
		struct slap_conn *c, struct slap_op *o,
		Entry *e));
	int	(*bi_op_delete) LDAP_P((BackendDB *bd,
		struct slap_conn *c, struct slap_op *o,
		char *dn));
	int	(*bi_op_abandon) LDAP_P((BackendDB *bd,
		struct slap_conn *c, struct slap_op *o,
		ber_int_t msgid));

	/* Auxilary Functions */
	int	(*bi_entry_release_rw) LDAP_P((BackendDB *bd, Entry *e, int rw));

	int	(*bi_acl_group)  LDAP_P((Backend *bd,
		Entry *e, char *bdn, char *edn,
		char *objectclassValue, char *groupattrName ));

	int	(*bi_connection_init) LDAP_P((BackendDB *bd,
		struct slap_conn *c));
	int	(*bi_connection_destroy) LDAP_P((BackendDB *bd,
		struct slap_conn *c));


	unsigned int bi_nDB;	/* number of databases of this type */
	void	*bi_private;	/* anything the backend type needs */
};

/*
 * represents an operation pending from an ldap client
 */

typedef struct slap_op {
	ber_int_t	o_opid;		/* id of this operation		  */
	ber_int_t	o_msgid;	/* msgid of the request		  */

	ldap_pvt_thread_t	o_tid;		/* thread handling this op	  */

	BerElement	*o_ber;		/* ber of the request		  */

	ber_tag_t	o_tag;		/* tag of the request		  */
	time_t		o_time;		/* time op was initiated	  */

	int		o_bind_in_progress;	/* multi-step bind in progress */

	char		*o_dn;		/* dn bound when op was initiated */
	char		*o_ndn;		/* normalized dn bound when op was initiated */
	ber_int_t	o_protocol;	/* version of the LDAP protocol used by client */
	ber_tag_t	o_authtype;	/* auth method used to bind dn	  */
					/* values taken from ldap.h	  */
					/* LDAP_AUTH_*			  */
	char		*o_authmech; /* SASL mechanism used to bind dn */

	LDAPControl	**o_ctrls;	 /* controls */

	unsigned long	o_connid; /* id of conn initiating this op  */

#ifdef LDAP_CONNECTIONLESS
	int		o_cldap;	/* != 0 if this came in via CLDAP */
	struct sockaddr	o_clientaddr;	/* client address if via CLDAP	  */
	char		o_searchbase;	/* search base if via CLDAP	  */
#endif

	ldap_pvt_thread_mutex_t	o_abandonmutex; /* protects o_abandon  */
	int		o_abandon;	/* abandon flag */

	struct slap_op	*o_next;	/* next operation in list	  */
	void	*o_private;	/* anything the backend needs	  */
} Operation;

/*
 * represents a connection from an ldap client
 */

typedef struct slap_conn {
	int			c_struct_state; /* structure management state */
	int			c_conn_state;	/* connection state */

	ldap_pvt_thread_mutex_t	c_mutex; /* protect the connection */
	Sockbuf		*c_sb;			/* ber connection stuff		  */

	/* only can be changed by connect_init */
	time_t		c_starttime;	/* when the connection was opened */
	time_t		c_activitytime;	/* when the connection was last used */
	unsigned long		c_connid;	/* id of this connection for stats*/

	char		*c_listener_url;	/* listener URL */
	char		*c_peer_domain;	/* DNS name of client */
	char		*c_peer_name;	/* peer name (trans=addr:port) */
	char		*c_sock_name;	/* sock name (trans=addr:port) */

#ifdef HAVE_CYRUS_SASL
	sasl_conn_t	*c_sasl_context;
#endif

	/* only can be changed by binding thread */
	int		c_bind_in_progress;	/* multi-op bind in progress */

	char	*c_cdn;		/* DN provided by the client */
	char	*c_dn;		/* DN bound to this conn  */
	ber_int_t	c_protocol;	/* version of the LDAP protocol used by client */
	ber_tag_t	c_authtype;/* auth method used to bind c_dn  */
	char	*c_authmech;	/* SASL mechanism used to bind c_dn */
	void	*c_authstate;	/* SASL state data */

	Operation	*c_ops;			/* list of operations being processed */
	Operation	*c_pending_ops;	/* list of pending operations */

	ldap_pvt_thread_mutex_t	c_write_mutex;	/* only one pdu written at a time */
	ldap_pvt_thread_cond_t	c_write_cv;		/* used to wait for sd write-ready*/

	BerElement	*c_currentber;	/* ber we're attempting to read */
	int		c_writewaiter;	/* true if writer is waiting */

#ifdef HAVE_TLS
	int	c_is_tls;		/* true if this LDAP over raw TLS */
	int	c_needs_tls_accept;	/* true if SSL_accept should be called */
#endif

	long	c_n_ops_received;		/* num of ops received (next op_id) */
	long	c_n_ops_executing;	/* num of ops currently executing */
	long	c_n_ops_pending;		/* num of ops pending execution */
	long	c_n_ops_completed;	/* num of ops completed */

	long	c_n_get;		/* num of get calls */
	long	c_n_read;		/* num of read calls */
	long	c_n_write;		/* num of write calls */
} Connection;

#if defined(LDAP_SYSLOG) && defined(LDAP_DEBUG)
#define Statslog( level, fmt, connid, opid, arg1, arg2, arg3 )	\
	do { \
		if ( ldap_debug & (level) ) \
			fprintf( stderr, (fmt), (connid), (opid), (arg1), (arg2), (arg3) );\
		if ( ldap_syslog & (level) ) \
			syslog( ldap_syslog_level, (fmt), (connid), (opid), (arg1), \
			        (arg2), (arg3) ); \
	} while (0)
#else
#define Statslog( level, fmt, connid, opid, arg1, arg2, arg3 )
#endif

LDAP_END_DECL

#include "proto-slap.h"

#endif /* _slap_h_ */
