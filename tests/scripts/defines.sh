
DATADIR=$SRCDIR/data
PROGDIR=./progs

if test "$BACKEND" = "bdb2" ; then
	LDIF2LDBM=../servers/slapd/tools/ldif2ldbm-bdb2
	CONF=$DATADIR/slapd-bdb2-master.conf
	ACLCONF=$DATADIR/slapd-bdb2-acl.conf
	MASTERCONF=$DATADIR/slapd-bdb2-repl-master.conf
	SLAVECONF=$DATADIR/slapd-bdb2-repl-slave.conf
	REFSLAVECONF=$DATADIR/slapd-bdb2-ref-slave.conf
	TIMING="-t"
else
	LDIF2LDBM=../servers/slapd/tools/ldif2ldbm
	CONF=$DATADIR/slapd-master.conf
	ACLCONF=$DATADIR/slapd-acl.conf
	MASTERCONF=$DATADIR/slapd-repl-master.conf
	SLAVECONF=$DATADIR/slapd-repl-slave.conf
	REFSLAVECONF=$DATADIR/slapd-ref-slave.conf
fi

if test "$LDAP_PROTO" ; then
	PROTO="-P $LDAP_PROTO"
fi

PASSWDCONF=$DATADIR/slapd-passwd.conf

CLIENTDIR=../clients/tools
#CLIENTDIR=/usr/local/bin

SLAPD=../servers/slapd/slapd
SLURPD=../servers/slurpd/slurpd
LDAPSEARCH="$CLIENTDIR/ldapsearch $PROTO -LLL"
LDAPMODIFY="$CLIENTDIR/ldapmodify $PROTO"
LDAPADD="$CLIENTDIR/ldapadd $PROTO"
LDAPMODRDN="$CLIENTDIR/ldapmodrdn $PROTO"
SLAPDTESTER=$PROGDIR/slapd-tester
LVL=${SLAPD_DEBUG-5}
ADDR=127.0.0.1
PORT=9009
SLAVEPORT=9010
DBDIR=./test-db
REPLDIR=./test-repl
LDIF=$DATADIR/test.ldif
LDIFORDERED=$DATADIR/test-ordered.ldif
MONITOR="cn=monitor"
BASEDN="o=University of Michigan, c=US"
MANAGERDN="cn=Manager, o=University of Michigan, c=US"
UPDATEDN="cn=Replica, o=University of Michigan, c=US"
PASSWD=secret
BABSDN="cn=Barbara Jensen, ou=Information Technology Division, ou=People, o=University of Michigan, c=US"
BJORNSDN="cn=Bjorn Jensen, ou=Information Technology Division, ou=People, o=University of Michigan, c=US"
JAJDN="cn=James A Jones 1, ou=Alumni Association, ou=People, o=University of Michigan, c=US"
MASTERLOG=$DBDIR/master.log
SLAVELOG=$DBDIR/slave.log
SLURPLOG=$DBDIR/slurp.log
SEARCHOUT=$DBDIR/ldapsearch.out
SEARCHFLT=$DBDIR/ldapsearch.flt
LDIFFLT=$DBDIR/ldif.flt
MASTEROUT=$DBDIR/master.out
SLAVEOUT=$DBDIR/slave.out
TESTOUT=$DBDIR/test.out
SEARCHOUTMASTER=$DATADIR/search.out.master
MODIFYOUTMASTER=$DATADIR/modify.out.master
ADDDELOUTMASTER=$DATADIR/adddel.out.master
MODRDNOUTMASTER0=$DATADIR/modrdn.out.master.0
MODRDNOUTMASTER1=$DATADIR/modrdn.out.master.1
MODRDNOUTMASTER2=$DATADIR/modrdn.out.master.2
MODRDNOUTMASTER3=$DATADIR/modrdn.out.master.3
ACLOUTMASTER=$DATADIR/acl.out.master
REPLOUTMASTER=$DATADIR/repl.out.master
MODSRCHFILTERS=$DATADIR/modify.search.filters
