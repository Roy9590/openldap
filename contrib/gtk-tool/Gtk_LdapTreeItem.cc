#include "Gtk_LdapTreeItem.h"
#include <gtk--/base.h>

Gtk_LdapTreeItem::Gtk_LdapTreeItem() : Gtk_TreeItem() {
	this->objectClass = NULL;
}

Gtk_LdapTreeItem::Gtk_LdapTreeItem(char *c, My_Window *w, LDAP *ld) : Gtk_TreeItem() {
	debug("Gtk_LdapTreeItem::Gtk_LdapTreeItem(%s)\n", c);
	char **s;
	this->dn = c;
	s = ldap_explode_dn(this->dn, 1);
	this->rdn = g_strdup_printf("%s", s[0]);
	this->par = w;
	this->ld = ld;
	this->objectClass = NULL;
	this->getDetails();
}

Gtk_LdapTreeItem::Gtk_LdapTreeItem(GtkTreeItem *t) : Gtk_TreeItem(t) {
}

Gtk_LdapTreeItem::~Gtk_LdapTreeItem() {
	cout << "Bye" << endl;
	delete this;
}

Gtk_LdapTree* Gtk_LdapTreeItem::getSubtree(LDAP *ld, int counter) {
	debug("Gtk_LdapTreeItem::getSubtree(%s)\n", this->dn);
	if (counter <= 0) return NULL;
	if (this->gtkobj()->subtree != NULL) {
		return (Gtk_LdapTree *)GTK_TREE(this->gtkobj()->subtree);
	}
	counter--;
	Gtk_LdapTree *subtree = NULL, *tree = NULL;
	Gtk_LdapTreeItem *subtreeitem = NULL;
	LDAPMessage *r_i = NULL, *entry = NULL;
	gchar *c;
	char **s;
	int entriesCount = 0, error;

	this->ld = ld;
	error = ldap_search_s(this->ld, this->dn, LDAP_SCOPE_ONELEVEL, "objectclass=*", NULL, 0, &r_i);
//	printf("%s\n", ldap_err2string(error));
	entriesCount = ldap_count_entries(this->ld, r_i);
	debug("%i results\n", entriesCount);
	if (entriesCount != 0) { 
		tree = new Gtk_LdapTree();
		tree->set_selection_mode(GTK_SELECTION_BROWSE);
		tree->set_view_mode(GTK_TREE_VIEW_ITEM);
		tree->set_view_lines(false);
		entry = ldap_first_entry(this->ld, r_i);
	//	float i = 1;
	//	float percent = 100/entriesCount;
	//	cout << "percent is " << percent << endl;
	//	this->par->progress.update(0);
	//	this->par->progress.show();
		while (entry != NULL) {
			subtreeitem = new Gtk_LdapTreeItem(ldap_get_dn(this->ld, entry), this->par, this->ld);
			subtree = subtreeitem->getSubtree(this->ld, counter);
			debug("inserting %s into %s\n",subtreeitem->rdn,this->rdn);
			tree->append(*subtreeitem);
			subtreeitem->show();
			if (subtree != NULL) subtreeitem->set_subtree(*subtree);
			debug("done\n");
			entry = ldap_next_entry(this->ld, entry);
		//	gfloat pvalue = (i*percent)/100;
		//	cout << pvalue << " %" << endl;
		//	this->par->progress.update(pvalue);
		//	this->par->progress.show();
		//	i++;
		}
	//	this->set_subtree(*tree);
	//	this->par->progress.update(0);
	//	this->par->progress->show();
	}
//	this->getDetails();
	debug("done\n");
	return tree;
}

void Gtk_LdapTreeItem::setType(int t) {
	debug("Gtk_LdapTreeItem::setType(%s)\n", this->objectClass);
	Gtk_Pixmap *xpm_icon;
	Gtk_Label *label;
	if (this->getchild() != NULL) {
		xpm_label = new Gtk_HBox(GTK_HBOX(this->getchild()->gtkobj()));
		xpm_label->remove_c(xpm_label->children()->nth_data(0));
		xpm_label->remove_c(xpm_label->children()->nth_data(0));
	}
	else xpm_label = new Gtk_HBox();
	if (strcasecmp(this->objectClass,"organization") == 0)
		xpm_icon=new Gtk_Pixmap(*xpm_label, root_node);
	else if (strcasecmp(this->objectClass,"organizationalunit") == 0)
		xpm_icon=new Gtk_Pixmap(*xpm_label, branch_node);
	else if (strcasecmp(this->objectClass,"person") == 0)
		xpm_icon=new Gtk_Pixmap(*xpm_label, leaf_node);
	else if (strcasecmp(this->objectClass,"alias") == 0)
		xpm_icon=new Gtk_Pixmap(*xpm_label, alias_node);
	else if (strcasecmp(this->objectClass,"rfc822mailgroup") == 0)
		xpm_icon=new Gtk_Pixmap(*xpm_label, rfc822mailgroup_node);
	else xpm_icon=new Gtk_Pixmap(*xpm_label, general_node);
	label = new Gtk_Label(this->rdn);
	xpm_label->pack_start(*xpm_icon, false, false, 1);
	xpm_label->pack_start(*label, false, false, 1);
	if (this->getchild() == NULL) this->add(xpm_label);
	label->show();
	xpm_label->show();
	xpm_icon->show();
}

