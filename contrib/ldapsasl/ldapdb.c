/* SASL LDAP auxprop implementation
 * Copyright (C) 2002 Howard Chu, hyc@symas.com
 */

#include <config.h>

#include <stdio.h>

#include "sasl.h"
#include "saslutil.h"
#include "saslplug.h"

#include "plugin_common.h"

#include <ldap.h>

static char ldapdb[] = "ldapdb";

SASL_AUXPROP_PLUG_INIT( ldapdb )

typedef struct ldapctx {
	const char *uri;	/* URI of LDAP server */
	const char *id;	/* SASL authcid to bind as */
	const char *pw;	/* password for bind */
	const char *mech;	/* SASL mech */
} ldapctx;

typedef struct gluectx {
	ldapctx *lc;
	sasl_server_params_t *lp;
	const char *user;
} gluectx;

static int ldapdb_interact(LDAP *ld, unsigned flags __attribute__((unused)),
	void *def, void *inter)
{
	sasl_interact_t *in = inter;
	gluectx *gc = def;
	const char *p;

	for (;in->id != SASL_CB_LIST_END;in++)
	{
		p = NULL;
		switch(in->id)
		{
			case SASL_CB_GETREALM:
				ldap_get_option(ld, LDAP_OPT_X_SASL_REALM, &p);
				break;		
			case SASL_CB_AUTHNAME:
				p = gc->lc->id;
				break;
			case SASL_CB_PASS:
				p = gc->lc->pw;
				break;
			case SASL_CB_USER:
				p = gc->user;
				break;
		}
		if (p)
		{
			int l = strlen(p);
			in->result = gc->lp->utils->malloc(l+1);
			if (!in->result)
				return LDAP_NO_MEMORY;
			strcpy((char *)in->result, p);
			in->len = l;
		}
	}
	return LDAP_SUCCESS;
}

static void ldapdb_auxprop_lookup(void *glob_context,
				  sasl_server_params_t *sparams,
				  unsigned flags,
				  const char *user,
				  unsigned ulen)
{
    ldapctx *ctx = glob_context;
    int ret, i, n, *aindx;
    const struct propval *pr;
    LDAP *ld = NULL;
    gluectx gc = { ctx, sparams, NULL };
    struct berval *dn = NULL, **bvals;
    LDAPMessage *msg, *res;
    char **attrs = NULL, *authzid = NULL;
    
    if(!ctx || !sparams || !user) return;

    pr = sparams->utils->prop_get(sparams->propctx);
    if(!pr) return;

    /* count how many attrs to fetch */
    for(i = 0, n = 0; pr[i].name; i++) {
	if(pr[i].name[0] == '*' && (flags & SASL_AUXPROP_AUTHZID))
	    continue;
	if(pr[i].values && !(flags & SASL_AUXPROP_OVERRIDE))
	    continue;
	n++;
    }
    /* nothing to do, bail out */
    if (!n) return;

    /* alloc an array of attr names for search, and index to the props */
    attrs = sparams->utils->malloc((n+1)*sizeof(char *)*2);
    if (!attrs) return;

    aindx = (int *)(attrs + n + 1);

    /* copy attr list */
    for (i=0, n=0; pr[i].name; i++) {
	if(pr[i].name[0] == '*' && (flags & SASL_AUXPROP_AUTHZID))
	    continue;
	if(pr[i].values && !(flags & SASL_AUXPROP_OVERRIDE))
	    continue;
    	attrs[n] = (char *)pr[i].name;
	if (pr[i].name[0] == '*') attrs[n]++;
	aindx[n] = i;
	n++;
    }
    attrs[n] = NULL;
    	
    if(ldap_initialize(&ld, ctx->uri)) {
    	sparams->utils->free(attrs);
    	return;
    }

    authzid = sparams->utils->malloc(ulen + sizeof("u:"));
    if (!authzid) goto done;
    strcpy(authzid, "u:");
    strcpy(authzid+2, user);
    gc.user = authzid;

    i = LDAP_VERSION3;
    ret = ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &i);

    ret = ldap_sasl_interactive_bind_s(ld, NULL, ctx->mech, NULL, NULL,
    	LDAP_SASL_QUIET, ldapdb_interact, &gc);
    if (ret != LDAP_SUCCESS) goto done;
    
    ret = ldap_extended_operation_s(ld, LDAP_EXOP_X_WHO_AM_I, NULL, NULL,
    	NULL, NULL, &dn);
    if (ret != LDAP_SUCCESS || !dn) goto done;
    
    if (dn->bv_val && !strncmp(dn->bv_val, "dn:", 3))
    ret = ldap_search_s(ld, dn->bv_val+3, LDAP_SCOPE_BASE, "(objectclass=*)",
    	attrs, 0, &res);
    ber_bvfree(dn);

    if (ret != LDAP_SUCCESS) goto done;

    for(msg=ldap_first_message(ld, res); msg; msg=ldap_next_message(ld, msg))
    {
    	if (ldap_msgtype(msg) != LDAP_RES_SEARCH_ENTRY) continue;
	for (i=0; i<n; i++)
	{
	    bvals = ldap_get_values_len(ld, msg, attrs[i]);
	    if (!bvals) continue;
	    if (pr[aindx[i]].values)
	    	sparams->utils->prop_erase(sparams->propctx, pr[aindx[i]].name);
	    sparams->utils->prop_set(sparams->propctx, pr[aindx[i]].name,
				 bvals[0]->bv_val, bvals[0]->bv_len);
	    ber_bvecfree(bvals);
	}
    }
    ldap_msgfree(res);

 done:
    if(authzid) sparams->utils->free(authzid);
    if(attrs) sparams->utils->free(attrs);
    if(ld) ldap_unbind(ld);
}

