bin_PROGRAMS += \
	%D%/hprobe \
	%D%/crashprobe

%C%_hprobe_SOURCES = %D%/hello.c
%C%_hprobe_LDADD = $(top_builddir)/src/libtelemetry.la
%C%_hprobe_LDFLAGS = \
	$(AM_LDFLAGS) \
	-pie

%C%_crashprobe_SOURCES = \
	%D%/crash_probe.c
%C%_crashprobe_CFLAGS = \
	$(AM_CFLAGS) \
	$(GLIB_CFLAGS)
%C%_crashprobe_LDADD = \
	$(top_builddir)/src/libtelemetry.la \
	$(top_builddir)/src/libtelem-shared.la \
	@ELFUTILS_LIBS@ \
	$(GLIB_LIBS)
%C%_crashprobe_LDFLAGS = \
	$(AM_LDFLAGS) \
	-pie

if HAVE_SYSTEMD_JOURNAL
if HAVE_SYSTEMD_ID128
bin_PROGRAMS += \
	%D%/journalprobe

%C%_journalprobe_SOURCES = \
	%D%/journal.c
%C%_journalprobe_CFLAGS = \
	$(AM_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(SYSTEMD_ID128_CFLAGS) \
	$(SYSTEMD_JOURNAL_CFLAGS)
%C%_journalprobe_LDADD = \
	$(top_builddir)/src/libtelemetry.la \
	$(top_builddir)/src/libtelem-shared.la \
	$(GLIB_LIBS) \
	$(SYSTEMD_ID128_LIBS) \
	$(SYSTEMD_JOURNAL_LIBS)
%C%_journalprobe_LDFLAGS = \
	$(AM_LDFLAGS) \
	-pie
endif
endif

# vim: filetype=automake tabstop=8 shiftwidth=8 noexpandtab
