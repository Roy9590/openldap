/* index.c - routines for dealing with attribute indexes */

#include "portable.h"

#include <stdio.h>

#include <ac/string.h>
#include <ac/socket.h>

#include "slap.h"
#include "back-ldbm.h"

static int	change_value(Backend *be,
			  DBCache *db,
			  char *type,
			  int indextype,
			  char *val,
			  ID id,
			  int
			  (*idl_func)(Backend *, DBCache *, Datum, ID));
static int	index2prefix(int indextype);

int
index_add_entry(
    Backend	*be,
    Entry	*e
)
{
	Attribute	*ap;
	struct berval	bv;
	struct berval	*bvals[2];

	Debug( LDAP_DEBUG_TRACE, "=> index_add( %ld, \"%s\" )\n", e->e_id,
	    e->e_dn, 0 );

	/*
	 * dn index entry - make it look like an attribute so it works
	 * with index_change_values() call
	 */

	bv.bv_val = ch_strdup( e->e_ndn );
	bv.bv_len = strlen( bv.bv_val );
	bvals[0] = &bv;
	bvals[1] = NULL;

	/* add the dn to the indexes */
	{
		char *dn = ch_strdup("dn");
		index_change_values( be, dn, bvals, e->e_id, __INDEX_ADD_OP );
		free( dn );
	}

	free( bv.bv_val );

	/* add each attribute to the indexes */
	for ( ap = e->e_attrs; ap != NULL; ap = ap->a_next ) {

		index_change_values( be, ap->a_type, ap->a_vals, e->e_id,
				     __INDEX_ADD_OP );
	}

	Debug( LDAP_DEBUG_TRACE, "<= index_add( %ld, \"%s\" ) 0\n", e->e_id,
	    e->e_ndn, 0 );
	return( 0 );
}

int
index_add_mods(
    Backend	*be,
    LDAPModList	*ml,
    ID		id
)
{
	int	rc;

	for ( ; ml != NULL; ml = ml->ml_next ) {
		LDAPMod *mod = &ml->ml_mod;

		switch ( mod->mod_op & ~LDAP_MOD_BVALUES ) {
		case LDAP_MOD_REPLACE:
			/* XXX: Delete old index data==>problem when this 
			 * gets called we lost values already!
			 */
		case LDAP_MOD_ADD:
			rc = index_change_values( be,
					       mod->mod_type,
					       mod->mod_bvalues,
					       id,
					       __INDEX_ADD_OP);
			break;
		case LDAP_MOD_DELETE:
			rc =  index_change_values( be,
						   mod->mod_type,
						   mod->mod_bvalues,
						   id,
						   __INDEX_DELETE_OP );
			break;
 		case LDAP_MOD_SOFTADD:	/* SOFTADD means index was there */
			rc = 0;
			break;
		}

		if ( rc != 0 ) {
			return( rc );
		}
	}

	return( 0 );
}

