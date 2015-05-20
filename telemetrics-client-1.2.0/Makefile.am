SUBDIRS = .

ACLOCAL_AMFLAGS = -I build-aux/m4

EXTRA_DIST = \
	AUTHORS \
	LICENSE.LGPL-2.1

DISTCHECK_CONFIGURE_FLAGS = \
	--with-systemdsystemunitdir=$$dc_install_base/$(systemdunitdir)

include $(top_srcdir)/build-aux/make/cflags.make
include $(top_srcdir)/build-aux/make/ldflags.make
include $(top_srcdir)/src/local.mk
include $(top_srcdir)/src/data/local.mk
include $(top_srcdir)/src/probes/local.mk
include $(top_srcdir)/tests/local.mk

release:
	@git rev-parse v$(PACKAGE_VERSION) &> /dev/null; \
	if [ "$$?" -eq 0 ]; then \
		echo "Error: Release $(PACKAGE_VERSION) already exists."; \
		echo "Bump version in configure.ac before releasing."; \
		exit 1; \
	fi
	@git tag -a -m "Release $(PACKAGE_VERSION)" v$(PACKAGE_VERSION)
	@printf "\nNew release $(PACKAGE_VERSION) tagged!\n\n"
