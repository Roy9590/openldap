/*
 * Copyright 1998-2003 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */
/*
 * (C) Copyright IBM Corp. 1997,2002
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is
 * given to IBM Corporation. This software is provided ``as is''
 * without express or implied warranty.
 */
/*
 * Portions (C) Copyright PADL Software Pty Ltd. 2003
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is 
 * given to PADL Software Pty Ltd. This software is provided ``as is'' 
 * without express or implied warranty.
 */

#include "portable.h"
#include <slap.h>
#include <lber_pvt.h>
#include <slapi.h>

/*
 * use a fake listener when faking a connection,
 * so it can be used in ACLs
 */
static struct slap_listener slap_unknown_listener = {
	BER_BVC("unknown"),	/* FIXME: use a URI form? */
	BER_BVC("UNKNOWN")
};

int bvptr2obj( struct berval **bvptr, struct berval **bvobj );

static void
internal_result_v3(
	Operation	*op, 
	SlapReply	*rs )
{
#ifdef notdef
	/* XXX needs review after internal API change */
	/* rs->sr_nentries appears to always be 0 */
	if (op->o_tag == LDAP_REQ_SEARCH)
		slapi_pblock_set( (Slapi_PBlock *)op->o_pb,
			SLAPI_NENTRIES, (void *)rs->sr_nentries );
#endif

	return;
}

static int
internal_search_entry(
	Operation	*op,
	SlapReply	*rs )
{
	char *ent2str = NULL;
	int nentries = 0, len = 0, i = 0;
	Slapi_Entry **head = NULL, **tp;
	
	ent2str = slapi_entry2str( rs->sr_entry, &len );
	if ( ent2str == NULL ) {
		return 1;
	}

	slapi_pblock_get( (Slapi_PBlock *)op->o_pb,
			SLAPI_NENTRIES, &nentries );
	slapi_pblock_get( (Slapi_PBlock *)op->o_pb,
			SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &head );
	
	i = nentries + 1;
	if ( nentries == 0 ) {
		tp = (Slapi_Entry **)slapi_ch_malloc( 2 * sizeof(Slapi_Entry *) );
		if ( tp == NULL ) {
			return 1;
		}

		tp[ 0 ] = (Slapi_Entry *)str2entry( ent2str );
		if ( tp[ 0 ] == NULL ) { 
			return 1;
		}

	} else {
		tp = (Slapi_Entry **)slapi_ch_realloc( (char *)head,
				sizeof(Slapi_Entry *) * ( i + 1 ) );
		if ( tp == NULL ) {
			return 1;
		}
		tp[ i - 1 ] = (Slapi_Entry *)str2entry( ent2str );
		if ( tp[ i - 1 ] == NULL ) { 
			return 1;
		}
	}
	tp[ i ] = NULL;
	          
	slapi_pblock_set( (Slapi_PBlock *)op->o_pb,
			SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, (void *)tp );
	slapi_pblock_set( (Slapi_PBlock *)op->o_pb,
			SLAPI_NENTRIES, (void *)i );

	return LDAP_SUCCESS;
}

static void
internal_result_ext(
	Operation	*op,	
	SlapReply	*sr )
{
	return;
}

static int
internal_search_reference(
	Operation	*op,	
	SlapReply	*sr )
{
	return LDAP_SUCCESS;
}

