/* filter.c - routines for parsing and dealing with filters */

#include "portable.h"

#include <stdio.h>

#include <ac/socket.h>
#include <ac/string.h>

#include "slap.h"

static int	get_filter_list(Connection *conn, BerElement *ber, Filter **f, char **fstr);
static int	get_substring_filter(Connection *conn, BerElement *ber, Filter *f, char **fstr);

int
get_filter( Connection *conn, BerElement *ber, Filter **filt, char **fstr )
{
	unsigned long	len;
	int		err;
	Filter		*f;
	char		*ftmp;

	Debug( LDAP_DEBUG_FILTER, "begin get_filter\n", 0, 0, 0 );

	/*
	 * A filter looks like this coming in:
	 *	Filter ::= CHOICE {
	 *		and		[0]	SET OF Filter,
	 *		or		[1]	SET OF Filter,
	 *		not		[2]	Filter,
	 *		equalityMatch	[3]	AttributeValueAssertion,
	 *		substrings	[4]	SubstringFilter,
	 *		greaterOrEqual	[5]	AttributeValueAssertion,
	 *		lessOrEqual	[6]	AttributeValueAssertion,
	 *		present		[7]	AttributeType,,
	 *		approxMatch	[8]	AttributeValueAssertion
	 *	}
	 *
	 *	SubstringFilter ::= SEQUENCE {
	 *		type               AttributeType,
	 *		SEQUENCE OF CHOICE {
	 *			initial          [0] IA5String,
	 *			any              [1] IA5String,
	 *			final            [2] IA5String
	 *		}
	 *	}
	 */

	f = (Filter *) ch_malloc( sizeof(Filter) );
	f->f_next = NULL;

	err = 0;
	*fstr = NULL;
	f->f_choice = ber_peek_tag( ber, &len );
#ifdef LDAP_COMPAT30
	if ( conn->c_version == 30 ) {
		switch ( f->f_choice ) {
		case LDAP_FILTER_EQUALITY:
		case LDAP_FILTER_GE:
		case LDAP_FILTER_LE:
		case LDAP_FILTER_PRESENT:
		case LDAP_FILTER_PRESENT_30:
		case LDAP_FILTER_APPROX:
			(void) ber_skip_tag( ber, &len );
			if ( f->f_choice == LDAP_FILTER_PRESENT_30 ) {
				f->f_choice = LDAP_FILTER_PRESENT;
			}
			break;
		default:
			break;
		}
	}
#endif
	switch ( f->f_choice ) {
	case LDAP_FILTER_EQUALITY:
		Debug( LDAP_DEBUG_FILTER, "EQUALITY\n", 0, 0, 0 );
		if ( (err = get_ava( ber, &f->f_ava )) == 0 ) {
			*fstr = ch_malloc(4 + strlen( f->f_avtype ) +
			    f->f_avvalue.bv_len);
			sprintf( *fstr, "(%s=%s)", f->f_avtype,
			    f->f_avvalue.bv_val );
		}
		break;

	case LDAP_FILTER_SUBSTRINGS:
		Debug( LDAP_DEBUG_FILTER, "SUBSTRINGS\n", 0, 0, 0 );
		err = get_substring_filter( conn, ber, f, fstr );
		break;

	case LDAP_FILTER_GE:
		Debug( LDAP_DEBUG_FILTER, "GE\n", 0, 0, 0 );
		if ( (err = get_ava( ber, &f->f_ava )) == 0 ) {
			*fstr = ch_malloc(5 + strlen( f->f_avtype ) +
			    f->f_avvalue.bv_len);
			sprintf( *fstr, "(%s>=%s)", f->f_avtype,
			    f->f_avvalue.bv_val );
		}
		break;

	case LDAP_FILTER_LE:
		Debug( LDAP_DEBUG_FILTER, "LE\n", 0, 0, 0 );
		if ( (err = get_ava( ber, &f->f_ava )) == 0 ) {
			*fstr = ch_malloc(5 + strlen( f->f_avtype ) +
			    f->f_avvalue.bv_len);
			sprintf( *fstr, "(%s<=%s)", f->f_avtype,
			    f->f_avvalue.bv_val );
		}
		break;

	case LDAP_FILTER_PRESENT:
		Debug( LDAP_DEBUG_FILTER, "PRESENT\n", 0, 0, 0 );
		if ( ber_scanf( ber, "a", &f->f_type ) == LBER_ERROR ) {
			err = LDAP_PROTOCOL_ERROR;
		} else {
			err = LDAP_SUCCESS;
			attr_normalize( f->f_type );
			*fstr = ch_malloc( 5 + strlen( f->f_type ) );
			sprintf( *fstr, "(%s=*)", f->f_type );
		}
		break;

	case LDAP_FILTER_APPROX:
		Debug( LDAP_DEBUG_FILTER, "APPROX\n", 0, 0, 0 );
		if ( (err = get_ava( ber, &f->f_ava )) == 0 ) {
			*fstr = ch_malloc(5 + strlen( f->f_avtype ) +
			    f->f_avvalue.bv_len);
			sprintf( *fstr, "(%s~=%s)", f->f_avtype,
			    f->f_avvalue.bv_val );
		}
		break;

	case LDAP_FILTER_AND:
		Debug( LDAP_DEBUG_FILTER, "AND\n", 0, 0, 0 );
		if ( (err = get_filter_list( conn, ber, &f->f_and, &ftmp ))
		    == 0 ) {
			if (ftmp == NULL) ftmp = ch_strdup("");
			*fstr = ch_malloc( 4 + strlen( ftmp ) );
			sprintf( *fstr, "(&%s)", ftmp );
			free( ftmp );
		}
		break;

	case LDAP_FILTER_OR:
		Debug( LDAP_DEBUG_FILTER, "OR\n", 0, 0, 0 );
		if ( (err = get_filter_list( conn, ber, &f->f_or, &ftmp ))
		    == 0 ) {
			if (ftmp == NULL) ftmp = ch_strdup("");
			*fstr = ch_malloc( 4 + strlen( ftmp ) );
			sprintf( *fstr, "(|%s)", ftmp );
			free( ftmp );
		}
		break;

	case LDAP_FILTER_NOT:
		Debug( LDAP_DEBUG_FILTER, "NOT\n", 0, 0, 0 );
		(void) ber_skip_tag( ber, &len );
		if ( (err = get_filter( conn, ber, &f->f_not, &ftmp )) == 0 ) {
			if (ftmp == NULL) ftmp = ch_strdup("");
			*fstr = ch_malloc( 4 + strlen( ftmp ) );
			sprintf( *fstr, "(!%s)", ftmp );
			free( ftmp );
		}
		break;

	default:
		Debug( LDAP_DEBUG_ANY, "unknown filter type %lu\n",
		       f->f_choice, 0, 0 );
		err = LDAP_PROTOCOL_ERROR;
		break;
	}

	if ( err != 0 ) {
		free( (char *) f );
		if ( *fstr != NULL ) {
			free( *fstr );
		}
	} else {
		*filt = f;
	}

	Debug( LDAP_DEBUG_FILTER, "end get_filter %d\n", err, 0, 0 );
	return( err );
}

