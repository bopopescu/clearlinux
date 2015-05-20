EXTRA_DIST += %D%/taplib.sh

TEST_EXTENSIONS = .sh

tap_driver = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/build-aux/tap-driver.sh

LOG_DRIVER = $(tap_driver)
SH_LOG_DRIVER = $(tap_driver)

TESTS = $(check_PROGRAMS) $(dist_check_SCRIPTS)

check_PROGRAMS = \
	%D%/check_config \
	%D%/check_daemon \
	%D%/check_libtelemetry

dist_check_SCRIPTS = \
	%D%/create-core.sh

%C%_check_config_SOURCES = \
	%D%/check_config.c

%C%_check_config_CFLAGS = \
	$(AM_CFLAGS) \
	@CHECK_CFLAGS@ \
	@GLIB_CFLAGS@
%C%_check_config_LDADD = \
	@CHECK_LIBS@ \
	@GLIB_LIBS@ \
	$(top_builddir)/src/libtelem-shared.la

%C%_check_daemon_SOURCES = \
	%D%/check_daemon.c \
	src/telemdaemon.c \
	src/telemdaemon.h

%C%_check_daemon_CFLAGS = \
	$(AM_CFLAGS) \
	@CHECK_CFLAGS@ \
	@GLIB_CFLAGS@ \
	@CURL_CFLAGS@
%C%_check_daemon_LDADD = \
	@CHECK_LIBS@ \
	@GLIB_LIBS@ \
	@CURL_LIBS@ \
	$(top_builddir)/src/libtelem-shared.la

%C%_check_libtelemetry_SOURCES = \
	%D%/check_libtelemetry.c \
	src/telemetry.c

%C%_check_libtelemetry_CFLAGS = \
	$(AM_CFLAGS) \
	@CHECK_CFLAGS@
%C%_check_libtelemetry_LDADD = \
	@CHECK_LIBS@ \
	$(top_builddir)/src/libtelemetry.la \
	$(top_builddir)/src/libtelem-shared.la

# vim: filetype=automake tabstop=8 shiftwidth=8 noexpandtab