static Connection *
slapiConnectionInit(
	char *DN, 
	int OpType ) 
{ 
	Connection *pConn, *c;
	ber_len_t max = sockbuf_max_incoming;

	pConn = (Connection *) slapi_ch_calloc(1, sizeof(Connection));
	if (pConn == NULL) {
		return (Connection *)NULL;
	}

	LDAP_STAILQ_INIT( &pConn->c_pending_ops );

	pConn->c_pending_ops.stqh_first =
		(Operation *) slapi_ch_calloc( 1, sizeof(Operation) );
	if ( pConn->c_pending_ops.stqh_first == NULL ) { 
		slapi_ch_free( (void **)&pConn );
		return (Connection *)NULL;
	}

	pConn->c_pending_ops.stqh_first->o_pb = 
		(Slapi_PBlock *) slapi_pblock_new();
	if ( pConn->c_pending_ops.stqh_first->o_pb == NULL ) {
		slapi_ch_free( (void **)&pConn->c_pending_ops.stqh_first );
		slapi_ch_free( (void **)&pConn );
		return (Connection *)NULL;
	}

	c = pConn;

	/* operation object */
	c->c_pending_ops.stqh_first->o_tag = OpType;
	c->c_pending_ops.stqh_first->o_protocol = LDAP_VERSION3; 
	c->c_pending_ops.stqh_first->o_authmech.bv_val = NULL; 
	c->c_pending_ops.stqh_first->o_authmech.bv_len = 0; 
	c->c_pending_ops.stqh_first->o_time = slap_get_time();
	c->c_pending_ops.stqh_first->o_do_not_cache = 1;
	c->c_pending_ops.stqh_first->o_threadctx = ldap_pvt_thread_pool_context( &connection_pool );
	c->c_pending_ops.stqh_first->o_tmpmemctx = NULL;
	c->c_pending_ops.stqh_first->o_tmpmfuncs = &ch_mfuncs;
	c->c_pending_ops.stqh_first->o_conn = c;

	/* connection object */
	c->c_authmech.bv_val = NULL;
	c->c_authmech.bv_len = 0;
	c->c_dn.bv_val = NULL;
	c->c_dn.bv_len = 0;
	c->c_ndn.bv_val = NULL;
	c->c_ndn.bv_len = 0;
	c->c_groups = NULL;

	c->c_listener = &slap_unknown_listener;
	ber_dupbv( &c->c_peer_domain, (struct berval *)&slap_unknown_bv );
	ber_dupbv( &c->c_peer_name, (struct berval *)&slap_unknown_bv );

	LDAP_STAILQ_INIT( &c->c_ops );

	c->c_sasl_bind_mech.bv_val = NULL;
	c->c_sasl_bind_mech.bv_len = 0;
	c->c_sasl_context = NULL;
	c->c_sasl_extra = NULL;

	c->c_sb = ber_sockbuf_alloc( );

	ber_sockbuf_ctrl( c->c_sb, LBER_SB_OPT_SET_MAX_INCOMING, &max );

	c->c_currentber = NULL;

	/* should check status of thread calls */
	ldap_pvt_thread_mutex_init( &c->c_mutex );
	ldap_pvt_thread_mutex_init( &c->c_write_mutex );
	ldap_pvt_thread_cond_init( &c->c_write_cv );

	c->c_n_ops_received = 0;
	c->c_n_ops_executing = 0;
	c->c_n_ops_pending = 0;
	c->c_n_ops_completed = 0;

	c->c_n_get = 0;
	c->c_n_read = 0;
	c->c_n_write = 0;

	c->c_protocol = LDAP_VERSION3; 

	c->c_activitytime = c->c_starttime = slap_get_time();

	c->c_connid = 0;

	c->c_conn_state  = 0x01;	/* SLAP_C_ACTIVE */
	c->c_struct_state = 0x02;	/* SLAP_C_USED */

	c->c_ssf = c->c_transport_ssf = 0;
	c->c_tls_ssf = 0;

	backend_connection_init( c );

	pConn->c_send_ldap_result = internal_result_v3;
	pConn->c_send_search_entry = internal_search_entry;
	pConn->c_send_ldap_extended = internal_result_ext;
	pConn->c_send_search_reference = internal_search_reference;

	return pConn;
}

static void slapiConnectionDestroy( Connection **pConn )
{
	Connection *conn = *pConn;
	Operation *op;

	if ( pConn == NULL ) {
		return;
	}

	op = (Operation *)conn->c_pending_ops.stqh_first;

	if ( op->o_req_dn.bv_val != NULL ) {
		slapi_ch_free( (void **)&op->o_req_dn.bv_val );
	}
	if ( op->o_req_ndn.bv_val != NULL ) {
		slapi_ch_free( (void **)&op->o_req_ndn.bv_val );
	}

	if ( conn->c_sb != NULL ) {
		ber_sockbuf_free( conn->c_sb );
	}
	if ( op != NULL ) {
		slapi_ch_free( (void **)&op );
	}
	slapi_ch_free( (void **)pConn );
}

/*
 * Function : values2obj
 * Convert an array of strings into a BerVarray.
 * the strings.
 */
static int
values2obj(
	char **ppValue,
	BerVarray *bvobj)
{
	int i;
	BerVarray tmpberval;

	if ( ppValue == NULL ) {
		*bvobj = NULL;
		return LDAP_SUCCESS;
	}

	for ( i = 0; ppValue[i] != NULL; i++ )
		;

	tmpberval = (BerVarray)slapi_ch_malloc( (i+1) * (sizeof(struct berval)) );
	if ( tmpberval == NULL ) {
		return LDAP_NO_MEMORY;
	}
	for ( i = 0; ppValue[i] != NULL; i++ ) {
		tmpberval[i].bv_val = ppValue[i];
		tmpberval[i].bv_len = strlen( ppValue[i] );
	}
	tmpberval[i].bv_val = NULL;
	tmpberval[i].bv_len = 0;

	*bvobj = tmpberval;

	return LDAP_SUCCESS;
}

static void
freeMods( Modifications *ml )
{
	/*
	 * Free a modification list whose values have been 
	 * set with bvptr2obj() or values2obj() (ie. they
	 * do not own the pointer to the underlying values)
	 */
	Modifications *next;

	for ( ; ml != NULL; ml = next ) {
		next = ml->sml_next;

		slapi_ch_free( (void **)&ml->sml_bvalues );
		slapi_ch_free( (void **)&ml->sml_nvalues );
		slapi_ch_free( (void **)&ml );
	}
}

