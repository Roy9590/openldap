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

#include "portable.h"
#include "slapi_common.h"
#include <ldap_pvt_thread.h>
#include <slap.h>
#include <slapi.h>

/*
 * Note: if ltdl.h is not available, slapi should not be compiled
 */
#include <ltdl.h>

static int loadPlugin( Slapi_PBlock *, const char *, const char *, int, 
	SLAPI_FUNC *, lt_dlhandle * );

/* pointer to link list of extended objects */
static ExtendedOp *pGExtendedOps = NULL;

/*********************************************************************
 * Function Name:      newPlugin
 *
 * Description:        This routine creates a new Slapi_PBlock structure,
 *                     loads in the plugin module and executes the init
 *                     function provided by the module.
 *
 * Input:              type - type of the plugin, such as SASL, database, etc.
 *                     path - the loadpath to load the module in
 *                     initfunc - name of the plugin function to execute first
 *                     argc - number of arguements
 *                     argv[] - an array of char pointers point to
 *                              the arguments passed in via
 *                              the configuration file.
 *
 * Output:             
 *
 * Return Values:      a pointer to a newly created Slapi_PBlock structrue or
 *                     NULL - function failed 
 *
 * Messages:           None
 *********************************************************************/

Slapi_PBlock *
newPlugin(
	int type, 
	const char *path, 
	const char *initfunc, 
	int argc, 
	char *argv[] ) 
{
	Slapi_PBlock	*pPlugin = NULL; 
	lt_dlhandle	hdLoadHandle;
	int		rc;

	pPlugin = slapi_pblock_new();
	if ( pPlugin == NULL ) {
		rc = LDAP_NO_MEMORY;
		goto done;
	}

	rc = slapi_pblock_set( pPlugin, SLAPI_PLUGIN_TYPE, (void *)type );
	if ( rc != LDAP_SUCCESS ) {
		goto done;
	}

	rc = slapi_pblock_set( pPlugin, SLAPI_PLUGIN_ARGC, (void *)argc );
	if ( rc != LDAP_SUCCESS ) {
		goto done;
	}

	rc = slapi_pblock_set( pPlugin, SLAPI_PLUGIN_ARGV, (void *)argv );
	if ( rc != LDAP_SUCCESS ) { 
		goto done;
	}

	rc = loadPlugin( pPlugin, path, initfunc, TRUE, NULL, &hdLoadHandle );

done:
	if ( rc != LDAP_SUCCESS && pPlugin != NULL ) {
		slapi_pblock_destroy( pPlugin );
		pPlugin = NULL;
	}

	return pPlugin;
} 

/*********************************************************************
 * Function Name:      insertPlugin
 *
 * Description:        insert the slapi_pblock structure to the end of the plugin
 *                     list 
 *
 * Input:              a pointer to a plugin slapi_pblock structure to be added to 
 *                     the list
 *
 * Output:             none
 *
 * Return Values:      LDAP_SUCCESS - successfully inserted.
 *                     LDAP_LOCAL_ERROR.
 *
 * Messages:           None
 *********************************************************************/
int 
insertPlugin(
	Backend *be, 
	Slapi_PBlock *pPB )
{ 
	Slapi_PBlock *pTmpPB;
	Slapi_PBlock *pSavePB;
	int    rc = LDAP_SUCCESS;

	pTmpPB = (Slapi_PBlock *)(be->be_pb);
       
	if ( pTmpPB == NULL ) {
		be->be_pb = (void *)pPB;
	} else {
		while ( pTmpPB != NULL && rc == LDAP_SUCCESS ) {
			pSavePB = pTmpPB;
			rc = slapi_pblock_get( pTmpPB, SLAPI_IBM_PBLOCK,
					&pTmpPB );
			if ( rc != LDAP_SUCCESS ) {
				rc = LDAP_OTHER;
			}
		}

		if ( rc == LDAP_SUCCESS ) { 
			rc = slapi_pblock_set( pSavePB, SLAPI_IBM_PBLOCK,
					(void *)pPB ); 
			if ( rc != LDAP_SUCCESS ) {
				rc = LDAP_OTHER;
			}
		}
	}
     
	return rc;
}
       