static int
get_filter_list( Connection *conn, BerElement *ber, Filter **f, char **fstr )
{
	Filter		**new;
	int		err;
	ber_tag_t	tag;
	ber_len_t	len;
	char		*last, *ftmp;

	Debug( LDAP_DEBUG_FILTER, "begin get_filter_list\n", 0, 0, 0 );

#ifdef LDAP_COMPAT30
	if ( conn->c_version == 30 ) {
		(void) ber_skip_tag( ber, &len );
	}
#endif
	*fstr = NULL;
	new = f;
	for ( tag = ber_first_element( ber, &len, &last ); tag != LBER_DEFAULT;
	    tag = ber_next_element( ber, &len, last ) ) {
		if ( (err = get_filter( conn, ber, new, &ftmp )) != 0 )
			return( err );
		if ( *fstr == NULL ) {
			*fstr = ftmp;
		} else {
			*fstr = ch_realloc( *fstr, strlen( *fstr ) +
			    strlen( ftmp ) + 1 );
			strcat( *fstr, ftmp );
			free( ftmp );
		}
		new = &(*new)->f_next;
	}
	*new = NULL;

	Debug( LDAP_DEBUG_FILTER, "end get_filter_list\n", 0, 0, 0 );
	return( 0 );
}

static int
get_substring_filter(
    Connection	*conn,
    BerElement	*ber,
    Filter	*f,
    char	**fstr
)
{
	ber_tag_t	tag;
	ber_len_t	len;
	ber_tag_t	rc;
	char		*val, *last;
	int		syntax;

	Debug( LDAP_DEBUG_FILTER, "begin get_substring_filter\n", 0, 0, 0 );

#ifdef LDAP_COMPAT30
	if ( conn->c_version == 30 ) {
		(void) ber_skip_tag( ber, &len );
	}
#endif
	if ( ber_scanf( ber, "{a" /*}*/, &f->f_sub_type ) == LBER_ERROR ) {
		return( LDAP_PROTOCOL_ERROR );
	}
	attr_normalize( f->f_sub_type );
	syntax = attr_syntax( f->f_sub_type );
	f->f_sub_initial = NULL;
	f->f_sub_any = NULL;
	f->f_sub_final = NULL;

	*fstr = ch_malloc( strlen( f->f_sub_type ) + 3 );
	sprintf( *fstr, "(%s=", f->f_sub_type );
	for ( tag = ber_first_element( ber, &len, &last ); tag != LBER_DEFAULT;
	    tag = ber_next_element( ber, &len, last ) ) {
#ifdef LDAP_COMPAT30
		if ( conn->c_version == 30 ) {
			rc = ber_scanf( ber, "{a}", &val );
		} else
#endif
			rc = ber_scanf( ber, "a", &val );
		if ( rc == LBER_ERROR ) {
			return( LDAP_PROTOCOL_ERROR );
		}
		if ( val == NULL || *val == '\0' ) {
			if ( val != NULL ) {
				free( val );
			}
			return( LDAP_INVALID_SYNTAX );
		}
		value_normalize( val, syntax );

		switch ( tag ) {
#ifdef LDAP_COMPAT30
		case LDAP_SUBSTRING_INITIAL_30:
#endif
		case LDAP_SUBSTRING_INITIAL:
			Debug( LDAP_DEBUG_FILTER, "  INITIAL\n", 0, 0, 0 );
			if ( f->f_sub_initial != NULL ) {
				return( LDAP_PROTOCOL_ERROR );
			}
			f->f_sub_initial = val;
			*fstr = ch_realloc( *fstr, strlen( *fstr ) +
			    strlen( val ) + 1 );
			strcat( *fstr, val );
			break;

#ifdef LDAP_COMPAT30
		case LDAP_SUBSTRING_ANY_30:
#endif
		case LDAP_SUBSTRING_ANY:
			Debug( LDAP_DEBUG_FILTER, "  ANY\n", 0, 0, 0 );
			charray_add( &f->f_sub_any, val );
			*fstr = ch_realloc( *fstr, strlen( *fstr ) +
			    strlen( val ) + 2 );
			strcat( *fstr, "*" );
			strcat( *fstr, val );
			break;

#ifdef LDAP_COMPAT30
		case LDAP_SUBSTRING_FINAL_30:
#endif
		case LDAP_SUBSTRING_FINAL:
			Debug( LDAP_DEBUG_FILTER, "  FINAL\n", 0, 0, 0 );
			if ( f->f_sub_final != NULL ) {
				return( LDAP_PROTOCOL_ERROR );
			}
			f->f_sub_final = val;
			*fstr = ch_realloc( *fstr, strlen( *fstr ) +
			    strlen( val ) + 2 );
			strcat( *fstr, "*" );
			strcat( *fstr, val );
			break;

		default:
			Debug( LDAP_DEBUG_FILTER, "  unknown type\n", tag, 0,
			    0 );
			return( LDAP_PROTOCOL_ERROR );
		}
	}
	*fstr = ch_realloc( *fstr, strlen( *fstr ) + 3 );
	if ( f->f_sub_final == NULL ) {
		strcat( *fstr, "*" );
	}
	strcat( *fstr, ")" );

	Debug( LDAP_DEBUG_FILTER, "end get_substring_filter\n", 0, 0, 0 );
	return( 0 );
}