/*
 * Function : LDAPModToEntry 
 * convert a dn plus an array of LDAPMod struct ptrs to an entry structure
 * with a link list of the correspondent attributes.
 * Return value : LDAP_SUCCESS
 *                LDAP_NO_MEMORY
 *                LDAP_OTHER
*/
Entry *
LDAPModToEntry(
	char *ldn, 
	LDAPMod **mods )
{
	struct berval		dn = { 0, NULL };
	Entry			*pEntry=NULL;
	LDAPMod			*pMod;
	struct berval		*bv;
	Operation		*op;

	Modifications		*modlist = NULL;
	Modifications		**modtail = &modlist;
	Modifications		tmp;

	int			rc = LDAP_SUCCESS;
	int			i;

	const char 		*text = NULL;


	op = (Operation *) slapi_ch_calloc(1, sizeof(Operation));
	if ( pEntry == NULL) {
		rc = LDAP_NO_MEMORY;
		goto cleanup;
	}  
	op->o_tag = LDAP_REQ_ADD;

	pEntry = (Entry *) ch_calloc( 1, sizeof(Entry) );
	if ( pEntry == NULL) {
		rc = LDAP_NO_MEMORY;
		goto cleanup;
	} 

	dn.bv_val = slapi_ch_strdup(ldn);
	dn.bv_len = strlen(ldn);

	rc = dnPrettyNormal( NULL, &dn, &pEntry->e_name, &pEntry->e_nname );
	if ( rc != LDAP_SUCCESS )
		goto cleanup;

	if ( rc == LDAP_SUCCESS ) {
		for ( i=0, pMod=mods[0]; rc == LDAP_SUCCESS && pMod != NULL; pMod=mods[++i]) {
			Modifications *mod;
			if ( (pMod->mod_op & LDAP_MOD_BVALUES) != 0 ) {
				/* attr values are in berval format */
				/* convert an array of pointers to bervals to an array of bervals */
				rc = bvptr2obj(pMod->mod_bvalues, &bv);
				if (rc != LDAP_SUCCESS) goto cleanup;
				tmp.sml_type.bv_val = pMod->mod_type;
				tmp.sml_type.bv_len = strlen( pMod->mod_type );
				tmp.sml_bvalues = bv;
				tmp.sml_nvalues = NULL;
		
				mod  = (Modifications *) ch_malloc( sizeof(Modifications) );

				mod->sml_op = LDAP_MOD_ADD;
				mod->sml_next = NULL;
				mod->sml_desc = NULL;
				mod->sml_type = tmp.sml_type;
				mod->sml_bvalues = tmp.sml_bvalues;
				mod->sml_nvalues = tmp.sml_nvalues;

				*modtail = mod;
				modtail = &mod->sml_next;

			} else {
				/* attr values are in string format, need to be converted */
				/* to an array of bervals */ 
				if ( pMod->mod_values == NULL ) {
					rc = LDAP_OTHER;
				} else {
					rc = values2obj( pMod->mod_values, &bv );
					if (rc != LDAP_SUCCESS) goto cleanup;
					tmp.sml_type.bv_val = pMod->mod_type;
					tmp.sml_type.bv_len = strlen( pMod->mod_type );
					tmp.sml_bvalues = bv;
					tmp.sml_nvalues = NULL;
		
					mod  = (Modifications *) ch_malloc( sizeof(Modifications) );

					mod->sml_op = LDAP_MOD_ADD;
					mod->sml_next = NULL;
					mod->sml_desc = NULL;
					mod->sml_type = tmp.sml_type;
					mod->sml_bvalues = tmp.sml_bvalues;
					mod->sml_nvalues = tmp.sml_nvalues;

					*modtail = mod;
					modtail = &mod->sml_next;
				}
			}
		} /* for each LDAPMod */
	}

	op->o_bd = select_backend( &pEntry->e_nname, 0, 0 );
	if ( op->o_bd == NULL ) {
		rc = LDAP_PARTIAL_RESULTS;
	} else {
		int repl_user = be_isupdate( op->o_bd, &op->o_bd->be_rootdn );
        	if ( !op->o_bd->be_update_ndn.bv_len || repl_user ) {
			int update = op->o_bd->be_update_ndn.bv_len;
			char textbuf[SLAP_TEXT_BUFLEN];
			size_t textlen = sizeof textbuf;

			rc = slap_mods_check( modlist, update, &text, 
					textbuf, textlen );
			if ( rc != LDAP_SUCCESS) {
				goto cleanup;
			}

			if ( !repl_user ) {
				rc = slap_mods_opattrs( op,
						modlist, modtail, &text, 
						textbuf, textlen );
				if ( rc != LDAP_SUCCESS) {
					goto cleanup;
				}
			}

			/*
			 * FIXME: slap_mods2entry is declared static 
			 * in servers/slapd/add.c
			 */
			rc = slap_mods2entry( modlist, &pEntry, repl_user,
					&text, textbuf, textlen );
			if (rc != LDAP_SUCCESS) {
				goto cleanup;
			}

		} else {
			rc = LDAP_REFERRAL;
		}
	}

cleanup:

	if ( dn.bv_val )
		slapi_ch_free( (void **)&dn.bv_val );
	if ( op )
		slapi_ch_free( (void **)&op );
	if ( modlist != NULL )
		freeMods( modlist );
	if ( rc != LDAP_SUCCESS ) {
		if ( pEntry != NULL ) {
			slapi_entry_free( pEntry );
		}
		pEntry = NULL;
	}

	return( pEntry );
}

