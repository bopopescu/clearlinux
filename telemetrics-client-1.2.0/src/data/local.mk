pathfix = @sed \
	-e 's|@prefix[@]|$(prefix)|g' \
	-e 's|@bindir[@]|$(bindir)|g' \
	-e 's|@libdir[@]|$(libdir)|g' \
	-e 's|@localstatedir[@]|$(localstatedir)|g' \
	-e 's|@SO_CUR[@]|$(SHAREDLIB_CURRENT)|g' \
	-e 's|@SO_REV[@]|$(SHAREDLIB_REVISION)|g' \
	-e 's|@SO_AGE[@]|$(SHAREDLIB_AGE)|g' \
	-e 's|@SOCKETDIR[@]|$(SOCKETDIR)|g'

EXTRA_DIST += \
	%D%/99-core-ulimit.conf \
	%D%/99-crash-probe.conf.in \
	%D%/example.conf \
	%D%/journal-probe.service.in \
	%D%/libtelemetry.pc.in \
	%D%/telemd.path.in \
	%D%/telemd.service.in \
	%D%/telemd.socket.in \
	%D%/telemetrics-dirs.conf.in \
	%D%/telemetrics.conf.in

configdefaultdir = $(datadir)/defaults/telemetrics
configdefault_DATA = %D%/telemetrics.conf

%D%/telemetrics.conf: %D%/telemetrics.conf.in
	$(pathfix) < $< > $@

tmpfilesdir = $(prefix)/lib/tmpfiles.d
tmpfiles_DATA = %D%/telemetrics-dirs.conf

%D%/telemetrics-dirs.conf: %D%/telemetrics-dirs.conf.in
	$(pathfix) < $< > $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = %D%/libtelemetry.pc

%D%/libtelemetry.pc: %D%/libtelemetry.pc.in
	$(pathfix) < $< > $@

systemdunitdir = @SYSTEMD_UNITDIR@
systemdunit_DATA = \
	%D%/journal-probe.service \
	%D%/telemd.service \
	%D%/telemd.socket \
	%D%/telemd.path

%D%/journal-probe.service: %D%/journal-probe.service.in
	$(pathfix) < $< > $@

%D%/telemd.service: %D%/telemd.service.in
	$(pathfix) < $< > $@

%D%/telemd.socket: %D%/telemd.socket.in
	$(pathfix) < $< > $@

%D%/telemd.path: %D%/telemd.path.in
	$(pathfix) < $< > $@

sysctldir = $(prefix)/lib/sysctl.d
sysctl_DATA = %D%/99-crash-probe.conf

%D%/99-crash-probe.conf: %D%/99-crash-probe.conf.in
	$(pathfix) < $< > $@

systemconfdir = $(prefix)/lib/systemd/system.conf.d
systemconf_DATA = %D%/99-core-ulimit.conf

clean-local:
	-rm -f  %D%/telemd.service \
		%D%/telemd.socket \
		%D%/telemd.path \
		%D%/telemetrics.conf \
		%D%/telemetrics-dirs.conf \
		%D%/libtelemetry.pc \
		%D%/99-crash-probe.conf \
		%D%/journal-probe.service \
		%D%/.dirstamp
