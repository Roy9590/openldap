##---------------------------------------------------------------------------
##
## Makefile Template for Manual Pages
##

MANDIR=$(mandir)/man$(MANSECT)

install-common: all-common install-local
	-$(MKDIR) -p $(MANDIR)
	@TMPMAN=/tmp/ldapman.$$$$$(MANCOMPRESSSUFFIX); \
	VERSION=`$(CAT) $(VERSIONFILE)`; \
	for page in *.$(MANSECT); do \
		$(SED) -e "s%LDVERSION%$$VERSION%" \
			-e 's%ETCDIR%$(sysconfdir)%' \
			-e 's%SYSCONFDIR%$(sysconfdir)%' \
			-e 's%SBINDIR%$(sbindir)%' \
			-e 's%BINDIR%$(bindir)%' \
			-e 's%LIBDIR%$(libdir)%' \
			-e 's%LIBEXECDIR%$(libexecdir)%' \
			$$page | $(MANCOMPRESS) > $$TMPMAN; \
		echo "installing $(MANDIR)/$$page"; \
		$(RM) $(MANDIR)/$$page $(MANDIR)/$$page$(MANCOMPRESSSUFFIX); \
		$(INSTALL) $(INSTALLFLAGS) -m 644 $$TMPMAN $(MANDIR)/$$page$(MANCOMPRESSSUFFIX); \
		if [ -f "$$page.links" ]; then \
			for link in `$(CAT) $$page.links`; do \
				echo "installing $(MANDIR)/$$link as link to $$page"; \
				$(RM) $(INSTDIR)/$$link $(MANDIR)/$$link$(MANCOMPRESSSUFFIX); \
				ln -sf $$page$(MANCOMPRESSSUFFIX) $(MANDIR)/$$link$(MANCOMPRESSSUFFIX); \
			done; \
		fi; \
	done; \
	$(RM) $$TMPMAN

all-common: all-local 
clean-common: 	clean-local
veryclean-common: veryclean-local clean-local
depend-common: depend-local
lint: lint-local
lint5: lint5-local

# these could be empty
lint-local: FORCE
lint5-local: FORCE

Makefile: $(top_srcdir)/build/lib.mk