/* Function : slapi_delete_internal
 *
 * Description : Plugin functions call this routine to delete an entry 
 *               in the backend directly
 * Return values : LDAP_SUCCESS
 *                 LDAP_PARAM_ERROR
 *                 LDAP_NO_MEMORY
 *                 LDAP_OTHER
 *                 LDAP_UNWILLING_TO_PERFORM
*/
Slapi_PBlock *
slapi_delete_internal(
	char *ldn, 
	LDAPControl **controls, 
	int log_change )
{
#ifdef LDAP_SLAPI
	Connection		*pConn = NULL;
	Operation		*op = NULL;
	Slapi_PBlock		*pPB = NULL;
	Slapi_PBlock		*pSavePB = NULL;
	SlapReply		rs = { REP_RESULT };
	struct berval		dn = { 0, NULL };

	int			manageDsaIt = 0;
	int			isCritical;

	if ( ldn == NULL ) {
		rs.sr_err = LDAP_PARAM_ERROR; 
		goto cleanup;
	}

	pConn = slapiConnectionInit( NULL, LDAP_REQ_DELETE );
	if (pConn == NULL) {
		rs.sr_err = LDAP_NO_MEMORY;
		goto cleanup;
	}

	op = (Operation *)pConn->c_pending_ops.stqh_first;
	pPB = (Slapi_PBlock *)op->o_pb;
	op->o_ctrls = controls;

	dn.bv_val = slapi_ch_strdup(ldn);
	dn.bv_len = strlen(ldn);
	rs.sr_err = dnPrettyNormal( NULL, &dn, &op->o_req_dn, &op->o_req_ndn );
	if ( rs.sr_err != LDAP_SUCCESS )
		goto cleanup;

	if ( slapi_control_present( controls, 
			SLAPI_CONTROL_MANAGEDSAIT_OID, NULL, &isCritical) ) {
		manageDsaIt = 1; 
	}

	op->o_bd = select_backend( &op->o_req_ndn, manageDsaIt, 0 );
	if ( op->o_bd == NULL ) {
		rs.sr_err = LDAP_PARTIAL_RESULTS;
		goto cleanup;
	}

	op->o_dn = pConn->c_dn = op->o_bd->be_rootdn;
	op->o_ndn = pConn->c_ndn = op->o_bd->be_rootndn;

	if ( op->o_bd->be_delete ) {
		int repl_user = be_isupdate( op->o_bd, &op->o_ndn );
		if ( !op->o_bd->be_update_ndn.bv_len || repl_user ) {
			if ( (*op->o_bd->be_delete)( op, &rs ) == 0 ) {
				if ( log_change ) {
					replog( op );
				}
			} else {
				rs.sr_err = LDAP_OTHER;
			}
        	} else {
			rs.sr_err = LDAP_REFERRAL;
        	}
	} else {
		rs.sr_err = LDAP_UNWILLING_TO_PERFORM;
	}

cleanup:
	if ( pPB != NULL ) {
		slapi_pblock_set( pPB, SLAPI_PLUGIN_INTOP_RESULT, (void *)rs.sr_err );
	}
	if ( dn.bv_val ) {
		slapi_ch_free( (void **)&dn.bv_val );
	}
	if ( pConn != NULL ) {
		pSavePB = pPB;
	}

	slapiConnectionDestroy( &pConn );

	return (pSavePB);
#else
	return NULL;
#endif /* LDAP_SLAPI */
}

Slapi_PBlock * 
slapi_add_entry_internal(
	Slapi_Entry *e, 
	LDAPControl **controls, 
	int log_changes ) 
{
#ifdef LDAP_SLAPI
	Connection		*pConn = NULL;
	Operation		*op = NULL;
	Slapi_PBlock		*pPB = NULL, *pSavePB = NULL;

	int			manageDsaIt = 0;
	int			isCritical;
	SlapReply		rs = { REP_RESULT };

	if ( e == NULL ) {
		rs.sr_err = LDAP_PARAM_ERROR;
		goto cleanup;
	}
	
	pConn = slapiConnectionInit( NULL, LDAP_REQ_ADD );
	if ( pConn == NULL ) {
		rs.sr_err = LDAP_NO_MEMORY;
		goto cleanup;
	}

	if ( slapi_control_present( controls, LDAP_CONTROL_MANAGEDSAIT,
				NULL, &isCritical ) ) {
		manageDsaIt = 1; 
	}

	op = (Operation *)pConn->c_pending_ops.stqh_first;
	pPB = (Slapi_PBlock *)op->o_pb;
	op->o_ctrls = controls;

	op->o_bd = select_backend( &e->e_nname, manageDsaIt, 0 );
	if ( op->o_bd == NULL ) {
		rs.sr_err = LDAP_PARTIAL_RESULTS;
		goto cleanup;
	}

	op->o_dn = pConn->c_dn = op->o_bd->be_rootdn;
	op->o_ndn = pConn->c_ndn = op->o_bd->be_rootndn;
	op->oq_add.rs_e = e;

	if ( op->o_bd->be_add ) {
		int repl_user = be_isupdate( op->o_bd, &op->o_ndn );
		if ( !op->o_bd->be_update_ndn.bv_len || repl_user ){
			if ( (*op->o_bd->be_add)( op, &rs ) == 0 ) {
				if ( log_changes ) {
					replog( op );
				}
			}
		} else {
			rs.sr_err = LDAP_REFERRAL;
		}
	} else {
		rs.sr_err = LDAP_UNWILLING_TO_PERFORM;
	}

cleanup:

	if ( pPB != NULL ) {
		slapi_pblock_set( pPB, SLAPI_PLUGIN_INTOP_RESULT, (void *)rs.sr_err );
	}

	if ( pConn != NULL ) {
		pSavePB = pPB;
	}

	slapiConnectionDestroy( &pConn );

	return( pSavePB );
#else
	return NULL;
#endif /* LDAP_SLAPI */
}


