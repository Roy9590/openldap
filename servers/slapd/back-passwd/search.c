/* search.c - /etc/passwd backend search function */

#include "portable.h"

#include <stdio.h>

#include <ac/ctype.h>
#include <ac/socket.h>
#include <ac/string.h>
#include <ac/time.h>

#include <pwd.h>

#include "slap.h"
#include "external.h"

static Entry *pw2entry(
	Backend *be,
	struct passwd *pw,
	char *rdn);

int
passwd_back_search(
    Backend	*be,
    Connection	*conn,
    Operation	*op,
    char	*base,
    int		scope,
    int		deref,
    int		slimit,
    int		tlimit,
    Filter	*filter,
    char	*filterstr,
    char	**attrs,
    int		attrsonly
)
{
	struct passwd	*pw;
	Entry		*e;
	char		*s;
	time_t		stoptime;

	int sent = 0;
	int err = LDAP_SUCCESS;

	char *rdn = NULL;
	char *parent = NULL;
	char *matched = NULL;
	char *user = NULL;

	tlimit = (tlimit > be->be_timelimit || tlimit < 1) ? be->be_timelimit
	    : tlimit;
	stoptime = op->o_time + tlimit;
	slimit = (slimit > be->be_sizelimit || slimit < 1) ? be->be_sizelimit
	    : slimit;

	endpwent();

#ifdef HAVE_SETPWFILE
	if ( be->be_private != NULL ) {
		(void) setpwfile( (char *) be->be_private );
	}
#endif /* HAVE_SETPWFILE */

	/* Handle a query for the base of this backend */
	if ( be_issuffix( be,  base ) ) {
		struct berval	val, *vals[2];

		vals[0] = &val;
		vals[1] = NULL;

		matched = ch_strdup( base );

		if( scope != LDAP_SCOPE_ONELEVEL ) {
			/* Create an entry corresponding to the base DN */
			e = (Entry *) ch_calloc(1, sizeof(Entry));
			e->e_attrs = NULL;
			e->e_dn = ch_strdup( base );

			/* Use the first attribute of the DN
		 	* as an attribute within the entry itself.
		 	*/
			rdn = dn_rdn(NULL, base);

			if( rdn == NULL || (s = strchr(rdn, '=')) == NULL ) {
				err = LDAP_INVALID_DN_SYNTAX;
				goto done;
			}

			val.bv_val = rdn_attr_value(rdn);
			val.bv_len = strlen( val.bv_val );
			attr_merge( e, rdn_attr_type(rdn), vals );

			free(rdn);
			rdn = NULL;

			/* Every entry needs an objectclass. We don't really
			 * know if our hardcoded choice here agrees with the
			 * DN that was configured for this backend, but it's
			 * better than nothing.
			 *
			 * should be a configuratable item
			 */
			val.bv_val = "organizationalUnit";
			val.bv_len = strlen( val.bv_val );
			attr_merge( e, "objectClass", vals );
	
			if ( test_filter( be, conn, op, e, filter ) == 0 ) {
				send_search_entry( be, conn, op,
					e, attrs, attrsonly, 0, NULL );
				sent++;
			}
		}

		if ( scope != LDAP_SCOPE_BASE ) {
			/* check all our "children" */

			for ( pw = getpwent(); pw != NULL; pw = getpwent() ) {
				/* check for abandon */
				ldap_pvt_thread_mutex_lock( &op->o_abandonmutex );
				if ( op->o_abandon ) {
					ldap_pvt_thread_mutex_unlock( &op->o_abandonmutex );
					endpwent();
					return( -1 );
				}
				ldap_pvt_thread_mutex_unlock( &op->o_abandonmutex );

				/* check time limit */
				if ( slap_get_time() > stoptime ) {
					send_ldap_result( conn, op, LDAP_TIMELIMIT_EXCEEDED,
			    		NULL, NULL, NULL, NULL );
					endpwent();
					return( 0 );
				}

				e = pw2entry( be, pw, NULL );

				if ( test_filter( be, conn, op, e, filter ) == 0 ) {
					/* check size limit */
					if ( --slimit == -1 ) {
						send_ldap_result( conn, op, LDAP_SIZELIMIT_EXCEEDED,
				    		NULL, NULL, NULL, NULL );
						endpwent();
						return( 0 );
					}

					send_search_entry( be, conn, op,
						e, attrs, attrsonly, 0, NULL );
					sent++;
				}

				entry_free( e );
			}
			endpwent();
		}

	} else {
		parent = dn_parent( be, base );

		/* This backend is only one layer deep. Don't answer requests for
		 * anything deeper than that.
		 */
		if( !be_issuffix( be, parent ) ) {
			int i;
			for( i=0; be->be_suffix[i] != NULL; i++ ) {
				if( dn_issuffix( base, be->be_suffix[i] ) ) {
					matched = ch_strdup( be->be_suffix[i] );
					break;
				}
			}
			err = LDAP_NO_SUCH_OBJECT;
			goto done;
		}

		if( scope == LDAP_SCOPE_ONELEVEL ) {
			goto done;
		}

		rdn = dn_rdn( NULL, base );

		if ( (user = rdn_attr_value(rdn)) == NULL) {
			err = LDAP_OPERATIONS_ERROR;
			goto done;
		}

		for( s = user; *s ; s++ ) {
			*s = TOLOWER( *s );
		}

		if ( (pw = getpwnam( user )) == NULL ) {
			matched = parent;
			parent = NULL;
			err = LDAP_NO_SUCH_OBJECT;
			goto done;
		}

		e = pw2entry( be, pw, rdn );

		if ( test_filter( be, conn, op, e, filter ) == 0 ) {
			send_search_entry( be, conn, op,
				e, attrs, attrsonly, 0, NULL );
			sent++;
		}

		entry_free( e );
	}

done:
	send_ldap_result( conn, op,
		err, err == LDAP_NO_SUCH_OBJECT ? matched : NULL, NULL,
		NULL, NULL );

	if( matched != NULL ) free( matched );
	if( parent != NULL ) free( parent );
	if( rdn != NULL ) free( rdn );
	if( user != NULL ) free( user );

	return( 0 );
}