/*********************************************************************
 * Function Name:      getAllPluginFuncs
 *
 * Description:        get the desired type of function pointers defined 
 *                     in all the plugins 
 *
 * Input:              the type of the functions to get, such as pre-operation,etc.
 *
 * Output:             none
 *
 * Return Values:      this routine returns a pointer to an array of function
 *                     pointers
 *
 * Messages:           None
 *********************************************************************/
int 
getAllPluginFuncs(
	Backend *be, 		
	int functype, 
	SLAPI_FUNC **ppFuncPtrs )
{
 
	Slapi_PBlock	*pCurrentPB; 
	SLAPI_FUNC	FuncPtr;
	SLAPI_FUNC	*pTmpFuncPtr;
	int		numPB = 0;
	int		rc = LDAP_SUCCESS;

	if ( be == NULL ) {
		/*
		 * No plugins supported if no backend (yet)
		 */
		rc = LDAP_OTHER;
		goto done;
	}

	assert( ppFuncPtrs );

	pCurrentPB = (Slapi_PBlock *)(be->be_pb);
     
	if ( pCurrentPB == NULL ) { 
		/*
		 * LDAP_OTHER is returned if no plugins are installed
		 */
		rc = LDAP_OTHER;
		goto done;
	}

	while ( pCurrentPB != NULL && rc == LDAP_SUCCESS ) {
		rc = slapi_pblock_get( pCurrentPB, functype, &FuncPtr );
		if ( rc == LDAP_SUCCESS ) {
			if ( FuncPtr != NULL )  {
				numPB++;
			}
			rc = slapi_pblock_get( pCurrentPB,
					SLAPI_IBM_PBLOCK, &pCurrentPB );
		}
	}

	if ( rc != LDAP_SUCCESS ) {
		goto done;
	}

	if ( numPB == 0 ) {
		*ppFuncPtrs = NULL;
		rc = LDAP_SUCCESS;
		goto done;
	}

	*ppFuncPtrs = pTmpFuncPtr = 
		(SLAPI_FUNC *)ch_malloc( ( numPB + 1 ) * sizeof(SLAPI_FUNC) ); 
	if ( ppFuncPtrs == NULL ) {
		rc = LDAP_NO_MEMORY;
		goto done;
	}

	pCurrentPB = (Slapi_PBlock *)(be->be_pb);
	while ( pCurrentPB != NULL && rc == LDAP_SUCCESS )  {
		rc = slapi_pblock_get( pCurrentPB, functype, &FuncPtr );
		if ( rc == LDAP_SUCCESS ) {
			if ( FuncPtr != NULL )  {
				*pTmpFuncPtr = FuncPtr;
				pTmpFuncPtr++;
			} 
			rc = slapi_pblock_get( pCurrentPB,
					SLAPI_IBM_PBLOCK, &pCurrentPB );
		}
	}
	*pTmpFuncPtr = NULL ;

done:
	if ( rc != LDAP_SUCCESS && *ppFuncPtrs != NULL ) {
		ch_free( *ppFuncPtrs );
		*ppFuncPtrs = NULL;
	}

	return rc;
}
              
/*********************************************************************
 * Function Name:      createExtendedOp
 *
 * Description: Creates an extended operation structure and
 *              initializes the fields
 *
 * Return value: A newly allocated structure or NULL
 ********************************************************************/
ExtendedOp *
createExtendedOp()
{
	ExtendedOp *ret;

	ret = (ExtendedOp *)ch_malloc(sizeof(ExtendedOp));
	if ( ret != NULL ) {
		ret->ext_oid.bv_val = NULL;
		ret->ext_oid.bv_len = 0;
		ret->ext_func = NULL;
		ret->ext_be = NULL;
		ret->ext_next = NULL;
	}

	return ret;
}


/*********************************************************************
 * Function Name:      removeExtendedOp
 *
 * Description:        This routine removes the ExtendedOp structures 
 *					   asscoiated with a particular extended operation 
 *					   plugin.
 *
 * Input:              pBE - pointer to a backend structure
 *                     opList - pointer to a linked list of extended
 *                              operation structures
 *                     pPB - pointer to a slapi parameter block
 *
 * Output:
 *
 * Return Value:       none
 *
 * Messages:           None
 *********************************************************************/