Slapi_PBlock *
slapi_add_internal(
	char *dn, 
	LDAPMod **mods, 
	LDAPControl **controls, 
	int log_changes  ) 
{
#ifdef LDAP_SLAPI
	LDAPMod			*pMod = NULL;
	Slapi_PBlock		*pb = NULL;
	Entry			*pEntry = NULL;
	int			i, rc = LDAP_SUCCESS;

	if ( mods == NULL || *mods == NULL || dn == NULL || *dn == '\0' ) {
		rc = LDAP_PARAM_ERROR ;
	}

	if ( rc == LDAP_SUCCESS ) {
		for ( i = 0, pMod = mods[0]; pMod != NULL; pMod = mods[++i] ) {
			if ( (pMod->mod_op & ~LDAP_MOD_BVALUES) != LDAP_MOD_ADD ) {
				rc = LDAP_OTHER;
				break;
			}
		}
	}

	if ( rc == LDAP_SUCCESS ) {
		if((pEntry = LDAPModToEntry( dn, mods )) == NULL) {
			rc = LDAP_OTHER;
		}
	}

	if ( rc != LDAP_SUCCESS ) {
		pb = slapi_pblock_new();
		slapi_pblock_set( pb, SLAPI_PLUGIN_INTOP_RESULT, (void *)rc );
	} else {
		pb = slapi_add_entry_internal( pEntry, controls, log_changes );
	}

	if ( pEntry ) {
		slapi_entry_free(pEntry);
	}

	return(pb);
#else
	return NULL;
#endif /* LDAP_SLAPI */
}

/* Function : slapi_modrdn_internal
 *
 * Description : Plugin functions call this routine to modify the rdn 
 *				 of an entry in the backend directly
 * Return values : LDAP_SUCCESS
 *                 LDAP_PARAM_ERROR
 *                 LDAP_NO_MEMORY
 *                 LDAP_OTHER
 *                 LDAP_UNWILLING_TO_PERFORM
 *
 * NOTE: This function does not support the "newSuperior" option from LDAP V3.
 */
Slapi_PBlock *
slapi_modrdn_internal(
	char *olddn, 
	char *lnewrdn, 
	int deloldrdn, 
	LDAPControl **controls, 
	int log_change )
{
#ifdef LDAP_SLAPI
	struct berval		dn = { 0, NULL };
	struct berval		newrdn = { 0, NULL };
	Connection		*pConn = NULL;
	Operation		*op = NULL;
	Slapi_PBlock		*pPB = NULL;
	Slapi_PBlock		*pSavePB = NULL;
	int			manageDsaIt = 0;
	int			isCritical;
	SlapReply		rs = { REP_RESULT };

	pConn = slapiConnectionInit( NULL,  LDAP_REQ_MODRDN);
	if ( pConn == NULL) {
		rs.sr_err = LDAP_NO_MEMORY;
		goto cleanup;
	}

	op = (Operation *)pConn->c_pending_ops.stqh_first;
	pPB = (Slapi_PBlock *)op->o_pb;
	op->o_ctrls = controls;

	if ( slapi_control_present( controls, 
			SLAPI_CONTROL_MANAGEDSAIT_OID, NULL, &isCritical ) ) {
		manageDsaIt = 1;
	}

	op->o_bd = select_backend( &op->o_req_ndn, manageDsaIt, 0 );
	if ( op->o_bd == NULL ) {
		rs.sr_err =  LDAP_PARTIAL_RESULTS;
		goto cleanup;
	}

	op->o_dn = pConn->c_dn = op->o_bd->be_rootdn;
	op->o_ndn = pConn->c_ndn = op->o_bd->be_rootndn;

	dn.bv_val = slapi_ch_strdup( olddn );
	dn.bv_len = strlen( olddn );

	rs.sr_err = dnPrettyNormal( NULL, &dn, &op->o_req_dn, &op->o_req_ndn );
	if ( rs.sr_err != LDAP_SUCCESS ) {
		goto cleanup;
	}

	if ( op->o_req_dn.bv_len == 0 ) {
		rs.sr_err = LDAP_UNWILLING_TO_PERFORM;
		goto cleanup;
	}

	newrdn.bv_val = slapi_ch_strdup( lnewrdn );
	newrdn.bv_len = strlen( lnewrdn );

	rs.sr_err = dnPrettyNormal( NULL, &newrdn, &op->oq_modrdn.rs_newrdn, &op->oq_modrdn.rs_nnewrdn );
	if ( rs.sr_err != LDAP_SUCCESS ) {
		goto cleanup;
	}

	if ( rdnValidate( &op->oq_modrdn.rs_nnewrdn ) != LDAP_SUCCESS ) {
		goto cleanup;
	}

	op->oq_modrdn.rs_newSup = NULL;
	op->oq_modrdn.rs_nnewSup = NULL;
	op->oq_modrdn.rs_deleteoldrdn = deloldrdn;

	if ( op->o_bd->be_modrdn ) {
		int repl_user = be_isupdate( op->o_bd, &op->o_ndn );
		if ( !op->o_bd->be_update_ndn.bv_len || repl_user ) {
			if ( (*op->o_bd->be_modrdn)( op, &rs ) == 0 ) {
				if ( log_change ) {
					replog( op );
				}
			} else {
				rs.sr_err = LDAP_OTHER;
			}
		} else {
			rs.sr_err = LDAP_REFERRAL;
		}
	} else {
		rs.sr_err = LDAP_UNWILLING_TO_PERFORM;
	}

cleanup:

	if ( pPB != NULL ) {
		slapi_pblock_set( pPB, SLAPI_PLUGIN_INTOP_RESULT, (void *)rs.sr_err );
	}
	
	if ( dn.bv_val )
		slapi_ch_free( (void **)&dn.bv_val );

	if ( newrdn.bv_val )
		slapi_ch_free( (void **)&newrdn.bv_val );
	if ( op->oq_modrdn.rs_newrdn.bv_val )
		slapi_ch_free( (void **)&op->oq_modrdn.rs_newrdn.bv_val );
	if ( op->oq_modrdn.rs_nnewrdn.bv_val )
		slapi_ch_free( (void **)&op->oq_modrdn.rs_nnewrdn.bv_val );

	if ( pConn != NULL ) {
		pSavePB = pPB;
	}

	slapiConnectionDestroy( &pConn );

	return( pSavePB );
#else
	return NULL;
#endif /* LDAP_SLAPI */
}

