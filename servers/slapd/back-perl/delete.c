/* $OpenLDAP$ */
/*
 * Copyright 1999-2003 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */
/*
 *	 Copyright 1999, John C. Quillan, All rights reserved.
 *	 Portions Copyright 2002, myinternet Limited. All rights reserved.
 *
 *	 Redistribution and use in source and binary forms are permitted only
 *	 as authorized by the OpenLDAP Public License.	A copy of this
 *	 license is available at http://www.OpenLDAP.org/license.html or
 *	 in file LICENSE in the top-level directory of the distribution.
 */

#include <EXTERN.h>
#include <perl.h>
#undef _ /* #defined by both Perl and ac/localize.h */

#ifdef HAVE_WIN32_ASPERL
#include "asperl_undefs.h"
#endif

#include "portable.h"

#include <stdio.h>

#include "slap.h"

#include "perl_back.h"

int
perl_back_delete(
	Operation	*op,
	SlapReply	*rs )
{
	PerlBackend *perl_back = (PerlBackend *) op->o_bd->be_private;
	int count;

	ldap_pvt_thread_mutex_lock( &perl_interpreter_mutex );	

	{
		dSP; ENTER; SAVETMPS;

		PUSHMARK(sp);
		XPUSHs( perl_back->pb_obj_ref );
		XPUSHs(sv_2mortal(newSVpv( op->o_req_dn.bv_val , 0 )));

		PUTBACK;

#ifdef PERL_IS_5_6
		count = call_method("delete", G_SCALAR);
#else
		count = perl_call_method("delete", G_SCALAR);
#endif

		SPAGAIN;

		if (count != 1) {
			croak("Big trouble in perl-back_delete\n");
		}

		rs->sr_err = POPi;

		PUTBACK; FREETMPS; LEAVE;
	}

	ldap_pvt_thread_mutex_unlock( &perl_interpreter_mutex );	

	send_ldap_result( op, rs );

	Debug( LDAP_DEBUG_ANY, "Perl DELETE\n", 0, 0, 0 );
	return( 0 );
}