void
removeExtendedOp(
	Backend *pBE, 
	ExtendedOp **opList, 
	Slapi_PBlock *pPB )
{
	ExtendedOp	*pTmpExtOp, *backExtOp;
	char		**pTmpOIDs;
	int		i;

#if 0
	assert( pBE != NULL); /* unused */
#endif /* 0 */
	assert( opList != NULL );
	assert( pPB != NULL );

	if ( *opList == NULL ) {
		return;
	}

	slapi_pblock_get( pPB, SLAPI_PLUGIN_EXT_OP_OIDLIST, &pTmpOIDs );
	if ( pTmpOIDs == NULL ) {
		return;
	}

	for ( i = 0; pTmpOIDs[i] != NULL; i++ ) {
		backExtOp = NULL;
		pTmpExtOp = *opList;
		for ( ; pTmpExtOp != NULL; pTmpExtOp = pTmpExtOp->ext_next) {
			int	rc;
			rc = strcasecmp( pTmpExtOp->ext_oid.bv_val,
					pTmpOIDs[ i ] );
			if ( rc == 0 ) {
				if ( backExtOp == NULL ) {
					*opList = pTmpExtOp->ext_next;
				} else {
					backExtOp->ext_next
						= pTmpExtOp->ext_next;
				}

				ch_free( pTmpExtOp );
				break;
			}
			backExtOp = pTmpExtOp;
		}
	}
}


/*********************************************************************
 * Function Name:      newExtendedOp
 *
 * Description:        This routine creates a new ExtendedOp structure, loads
 *                     in the extended op module and put the extended op function address
 *                     in the structure. The function will not be executed in
 *                     this routine.
 *
 * Input:              pBE - pointer to a backend structure
 *                     opList - pointer to a linked list of extended
 *                              operation structures
 *                     pPB - pointer to a slapi parameter block
 *
 * Output:
 *
 * Return Value:       an LDAP return code
 *
 * Messages:           None
 *********************************************************************/
int 
newExtendedOp(
	Backend *pBE, 	
	ExtendedOp **opList, 
	Slapi_PBlock *pPB )
{
	ExtendedOp	*pTmpExtOp = NULL;
	SLAPI_FUNC	tmpFunc;
	char		**pTmpOIDs;
	int		rc = LDAP_OTHER;
	int		i;

	if ( (*opList) == NULL ) { 
		*opList = createExtendedOp();
		if ( (*opList) == NULL ) {
			rc = LDAP_NO_MEMORY;
			goto error_return;
		}
		pTmpExtOp = *opList;
		
	} else {                        /* Find the end of the list */
		for ( pTmpExtOp = *opList; pTmpExtOp->ext_next != NULL;
				pTmpExtOp = pTmpExtOp->ext_next )
			; /* EMPTY */
		pTmpExtOp->ext_next = createExtendedOp();
		if ( pTmpExtOp->ext_next == NULL ) {
			rc = LDAP_NO_MEMORY;
			goto error_return;
		}
		pTmpExtOp = pTmpExtOp->ext_next;
	}

	rc = slapi_pblock_get( pPB,SLAPI_PLUGIN_EXT_OP_OIDLIST, &pTmpOIDs );
	if ( rc != LDAP_SUCCESS ) {
		rc = LDAP_OTHER;
		goto error_return;
	}

	rc = slapi_pblock_get(pPB,SLAPI_PLUGIN_EXT_OP_FN, &tmpFunc);
	if ( rc != 0 ) {
		rc = LDAP_OTHER;
		goto error_return;
	}

	if ( (pTmpOIDs == NULL) || (tmpFunc == NULL) ) {
		rc = LDAP_OTHER;
		goto error_return;
	}

	for ( i = 0; pTmpOIDs[i] != NULL; i++ ) {
		pTmpExtOp->ext_oid.bv_val = pTmpOIDs[i];
		pTmpExtOp->ext_oid.bv_len = strlen( pTmpOIDs[i] );
		pTmpExtOp->ext_func = tmpFunc;
		pTmpExtOp->ext_be = pBE;
		if ( pTmpOIDs[i + 1] != NULL ) {
			pTmpExtOp->ext_next = createExtendedOp();
			if ( pTmpExtOp->ext_next == NULL ) {
				rc = LDAP_NO_MEMORY;
				break;
			}
			pTmpExtOp = pTmpExtOp->ext_next;
		}
	}

error_return:
	return rc;
}