void
filter_free( Filter *f )
{
	Filter	*p, *next;

	if ( f == NULL ) {
		return;
	}

	switch ( f->f_choice ) {
	case LDAP_FILTER_EQUALITY:
	case LDAP_FILTER_GE:
	case LDAP_FILTER_LE:
	case LDAP_FILTER_APPROX:
		ava_free( &f->f_ava, 0 );
		break;

	case LDAP_FILTER_SUBSTRINGS:
		if ( f->f_sub_type != NULL ) {
			free( f->f_sub_type );
		}
		if ( f->f_sub_initial != NULL ) {
			free( f->f_sub_initial );
		}
		charray_free( f->f_sub_any );
		if ( f->f_sub_final != NULL ) {
			free( f->f_sub_final );
		}
		break;

	case LDAP_FILTER_PRESENT:
		if ( f->f_type != NULL ) {
			free( f->f_type );
		}
		break;

	case LDAP_FILTER_AND:
	case LDAP_FILTER_OR:
	case LDAP_FILTER_NOT:
		for ( p = f->f_list; p != NULL; p = next ) {
			next = p->f_next;
			filter_free( p );
		}
		break;

	default:
		Debug( LDAP_DEBUG_ANY, "unknown filter type %lu\n",
		       f->f_choice, 0, 0 );
		break;
	}
	free( f );
}