int Gtk_LdapTreeItem::showDetails() {
	debug("Gtk_LdapTreeItem::showDetails()\n");
	if (this->notebook != NULL) {
		if (par->viewport->getchild() != NULL) {
			par->viewport->remove_c(par->viewport->getchild()->gtkobj());
		}
		par->viewport->add(this->notebook);
		this->notebook->show();
		par->viewport->show();
		return 0;
	}
	else this->getDetails();
	this->showDetails();
	return 0;
}

int Gtk_LdapTreeItem::getDetails() {
	debug("Gtk_LdapTreeItem::getDetails()\n");
	int error, entriesCount;
	BerElement *ber;
	LDAPMessage *entry;
	char *attribute, **values;
	Gtk_CList *table;
	Gtk_Label *label;
	GList *child_list;
	Gtk_Notebook *g;
	Gtk_Viewport *viewport;
	error = ldap_search_s(this->ld, this->dn, LDAP_SCOPE_BASE, "objectclass=*", NULL, 0, &this->result_identifier);
	entriesCount = ldap_count_entries(this->ld, this->result_identifier);
	if (entriesCount == 0) return 0;
	notebook = new Gtk_Notebook();
	notebook->set_tab_pos(GTK_POS_LEFT);
	const gchar *titles[] = { "values" };
	
	for (entry = ldap_first_entry(ld, result_identifier); entry != NULL; entry = ldap_next_entry(ld, result_identifier)) {
		for (attribute = ldap_first_attribute(ld, entry, &ber); attribute != NULL; attribute = ldap_next_attribute(ld, entry, ber)) {
			values = ldap_get_values(ld, entry, attribute);
			if (strcasecmp(attribute, "objectclass") == 0) {
			//	debug("processing objectclass\n");
				if (strcasecmp(values[0],"top") == 0)
					this->objectClass = strdup(values[1]);
				else this->objectClass = strdup(values[0]);
			}
			table = new Gtk_CList(1, titles);
			for (int i=0; i<ldap_count_values(values); i++) {
			//	debug("%i:%s\n",i, values[i]);
				const gchar *t[] = { values[i] };
				table->append(t);
			}
			ldap_value_free(values);
			label = new Gtk_Label(attribute);
			label->set_alignment(0, 0);
			label->set_justify(GTK_JUSTIFY_LEFT);
			notebook->append_page(*table, *label);
			table->show();
			label->show();
		}
	}
	this->setType(1);
	debug("done\n");
	return 0;
}

void Gtk_LdapTreeItem::show_impl() {
	debug("%s showed\n", this->dn);
	Gtk_c_signals_TreeItem *sig=(Gtk_c_signals_TreeItem *)internal_getsignalbase();
	sig->show(GTK_WIDGET(gtkobj()));
}

void Gtk_LdapTreeItem::select_impl() {
	debug("%s selected\n", this->dn);
//	gtk_item_select(GTK_ITEM(GTK_TREE_ITEM(this->gtkobj())));
	Gtk_c_signals_Item *sig=(Gtk_c_signals_Item *)internal_getsignalbase();
	if (!sig->select) return;
	sig->select(GTK_ITEM(gtkobj()));
	this->showDetails();
}

void Gtk_LdapTreeItem::collapse_impl() {
	debug("%s collapsed\n", this->dn);
	Gtk_c_signals_TreeItem *sig=(Gtk_c_signals_TreeItem *)internal_getsignalbase();
	if (!sig->collapse) return;
	sig->collapse(GTK_TREE_ITEM(gtkobj()));
//	gtk_widget_hide(GTK_WIDGET(GTK_TREE(GTK_TREE_ITEM (this->gtkobj())->subtree)));
}

void Gtk_LdapTreeItem::expand_impl() {
	debug("%s expanded\n",this->dn);
	Gtk_LdapTreeItem *item;
	G_List<GtkWidget> *list;
	Gtk_Tree *tree;
	Gtk_c_signals_TreeItem *sig=(Gtk_c_signals_TreeItem *)internal_getsignalbase();
	if (!sig->expand) return;
	sig->expand(GTK_TREE_ITEM(gtkobj()));
//	Gtk_Tree *t;
//	t = new Gtk_Tree(GTK_TREE(GTK_TREE_ITEM(this->gtkobj())->subtree));
//	bool vis = t->visible();
//	if (vis == false) {
//		gtk_widget_show(GTK_WIDGET(GTK_TREE(GTK_TREE_ITEM (this->gtkobj())->subtree)));
//		cout << this->dn << " expanded" << endl;
//	}
//	else {
//		gtk_widget_hide(GTK_WIDGET(GTK_TREE(GTK_TREE_ITEM (this->gtkobj())->subtree)));
//		cout << this->dn << " collapsed" << endl;
//	}
}
