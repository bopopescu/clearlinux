LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	     $(top_srcdir)/build-aux/tap-driver.sh

AM_TESTS_ENVIRONMENT=SYSTEMDSYSUSERS=$(SYSTEMDSYSUSERS); export SYSTEMDSYSUSERS;

TESTS = t/sysusers.t

sysusers_DATA = sysusers.d/clear.conf

network_DATA = \
	network/80-dhcp.network \
	network/80-virtio.network

udevrules_DATA = \
	rules/80-kvm.rules

systemdsystempreset_DATA = system-preset/80-clear.preset

systemdsystempresettransient_DATA = system-preset-transient/80-clear.preset

systemdjournalconf_DATA = journal.conf.d/clear.conf

systemdsystemunit_DATA = \
	system/remount.service \
	system/clr-locale.service \
	system/locale-archive.service \
	system/update-triggers.target \
	system/ldconfig-trigger.service \
	system/locale-archive-trigger.service \
	system/mandb-trigger.service \
	system/tmpfiles-trigger.service \
	system/catalog-trigger.service

systemdbootchartconf_DATA = bootchart.conf.d/clear.conf

nodist_defaults_DATA = \
	etc/group \
	etc/gshadow \
	etc/passwd \
	etc/shadow

BUILT_SOURCES = $(nodist_defaults_DATA)

CLEANFILES = $(nodist_defaults_DATA) etc/.pwd.lock build.stamp

$(nodist_defaults_DATA): $(sysusers_DATA) build.stamp

build.stamp:
	mkdir -p etc
	$(foreach in,$(nodist_defaults_DATA), cp $(top_srcdir)/$(in).in $(in);)
	$(SYSTEMDSYSUSERS) --root=$(CURDIR) $(CURDIR)/$(sysusers_DATA)
	chmod +r etc/*
	rm -f etc/*- etc/.pwd.lock
	touch $@

EXTRA_DIST= \
	$(TESTS) \
	$(sysusers_DATA) \
	$(network_DATA) \
	$(udevrules_DATA) \
	$(systemdsystempreset_DATA) \
	$(systemdsystempresettransient_DATA) \
	$(systemdsystemconf_DATA) \
	$(systemdjournalconf_DATA) \
	$(systemdsystemunit_DATA) \
	$(systemdbootchartconf_DATA) \
	etc/group.in \
	etc/gshadow.in \
	etc/passwd.in \
	etc/shadow.in