#ifdef LDAP_DEBUG

void
filter_print( Filter *f )
{
	int	i;
	Filter	*p;

	if ( f == NULL ) {
		fprintf( stderr, "NULL" );
	}

	switch ( f->f_choice ) {
	case LDAP_FILTER_EQUALITY:
		fprintf( stderr, "(%s=%s)", f->f_ava.ava_type,
		    f->f_ava.ava_value.bv_val );
		break;

	case LDAP_FILTER_GE:
		fprintf( stderr, "(%s>=%s)", f->f_ava.ava_type,
		    f->f_ava.ava_value.bv_val );
		break;

	case LDAP_FILTER_LE:
		fprintf( stderr, "(%s<=%s)", f->f_ava.ava_type,
		    f->f_ava.ava_value.bv_val );
		break;

	case LDAP_FILTER_APPROX:
		fprintf( stderr, "(%s~=%s)", f->f_ava.ava_type,
		    f->f_ava.ava_value.bv_val );
		break;

	case LDAP_FILTER_SUBSTRINGS:
		fprintf( stderr, "(%s=", f->f_sub_type );
		if ( f->f_sub_initial != NULL ) {
			fprintf( stderr, "%s", f->f_sub_initial );
		}
		if ( f->f_sub_any != NULL ) {
			for ( i = 0; f->f_sub_any[i] != NULL; i++ ) {
				fprintf( stderr, "*%s", f->f_sub_any[i] );
			}
		}
		charray_free( f->f_sub_any );
		if ( f->f_sub_final != NULL ) {
			fprintf( stderr, "*%s", f->f_sub_final );
		}
		break;

	case LDAP_FILTER_PRESENT:
		fprintf( stderr, "%s=*", f->f_type );
		break;

	case LDAP_FILTER_AND:
	case LDAP_FILTER_OR:
	case LDAP_FILTER_NOT:
		fprintf( stderr, "(%c", f->f_choice == LDAP_FILTER_AND ? '&' :
		    f->f_choice == LDAP_FILTER_OR ? '|' : '!' );
		for ( p = f->f_list; p != NULL; p = p->f_next ) {
			filter_print( p );
		}
		fprintf( stderr, ")" );
		break;

	default:
		fprintf( stderr, "unknown type %lu", f->f_choice );
		break;
	}
}

#endif /* ldap_debug */