/* Function : slapi_modify_internal
 *
 * Description:	Plugin functions call this routine to modify an entry 
 *				in the backend directly
 * Return values : LDAP_SUCCESS
 *                 LDAP_PARAM_ERROR
 *                 LDAP_NO_MEMORY
 *                 LDAP_OTHER
 *                 LDAP_UNWILLING_TO_PERFORM
*/
Slapi_PBlock *
slapi_modify_internal(
	char *ldn, 	
	LDAPMod **mods, 
	LDAPControl **controls, 
	int log_change )
{
#ifdef LDAP_SLAPI
	int			i;
	Connection		*pConn = NULL;
	Operation		*op = NULL;
	Slapi_PBlock		*pPB = NULL;
	Slapi_PBlock		*pSavePB = NULL;

	struct berval dn = { 0, NULL };

	int			manageDsaIt = 0;
	int			isCritical;
	struct berval		*bv;
	LDAPMod			*pMod;

	Modifications		*modlist = NULL;
	Modifications		**modtail = &modlist;
	Modifications		tmp;

	SlapReply		rs = { REP_RESULT };

	if ( mods == NULL || *mods == NULL || ldn == NULL ) {
		rs.sr_err = LDAP_PARAM_ERROR ;
		goto cleanup;
	}

	pConn = slapiConnectionInit( NULL,  LDAP_REQ_MODIFY );
	if ( pConn == NULL ) {
		rs.sr_err = LDAP_NO_MEMORY;
		goto cleanup;
	}

	op = (Operation *)pConn->c_pending_ops.stqh_first;
	pPB = (Slapi_PBlock *)op->o_pb;
	op->o_ctrls = controls;

	dn.bv_val = slapi_ch_strdup( ldn );
	dn.bv_len = strlen( ldn );
	rs.sr_err = dnPrettyNormal( NULL, &dn, &op->o_req_dn, &op->o_req_ndn );
	if ( rs.sr_err != LDAP_SUCCESS ) {
		goto cleanup;
	}

	if ( slapi_control_present( controls, 
			SLAPI_CONTROL_MANAGEDSAIT_OID, NULL, &isCritical ) ) {
        	manageDsaIt = 1;
	}

	op->o_bd = select_backend( &op->o_req_ndn, manageDsaIt, 0 );
	if ( op->o_bd == NULL ) {
		rs.sr_err = LDAP_PARTIAL_RESULTS;
		goto cleanup;
    	}

	op->o_dn = pConn->c_dn = op->o_bd->be_rootdn;
	op->o_ndn = pConn->c_ndn = op->o_bd->be_rootndn;

	for ( i = 0, pMod = mods[0];
		rs.sr_err == LDAP_SUCCESS && pMod != NULL; 
		pMod = mods[++i] )
	{
		Modifications *mod;

		if ( (pMod->mod_op & LDAP_MOD_BVALUES) != 0 ) {
			/*
			 * attr values are in berval format
			 * convert an array of pointers to bervals
			 * to an array of bervals
			 */
			rs.sr_err = bvptr2obj( pMod->mod_bvalues, &bv );
			if ( rs.sr_err != LDAP_SUCCESS )
				goto cleanup;
			tmp.sml_type.bv_val = pMod->mod_type;
			tmp.sml_type.bv_len = strlen( pMod->mod_type );
			tmp.sml_bvalues = bv;
			tmp.sml_nvalues = NULL;

			mod  = (Modifications *)ch_malloc( sizeof(Modifications) );

			mod->sml_op = pMod->mod_op;
			mod->sml_next = NULL;
			mod->sml_desc = NULL;
			mod->sml_type = tmp.sml_type;
			mod->sml_bvalues = tmp.sml_bvalues;
			mod->sml_nvalues = tmp.sml_nvalues;
		} else { 
			rs.sr_err = values2obj( pMod->mod_values, &bv );
			if ( rs.sr_err != LDAP_SUCCESS )
				goto cleanup;
			tmp.sml_type.bv_val = pMod->mod_type;
			tmp.sml_type.bv_len = strlen( pMod->mod_type );
			tmp.sml_bvalues = bv;
			tmp.sml_nvalues = NULL;

			mod  = (Modifications *) ch_malloc( sizeof(Modifications) );

			mod->sml_op = pMod->mod_op;
			mod->sml_next = NULL;
			mod->sml_desc = NULL;
			mod->sml_type = tmp.sml_type;
			mod->sml_bvalues = tmp.sml_bvalues;
			mod->sml_nvalues = tmp.sml_nvalues;
		}
		*modtail = mod;
		modtail = &mod->sml_next;

		switch( pMod->mod_op ) {
		case LDAP_MOD_ADD:
		if ( mod->sml_bvalues == NULL ) {
			rs.sr_err = LDAP_PROTOCOL_ERROR;
			goto cleanup;
		}

		/* fall through */
		case LDAP_MOD_DELETE:
		case LDAP_MOD_REPLACE:
		break;

		default:
			rs.sr_err = LDAP_PROTOCOL_ERROR;
			goto cleanup;
		}
	} 
	*modtail = NULL;

	if ( op->o_req_ndn.bv_len == 0 ) {
		rs.sr_err = LDAP_UNWILLING_TO_PERFORM;
		goto cleanup;
	}

	op->oq_modify.rs_modlist = modlist;

	if ( op->o_bd->be_modify ) {
		int repl_user = be_isupdate( op->o_bd, &op->o_ndn );
		if ( !op->o_bd->be_update_ndn.bv_len || repl_user ) {
			int update = op->o_bd->be_update_ndn.bv_len;
			const char *text = NULL;
			char textbuf[SLAP_TEXT_BUFLEN];
			size_t textlen = sizeof( textbuf );

			rs.sr_err = slap_mods_check( modlist, update,
					&text, textbuf, textlen );
			if ( rs.sr_err != LDAP_SUCCESS ) {
				goto cleanup;
			}

			if ( !repl_user ) {
				rs.sr_err = slap_mods_opattrs( op, modlist,
						modtail, &text, textbuf, 
						textlen );
				if ( rs.sr_err != LDAP_SUCCESS ) {
					goto cleanup;
				}
			}
			if ( (*op->o_bd->be_modify)( op, &rs ) == 0 ) {
				if ( log_change ) {
					replog( op );
				}
			} else {
				rs.sr_err = LDAP_OTHER;
			}
		} else {
			rs.sr_err = LDAP_REFERRAL;
		}
	} else {
		rs.sr_err = LDAP_UNWILLING_TO_PERFORM;
	}

cleanup:

	if ( pPB != NULL ) 
		slapi_pblock_set( pPB, SLAPI_PLUGIN_INTOP_RESULT, (void *)rs.sr_err );

	if ( dn.bv_val )
		slapi_ch_free( (void **)&dn.bv_val );

	if ( modlist != NULL )
		freeMods( modlist );

	if ( pConn != NULL ) {
		pSavePB = pPB;
	}

	slapiConnectionDestroy( &pConn );

	return ( pSavePB );
#else
	return NULL;
#endif /* LDAP_SLAPI */
}

