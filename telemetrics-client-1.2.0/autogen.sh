#!/bin/sh

autoreconf --force --install --warnings=all

if [ "$1" = "debug" ]; then
	echo "Creating config.site for local overrides"
	cat > config.site <<- EOF
	CFLAGS="-ggdb3 -O0" CPPFLAGS="-DDEBUG -U_FORTIFY_SOURCE"
	EOF
	echo "Now run CONFIG_SITE=config.site ./configure ..."
fi