/*********************************************************************
 * Function Name:      getPluginFunc
 *
 * Description:        This routine gets the function address for a given function
 *                     name.
 *
 * Input:
 *                     funcName - name of the extended op function, ie. an OID.
 *
 * Output:             pFuncAddr - the function address of the requested function name.
 *
 * Return Values:      a pointer to a newly created ExtendOp structrue or
 *                     NULL - function failed
 *
 * Messages:           None
 *********************************************************************/
int 
getPluginFunc(
	struct berval *reqoid, 		
	SLAPI_FUNC *pFuncAddr ) 
{
	ExtendedOp	*pTmpExtOp;

	assert( reqoid != NULL );
	assert( pFuncAddr != NULL );

	*pFuncAddr = NULL;

	if ( pGExtendedOps == NULL ) {
		return LDAP_OTHER;
	}

	pTmpExtOp = pGExtendedOps;
	while ( pTmpExtOp != NULL ) {
		int	rc;
		
		rc = strcasecmp( reqoid->bv_val, pTmpExtOp->ext_oid.bv_val );
		if ( rc == 0 ) {
			*pFuncAddr = pTmpExtOp->ext_func;
			break;
		}
		pTmpExtOp = pTmpExtOp->ext_next;
	}

	return ( *pFuncAddr == NULL ? 1 : 0 );
}

/***************************************************************************
 * This function is similar to getPluginFunc above. except it returns one OID
 * per call. It is called from root_dse_info (root_dse.c).
 * The function is a modified version of get_supported_extop (file extended.c).
 ***************************************************************************/
struct berval *
ns_get_supported_extop( int index )
{
        ExtendedOp	*ext;

        for ( ext = pGExtendedOps ; ext != NULL && --index >= 0;
			ext = ext->ext_next) {
                ; /* empty */
        }

        if ( ext == NULL ) {
		return NULL;
	}

        return &ext->ext_oid ;
}

/*********************************************************************
 * Function Name:      loadPlugin
 *
 * Description:        This routine loads the specified DLL, gets and executes the init function
 *                     if requested.
 *
 * Input:
 *                     pPlugin - a pointer to a Slapi_PBlock struct which will be passed to
 *                               the DLL init function.
 *                     path - path name of the DLL to be load.
 *                     initfunc - either the DLL initialization function or an OID of the
 *                                loaded extended operation.
 *                     doInit - if it is TRUE, execute the init function, otherwise, save the
 *                              function address but not execute it.
 *
 * Output:             pInitFunc - the function address of the loaded function. This param
 *                                 should be not be null if doInit is FALSE.
 *                     pLdHandle - handle returned by lt_dlopen()
 *
 * Return Values:      LDAP_SUCCESS, LDAP_LOCAL_ERROR
 *
 * Messages:           None
 *********************************************************************/

static int 
loadPlugin(
	Slapi_PBlock	*pPlugin,
	const char	*path,
	const char	*initfunc, 
	int		doInit,
	SLAPI_FUNC	*pInitFunc,
	lt_dlhandle	*pLdHandle ) 
{
	int		rc = LDAP_SUCCESS;
	SLAPI_FUNC	fpInitFunc = NULL;

	assert( pLdHandle );

	if ( lt_dlinit() ) {
		return LDAP_LOCAL_ERROR;
	}

	/* load in the module */
	*pLdHandle = lt_dlopen( path );
	if ( *pLdHandle == NULL ) {
		return LDAP_LOCAL_ERROR;
	}

	fpInitFunc = (SLAPI_FUNC)lt_dlsym( *pLdHandle, initfunc );
	if ( fpInitFunc == NULL ) {
		lt_dlclose( *pLdHandle );
		return LDAP_LOCAL_ERROR;
	}

	if ( doInit == TRUE ) {
		rc = ( *fpInitFunc )( pPlugin );
		if ( rc != LDAP_SUCCESS ) {
			lt_dlclose( *pLdHandle );
		}

	} else {
		*pInitFunc = fpInitFunc;
	}

	return rc;
}