ID_BLOCK *
index_read(
    Backend	*be,
    char	*type,
    int		indextype,
    char	*val
)
{
	DBCache	*db;
	Datum   	key;
	ID_BLOCK		*idl;
	int		indexmask, syntax;
	char		prefix;
	char		*realval, *tmpval;
	char		buf[BUFSIZ];

	char		*at_cn;

	ldbm_datum_init( key );

	prefix = index2prefix( indextype );
	Debug( LDAP_DEBUG_TRACE, "=> index_read( \"%s\" \"%c\" \"%s\" )\n",
	    type, prefix, val );

	attr_masks( be->be_private, type, &indexmask, &syntax );
	if ( ! (indextype & indexmask) ) {
		idl =  idl_allids( be );
		Debug( LDAP_DEBUG_TRACE,
		    "<= index_read %ld candidates (allids - not indexed)\n",
		    idl ? ID_BLOCK_NIDS(idl) : 0, 0, 0 );
		return( idl );
	}

	attr_normalize( type );
	at_cn = at_canonical_name( type );

	if ( (db = ldbm_cache_open( be, at_cn, LDBM_SUFFIX, LDBM_WRCREAT ))
	    == NULL ) {
		Debug( LDAP_DEBUG_ANY,
		    "<= index_read NULL (could not open %s%s)\n", at_cn,
		    LDBM_SUFFIX, 0 );
		return( NULL );
	}

	realval = val;
	tmpval = NULL;
	if ( prefix != UNKNOWN_PREFIX ) {
              unsigned int	len = strlen( val );

              if ( (len + 2) < sizeof(buf) ) {
			realval = buf;
		} else {
			/* value + prefix + null */
			tmpval = (char *) ch_malloc( len + 2 );
			realval = tmpval;
		}
              realval[0] = prefix;
              strcpy( &realval[1], val );
	}

	key.dptr = realval;
	key.dsize = strlen( realval ) + 1;

	idl = idl_fetch( be, db, key );
	if ( tmpval != NULL ) {
              free( tmpval );
	}

	ldbm_cache_close( be, db );

	Debug( LDAP_DEBUG_TRACE, "<= index_read %ld candidates\n",
	       idl ? ID_BLOCK_NIDS(idl) : 0, 0, 0 );
	return( idl );
}

/* Add or remove stuff from index files */

static int
change_value(
    Backend		*be,
    DBCache	*db,
    char		*type,
    int			indextype,
    char		*val,
    ID			id,
    int			(*idl_func)(Backend *, DBCache *, Datum, ID)
)
{
	int	rc;
	Datum   key;
	char	*tmpval = NULL;
	char	*realval = val;
	char	buf[BUFSIZ];

	char	prefix = index2prefix( indextype );

	ldbm_datum_init( key );

	Debug( LDAP_DEBUG_TRACE,
	       "=> change_value( \"%c%s\", op=%s )\n",
	       prefix, val, (idl_func == idl_insert_key ? "ADD":"DELETE") );

	if ( prefix != UNKNOWN_PREFIX ) {
              unsigned int     len = strlen( val );

              if ( (len + 2) < sizeof(buf) ) {
			realval = buf;
	      } else {
			/* value + prefix + null */
			tmpval = (char *) ch_malloc( len + 2 );
			realval = tmpval;
	      }
              realval[0] = prefix;
              strcpy( &realval[1], val );
	}

	key.dptr = realval;
	key.dsize = strlen( realval ) + 1;

	rc = idl_func( be, db, key, id );

	if ( tmpval != NULL ) {
		free( tmpval );
	}

	ldap_pvt_thread_yield();

	Debug( LDAP_DEBUG_TRACE, "<= change_value %d\n", rc, 0, 0 );

	return( rc );

}/* static int change_value() */