static Entry *
pw2entry( Backend *be, struct passwd *pw, char *rdn )
{
	Entry		*e;
	char		buf[256];
	struct berval	val;
	struct berval	*vals[2];

	vals[0] = &val;
	vals[1] = NULL;

	/*
	 * from pw we get pw_name and make it cn
	 * give it an objectclass of person.
	 */

	e = (Entry *) ch_calloc( 1, sizeof(Entry) );
	e->e_attrs = NULL;

	/* rdn attribute type should be a configuratable item */
	sprintf( buf, "uid=%s,%s", pw->pw_name, be->be_suffix[0] );
	e->e_dn = ch_strdup( buf );
	e->e_ndn = dn_normalize_case( ch_strdup( buf ) );

	val.bv_val = pw->pw_name;
	val.bv_len = strlen( pw->pw_name );
	attr_merge( e, "uid", vals );	/* required by uidObject */
	attr_merge( e, "cn", vals );	/* required by person */
	attr_merge( e, "sn", vals );	/* required by person */

#ifdef HAVE_PW_GECOS
	/*
	 * if gecos is present, add it as a cn. first process it
	 * according to standard BSD usage. If the processed cn has
	 * a space, use the tail as the surname.
	 */
	if (pw->pw_gecos[0]) {
		char *s;

		val.bv_val = pw->pw_gecos;
		val.bv_len = strlen(val.bv_val);
		attr_merge(e, "description", vals);

		s = strchr(val.bv_val, ',');
		if (s)
			*s = '\0';
		s = strchr(val.bv_val, '&');
		if (s) {
			int i = s - val.bv_val;
			strncpy(buf, val.bv_val, i);
			s = buf+i;
			strcpy(s, pw->pw_name);
			if (islower(*s))
				*s = toupper(*s);
			strcat(s, val.bv_val+i+1);
			val.bv_val = buf;
		}
		val.bv_len = strlen(val.bv_val);
		if ( strcmp( val.bv_val, pw->pw_name ))
			attr_merge( e, "cn", vals );
		if ( (s=strrchr(val.bv_val, ' '))) {
			val.bv_val = s + 1;
			val.bv_len = strlen(val.bv_val);
			attr_merge(e, "sn", vals);
		}
	}
#endif

	/* objectclasses should be configuratable items */
	val.bv_val = "person";
	val.bv_len = strlen( val.bv_val );
	attr_merge( e, "objectclass", vals );

	val.bv_val = "uidObject";
	val.bv_len = strlen( val.bv_val );
	attr_merge( e, "objectclass", vals );
	return( e );
}