int 
doPluginFNs(
	Backend		*be, 	
	int		funcType, 
	Slapi_PBlock	*pPB )
{

	int rc = LDAP_SUCCESS;
	SLAPI_FUNC *pGetPlugin = NULL, *tmpPlugin = NULL; 

	rc = getAllPluginFuncs(be, funcType, &tmpPlugin );
	if ( rc != LDAP_SUCCESS || tmpPlugin == NULL ) {
		return rc;
	}

	for ( pGetPlugin = tmpPlugin ; *pGetPlugin != NULL; pGetPlugin++ ) {
		/*
		 * FIXME: we should provide here a sort of sandbox,
		 * to protect from plugin faults; e.g. trap signals
		 * and longjump here, marking the plugin as unsafe for
		 * later executions ...
		 */
		rc = (*pGetPlugin)(pPB);

		/*
		 * Only non-postoperation plugins abort processing on
		 * failure (confirmed with SLAPI specification).
		 */
		if ( !SLAPI_PLUGIN_IS_POST_FN( funcType ) && rc != 0 ) {
			break;
		}
	}

	ch_free( tmpPlugin );

	return rc;
}

int
netscape_plugin(
	Backend		*be, 		
	const char	*fname, 
	int		lineno, 
	int		argc, 
	char		**argv )
{
	int		iType = -1;
	int		numPluginArgc = 0;
	char		**ppPluginArgv = NULL;

	if ( argc < 4 ) {
		fprintf( stderr,
			"%s: line %d: missing arguments "
			"in \"plugin <plugin_type> <lib_path> "
			"<init_function> [<arguments>]\" line\n",
			fname, lineno );
		return 1;
	}
	
	if ( strcasecmp( argv[1], "preoperation" ) == 0 ) {
		iType = SLAPI_PLUGIN_PREOPERATION;
	} else if ( strcasecmp( argv[1], "postoperation" ) == 0 ) {
		iType = SLAPI_PLUGIN_POSTOPERATION;
	} else if ( strcasecmp( argv[1], "extendedop" ) == 0 ) {
		iType = SLAPI_PLUGIN_EXTENDEDOP;
	} else if ( strcasecmp( argv[1], "opattrsp" ) == 0 ) {
		iType = SLAPI_PLUGIN_OPATTR_SP;
	} else {
		fprintf( stderr, "%s: line %d: invalid plugin type \"%s\".\n",
				fname, lineno, argv[1] );
		return 1;
	}
	
	numPluginArgc = argc - 4;
	if ( numPluginArgc > 0 ) {
		ppPluginArgv = &argv[4];
	} else {
		ppPluginArgv = NULL;
	}

	if ( iType == SLAPI_PLUGIN_PREOPERATION ||
		  	iType == SLAPI_PLUGIN_EXTENDEDOP ||
			iType == SLAPI_PLUGIN_POSTOPERATION ||
			iType == SLAPI_PLUGIN_OPATTR_SP ) {
		int rc;
		Slapi_PBlock *pPlugin;

		pPlugin = newPlugin( iType, argv[2], argv[3], 
					numPluginArgc, ppPluginArgv );
		if (pPlugin == NULL) {
			return 1;
		}

		if (iType == SLAPI_PLUGIN_EXTENDEDOP) {
			rc = newExtendedOp(be, &pGExtendedOps, pPlugin);
			if ( rc != LDAP_SUCCESS ) {
				slapi_pblock_destroy( pPlugin );
				return 1;
			}
		}

		rc = insertPlugin( be, pPlugin );
		if ( rc != LDAP_SUCCESS ) {
			if ( iType == SLAPI_PLUGIN_EXTENDEDOP ) {
				removeExtendedOp( be, &pGExtendedOps, pPlugin );
			}
			slapi_pblock_destroy( pPlugin );
			return 1;
		}
	}

	return 0;
}

int
slapi_init(void)
{
	if ( ldap_pvt_thread_mutex_init( &slapi_hn_mutex ) ) {
		return -1;
	}
	
	if ( ldap_pvt_thread_mutex_init( &slapi_time_mutex ) ) {
		return -1;
	}

	if ( ldap_pvt_thread_mutex_init( &slapi_printmessage_mutex ) ) {
		return -1;
	}

	slapi_log_file = ch_strdup( LDAP_RUNDIR LDAP_DIRSEP "errors" );
	if ( slapi_log_file == NULL ) {
		return -1;
	}

	return 0;
}