int
index_change_values(
    Backend		*be,
    char		*type,
    struct berval	**vals,
    ID			id,
    unsigned int	op
)
{
	char		*val, *p, *code, *w;
	unsigned	i, j, len;
	int		indexmask, syntax;
	char		buf[SUBLEN + 1];
	char		vbuf[BUFSIZ];
	char		*bigbuf;
	DBCache	*db;

	int		(*idl_funct)(Backend *,
				    DBCache *,
				    Datum, ID);
	char		*at_cn;	/* Attribute canonical name */
	int		mode;

	Debug( LDAP_DEBUG_TRACE,
	       "=> index_change_values( \"%s\", %ld, op=%s )\n", 
	       type, id, ((op == __INDEX_ADD_OP) ? "ADD" : "DELETE" ) );

	
	if (op == __INDEX_ADD_OP) {

	    /* Add values */

	    idl_funct =  idl_insert_key;
	    mode = LDBM_WRCREAT;

	} else {

	    /* Delete values */

	    idl_funct = idl_delete_key;
	    mode = LDBM_WRITER;

	}

	attr_normalize(type);
	attr_masks( be->be_private, type, &indexmask, &syntax );

	if ( indexmask == 0 ) {
		return( 0 );
	}

	at_cn = at_canonical_name( type );

	if ( (db = ldbm_cache_open( be, at_cn, LDBM_SUFFIX, mode ))
	     == NULL ) {
		Debug( LDAP_DEBUG_ANY,
		       "<= index_change_values (couldn't open(%s%s),md=%s)\n",
		       at_cn,
		       LDBM_SUFFIX,
		       ((mode==LDBM_WRCREAT)?"LDBM_WRCREAT":"LDBM_WRITER") );
		return( -1 );
	}


	for ( i = 0; vals[i] != NULL; i++ ) {
		/*
		 * presence index entry
		 */
		if ( indexmask & INDEX_PRESENCE ) {

			change_value( be, db, at_cn, INDEX_PRESENCE,
				      "*", id, idl_funct );

		}

		Debug( LDAP_DEBUG_TRACE,
		       "index_change_values syntax 0x%x syntax bin 0x%x\n",
		       syntax, SYNTAX_BIN, 0 );

		if ( syntax & SYNTAX_BIN ) {

			ldbm_cache_close( be, db );
			return( 0 );

		}

		bigbuf = NULL;
		len = vals[i]->bv_len;

		/* value + null */
		if ( len + 2 > sizeof(vbuf) ) {
			bigbuf = (char *) ch_malloc( len + 1 );
			val = bigbuf;
		} else {
			val = vbuf;
		}
		(void) memcpy( val, vals[i]->bv_val, len );
		val[len] = '\0';

		value_normalize( val, syntax );

		/* value_normalize could change the length of val */
		len = strlen( val );

		/*
		 * equality index entry
		 */
		if ( indexmask & INDEX_EQUALITY ) {
		    
			change_value( be, db, at_cn, INDEX_EQUALITY,
				      val, id, idl_funct);

		}

		/*
		 * approximate index entry
		 */
		if ( indexmask & INDEX_APPROX ) {
			for ( w = first_word( val ); w != NULL;
			    w = next_word( w ) ) {
				if ( (code = phonetic( w )) != NULL ) {
					change_value( be,
						      db,
						      at_cn,
						      INDEX_APPROX,
						      code,
						      id,
						      idl_funct );
					free( code );
				}
			}
		}

		/*
		 * substrings index entry
		 */
		if ( indexmask & INDEX_SUB ) {
			/* leading and trailing */
			if ( len > SUBLEN - 2 ) {
				buf[0] = '^';
				for ( j = 0; j < SUBLEN - 1; j++ ) {
					buf[j + 1] = val[j];
				}
				buf[SUBLEN] = '\0';

				change_value( be, db, at_cn, INDEX_SUB,
					      buf, id, idl_funct );

				p = val + len - SUBLEN + 1;
				for ( j = 0; j < SUBLEN - 1; j++ ) {
					buf[j] = p[j];
				}
				buf[SUBLEN - 1] = '$';
				buf[SUBLEN] = '\0';

				change_value( be, db, at_cn, INDEX_SUB,
					      buf, id, idl_funct );
			}

			/* any */
			for ( p = val; p < (val + len - SUBLEN + 1); p++ ) {
				for ( j = 0; j < SUBLEN; j++ ) {
					buf[j] = p[j];
				}
				buf[SUBLEN] = '\0';

				change_value( be, db, at_cn, INDEX_SUB,
					      buf, id, idl_funct );
			}
		}

		if ( bigbuf != NULL ) {
			free( bigbuf );
		}
	}
	ldbm_cache_close( be, db );

	return( 0 );

}/* int index_change_values() */

static int
index2prefix( int indextype )
{
	int	prefix;

	switch ( indextype ) {
	case INDEX_EQUALITY:
		prefix = EQ_PREFIX;
		break;
	case INDEX_APPROX:
		prefix = APPROX_PREFIX;
		break;
	case INDEX_SUB:
		prefix = SUB_PREFIX;
		break;
	default:
		prefix = UNKNOWN_PREFIX;
		break;
	}

	return( prefix );
}
