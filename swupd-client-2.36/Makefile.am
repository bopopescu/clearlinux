EXTRA_DIST = COPYING

AM_CFLAGS = -fPIC -Iinclude/ -O2 -g -Wall -W -Wformat-security -D_FORTIFY_SOURCE=2 -DSWUPD_LINUX_ROOTFS
ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = \
	swupd_bsdump \
	swupd_bsdiff \
	swupd_bspatch \
	swupd_hashdump \
	swupd_update \
	swupd_verify \
	clr_bundle_add \
	clr_bundle_rm

check_PROGRAMS = \
	swupd_bsdiff_bench \
	swupd_hashtest \
	swupd_listtest \
	swupd_executortest \
	swupd_fuzz \
	swupd_locktest \
	swupd_sig_verifytest

SWUPD_COMMON_SOURCES = \
	src/curl.c \
	src/delta.c \
	src/download.c \
	src/executor.c \
	src/filedesc.c \
	src/hash.c \
	src/helpers.c \
	src/heuristics.c \
	src/list.c \
	src/lock.c \
	src/log.c \
	src/manifest.c \
	src/packs.c \
	src/signature.c \
	src/staging.c \
	src/stats.c \
	src/subscriptions.c \
	src/tm_report.c \
	src/update.c \
	src/version.c \
	src/xattrs.c \
	src/esp.c \
	src/diskspace.c \
	src/efivars.c \
	src/urls.c \
	src/progress.c \
	src/globals.c \
	src/scripts.c

SWUPD_BSDIFF_SOURCES = \
	bsdiff/swupd_bsdiff.c \
	bsdiff/swupd_bspatch.c

lib_LTLIBRARIES = libswupd.la
libswupd_la_SOURCES = $(SWUPD_COMMON_SOURCES) $(SWUPD_BSDIFF_SOURCES)
libswupd_la_LIBADD = $(SWUPD_COMPRESSION_LIBS) $(openssl_LIBS) $(curl_LIBS)

swupd_update_SOURCES = src/main.c
swupd_verify_SOURCES = src/verify.c

clr_bundle_add_SOURCES = src/clr_bundle_add.c
clr_bundle_rm_SOURCES = src/clr_bundle_rm.c

swupd_bsdump_SOURCES = bsdiff/swupd_bsdump.c
swupd_bsdiff_SOURCES = bsdiff/diff.c
swupd_bspatch_SOURCES = bsdiff/patch.c
swupd_hashdump_SOURCES = src/hashdump.c

swupd_bsdiff_bench_SOURCES = test/bsdiff_bench.c
swupd_hashtest_SOURCES = test/hash_test.c
swupd_listtest_SOURCES = test/listtest.c
swupd_executortest_SOURCES = test/executor_test.c
swupd_fuzz_SOURCES = test/fuzz.c
swupd_locktest_SOURCES = test/locktest.c
swupd_sig_verifytest_SOURCES = test/signature/verify_test.c


AM_CPPFLAGS = $(AM_CFLAGS) -Iinclude/
SWUPD_COMPRESSION_LIBS = $(lzma_LIBS) $(zlib_LIBS) $(bzip2_LIBS)
SWUPD_CORE_LIBS = libswupd.la ${curl_LIBS} $(openssl_LIBS) $(SWUPD_COMPRESSION_LIBS)

swupd_update_LDADD = $(SWUPD_CORE_LIBS) $(pthread_LIBS)
swupd_verify_LDADD = $(SWUPD_CORE_LIBS) $(pthread_LIBS)

clr_bundle_add_LDADD = $(SWUPD_CORE_LIBS) $(pthread_LIBS)
clr_bundle_rm_LDADD = $(SWUPD_CORE_LIBS) $(pthread_LIBS)

swupd_bsdiff_LDADD = $(SWUPD_CORE_LIBS)
swupd_bspatch_LDADD = $(SWUPD_CORE_LIBS)
swupd_hashdump_LDADD = $(SWUPD_CORE_LIBS)

swupd_bsdiff_bench_LDADD = $(SWUPD_CORE_LIBS)
swupd_hashtest_LDADD = $(SWUPD_CORE_LIBS) $(pthread_LIBS)
swupd_listtest_LDADD = $(SWUPD_CORE_LIBS)
swupd_executortest_LDADD = $(SWUPD_CORE_LIBS) $(pthread_LIBS)
swupd_fuzz_LDADD = $(SWUPD_CORE_LIBS) $(pthread_LIBS)
swupd_locktest_LDADD = $(SWUPD_CORE_LIBS) $(pthread_LIBS)
swupd_sig_verifytest_LDADD = $(SWUPD_CORE_LIBS)

noinst_HEADERS = include/*

swupdcertsdir = @swupdcertsdir@
SWUPD_CERTS = certs/157753a5.0 \
	certs/425b0f6b.0 \
	certs/425b0f6b.key \
	certs/8d28ae65.0 \
	certs/d6325660.0 \
	certs/d6325660.1
swupdcerts_DATA = $(SWUPD_CERTS)
dist_swupdcerts_DATA = $(SWUPD_CERTS)

distclean-local:
	rm -rf aclocal.m4 ar-lib autom4te.cache config.guess config.h.in config.h.in~ config.sub configure depcomp install-sh ltmain.sh m4 Makefile.in missing