static void ldapdb_auxprop_free(void *glob_ctx, const sasl_utils_t *utils)
{
	utils->free(glob_ctx);
}

static sasl_auxprop_plug_t ldapdb_auxprop_plugin = {
    0,           /* Features */
    0,           /* spare */
    NULL,        /* glob_context */
    ldapdb_auxprop_free,	/* auxprop_free */
    ldapdb_auxprop_lookup, /* auxprop_lookup */
    ldapdb,    /* name */
    NULL         /* spare */
};

int ldapdb_auxprop_plug_init(const sasl_utils_t *utils,
                             int max_version,
                             int *out_version,
                             sasl_auxprop_plug_t **plug,
                             const char *plugname __attribute__((unused))) 
{
    ldapctx tmp, *p;
    const char *s;

    if(!out_version || !plug) return SASL_BADPARAM;

    if(max_version < SASL_AUXPROP_PLUG_VERSION) return SASL_BADVERS;
    
    utils->getopt(utils->getopt_context, ldapdb, "ldapdb_uri", &tmp.uri, NULL);
    if(!tmp.uri) return SASL_BADPARAM;

    utils->getopt(utils->getopt_context, ldapdb, "ldapdb_id", &tmp.id, NULL);
    utils->getopt(utils->getopt_context, ldapdb, "ldapdb_pw", &tmp.pw, NULL);
    utils->getopt(utils->getopt_context, ldapdb, "ldapdb_mech", &tmp.mech, NULL);
    utils->getopt(utils->getopt_context, ldapdb, "ldapdb_rc", &s, NULL);
    if(s && setenv("LDAPRC", s, 1)) return SASL_BADPARAM;

    p = utils->malloc(sizeof(ldapctx));
    if (!p) return SASL_NOMEM;
    *p = tmp;
    ldapdb_auxprop_plugin.glob_context = p;

    *out_version = SASL_AUXPROP_PLUG_VERSION;

    *plug = &ldapdb_auxprop_plugin;

    return SASL_OK;
}