Slapi_PBlock *
slapi_search_internal_bind(
	char *bindDN, 
	char *ldn, 
	int scope, 
	char *filStr, 
	LDAPControl **controls, 
	char **attrs, 
	int attrsonly ) 
{	
#ifdef LDAP_SLAPI
	Connection		*c;
	Operation		*op = NULL;
	Slapi_PBlock		*ptr = NULL;		
	Slapi_PBlock		*pSavePB = NULL;		
	struct berval		dn = { 0, NULL };
	Filter			*filter=NULL;
	struct berval		fstr = { 0, NULL };
	AttributeName		*an = NULL;
	const char		*text = NULL;

	int			manageDsaIt = 0; 
	int			isCritical;
	int			i;

	SlapReply		rs = { REP_RESULT };

	c = slapiConnectionInit( NULL, LDAP_REQ_SEARCH );
	if ( c == NULL ) {
		rs.sr_err = LDAP_NO_MEMORY;
		goto cleanup;
	}

	op = (Operation *)c->c_pending_ops.stqh_first;
	ptr = (Slapi_PBlock *)op->o_pb;
	op->o_ctrls = controls;

	dn.bv_val = slapi_ch_strdup(ldn);
	dn.bv_len = strlen(ldn);

	rs.sr_err = dnPrettyNormal( NULL, &dn, &op->o_req_dn, &op->o_req_ndn );
	if ( rs.sr_err != LDAP_SUCCESS ) {
		goto cleanup;
	}

	if ( scope != LDAP_SCOPE_BASE && 
		 	scope != LDAP_SCOPE_ONELEVEL && 
		 	scope != LDAP_SCOPE_SUBTREE ) {
		rs.sr_err = LDAP_PROTOCOL_ERROR;
		goto cleanup;
	}

	filter = slapi_str2filter(filStr);
	if ( filter == NULL ) {
		rs.sr_err = LDAP_PROTOCOL_ERROR;
		goto cleanup;
	}

	filter2bv( filter, &fstr );

	for ( i = 0; attrs != NULL && attrs[i] != NULL; i++ ) {
		; /* count the number of attributes */
	}

	if (i > 0) {
		an = (AttributeName *)slapi_ch_calloc( (i + 1), sizeof(AttributeName) );
		for (i = 0; attrs[i] != 0; i++) {
			an[i].an_desc = NULL;
			an[i].an_oc = NULL;
			an[i].an_name.bv_val = slapi_ch_strdup(attrs[i]);
			an[i].an_name.bv_len = strlen(attrs[i]);
			slap_bv2ad( &an[i].an_name, &an[i].an_desc, &text );
		}
		an[i].an_name.bv_val = NULL;
	}

	memset( &rs, 0, sizeof(rs) );
	rs.sr_type = REP_RESULT;
	rs.sr_err = LDAP_SUCCESS;
	rs.sr_entry = NULL; /* paranoia */

	if ( scope == LDAP_SCOPE_BASE ) {
		rs.sr_entry = NULL;

		if ( op->o_req_ndn.bv_len == 0 ) {
			rs.sr_err = root_dse_info( c, &rs.sr_entry, &rs.sr_text );
		}

		if( rs.sr_err != LDAP_SUCCESS ) {
			send_ldap_result( op, &rs );
			goto cleanup;
		} else if ( rs.sr_entry != NULL ) {
			rs.sr_err = test_filter( op, rs.sr_entry, filter );

			if ( rs.sr_err == LDAP_COMPARE_TRUE ) {
				rs.sr_type = REP_SEARCH;
				rs.sr_err = LDAP_SUCCESS;
				rs.sr_attrs = an;

				send_search_entry( op, &rs );
            		}

			entry_free( rs.sr_entry );

			rs.sr_type = REP_RESULT;
			rs.sr_err = LDAP_SUCCESS;

			send_ldap_result( op, &rs );

			goto cleanup;
		}
	}

	if ( !op->o_req_ndn.bv_len && default_search_nbase.bv_len ) {
		slapi_ch_free( (void **)op->o_req_dn.bv_val );
		slapi_ch_free( (void **)op->o_req_ndn.bv_val );

		ber_dupbv( &op->o_req_dn, &default_search_base );
		ber_dupbv( &op->o_req_ndn, &default_search_nbase );
	}

	if ( slapi_control_present( controls,
			LDAP_CONTROL_MANAGEDSAIT, NULL, &isCritical ) ) {
		manageDsaIt = 1;
	}

	op->o_bd = select_backend( &op->o_req_ndn, manageDsaIt, 0 );
	if ( op->o_bd == NULL ) {
		if ( manageDsaIt == 1 ) {
			rs.sr_err = LDAP_NO_SUCH_OBJECT;
		} else {
			rs.sr_err = LDAP_PARTIAL_RESULTS;
		}
		goto cleanup;
	} 

	op->o_dn = c->c_dn = op->o_bd->be_rootdn;
	op->o_ndn = c->c_ndn = op->o_bd->be_rootndn;

	op->oq_search.rs_scope = scope;
	op->oq_search.rs_deref = 0;
	op->oq_search.rs_slimit = LDAP_NO_LIMIT;
	op->oq_search.rs_tlimit = LDAP_NO_LIMIT;
	op->oq_search.rs_attrsonly = attrsonly;
	op->oq_search.rs_attrs = an;
	op->oq_search.rs_filter = filter;
	op->oq_search.rs_filterstr = fstr;

	if ( op->o_bd->be_search ) {
		if ( (*op->o_bd->be_search)( op, &rs ) != 0 ) {
			rs.sr_err = LDAP_OTHER;
		}
	} else {
		rs.sr_err = LDAP_UNWILLING_TO_PERFORM;
	}

cleanup:

	if ( ptr != NULL )
		slapi_pblock_set( ptr, SLAPI_PLUGIN_INTOP_RESULT, (void *)rs.sr_err );

	if ( dn.bv_val )
		slapi_ch_free( (void **)&dn.bv_val );
	if ( filter )
		slapi_filter_free( filter, 1 );
	if ( fstr.bv_val )
		slapi_ch_free( (void **)&fstr.bv_val );
	if ( an != NULL )
		slapi_ch_free( (void **)&an );

	if ( c != NULL ) {
		pSavePB = ptr;
    	}

	slapiConnectionDestroy( &c );

	return( pSavePB );
#else
	return NULL;
#endif /* LDAP_SLAPI */
}

Slapi_PBlock * 
slapi_search_internal(
	char *base,
	int scope,
	char *filStr, 
	LDAPControl **controls,
	char **attrs,
	int attrsonly ) 
{
#ifdef LDAP_SLAPI
	return slapi_search_internal_bind( NULL, base, scope, filStr,
			controls, attrs, attrsonly );
#else
	return NULL;
#endif /* LDAP_SLAPI */
}

