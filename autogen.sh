#!/bin/sh
# Run this to set up the build system: configure, makefiles, etc.

# $Id: autogen.sh,v 1.5 2003/03/04 16:52:48 giles Exp $

package="jbig2dec"
AUTOMAKE_FLAGS="$AUTOMAKE_FLAGS"

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

cd "$srcdir"

(autoconf --version) < /dev/null > /dev/null 2>&1 || {
        echo
        echo "You must have autoconf installed to compile $package."
        echo "Download the appropriate package for your distribution,"
        echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/"
	exit 1
}

VERSIONGREP="sed -e s/.*[^0-9\.]\([0-9]\.[0-9]\).*/\1/"

# do we need automake?
if test -r Makefile.am; then
  AM_NEEDED=`fgrep AUTOMAKE_OPTIONS Makefile.am | $VERSIONGREP`
  if test -z $AM_NEEDED; then
    echo -n "checking for automake..."
    AUTOMAKE=automake
    ACLOCAL=aclocal
    if ($AUTOMAKE --version < /dev/null > /dev/null 2>&1); then
      echo "no"
      AUTOMAKE=
    else
      echo "yes"
    fi
  else
    echo -n "checking for automake $AM_NEEDED or later..."
    for am in automake-$AM_NEEDED automake$AM_NEEDED automake; do
      ($am --version < /dev/null > /dev/null 2>&1) || continue
      ver=`$am --version < /dev/null | head -1 | $VERSIONGREP`
      if test $ver = $AM_NEEDED; then
        AUTOMAKE=$am
        echo $AUTOMAKE
        break
      fi
    done
    test -z $AUTOMAKE &&  echo "no"
    echo -n "checking for aclocal $AM_NEEDED or later..."
    for ac in aclocal-$AM_NEEDED aclocal$AM_NEEDED aclocal; do
      ($ac --version < /dev/null > /dev/null 2>&1) || continue
      ver=`$ac --version < /dev/null | head -1 | $VERSIONGREP`
      if test $ver = $AM_NEEDED; then
        ACLOCAL=$ac
        echo $ACLOCAL
        break
      fi
    done
    test -z $ACLOCAL && echo "no"
  fi
  test -z $AUTOMAKE || test -z $ACLOCAL && {
        echo
        echo "You must have automake installed to compile $package."
        echo "Download the appropriate package for your distribution,"
        echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/"
	exit 1
  }
fi  


echo "Generating configuration files for $package, please wait...."

echo "  $ACLOCAL $ACLOCAL_FLAGS"
$ACLOCAL $ACLOCAL_FLAGS

echo "  autoheader"
autoheader

echo "  creating config_types.h.in"
cat >config_types.h.in <<EOF
/*
   generated header with missing types for the
   jbig2dec program and library. include this
   after config.h, within the HAVE_CONFIG_H
   ifdef
*/

#ifndef HAVE_STDINT_H
#  ifdef JBIG2_REPLACE_STDINT_H
#   include <@JBIG2_STDINT_H@>
#  else
    typedef unsigned @JBIG2_INT32_T@ uint32_t;
    typedef unsigned @JBIG2_INT16_T@ uint16_t;
    typedef unsigned @JBIG2_INT8_T@ uint8_t;
    typedef signed @JBIG2_INT32_T@ int32_t;
    typedef signed @JBIG2_INT16_T@ int16_t;
    typedef signed @JBIG2_INT8_T@ int8_t;
#  endif /* JBIG2_REPLACE_STDINT */
#endif /* HAVE_STDINT_H */
EOF
    
echo "  $AUTOMAKE --add-missing $AUTOMAKE_FLAGS"
$AUTOMAKE --add-missing $AUTOMAKE_FLAGS 
echo "  running autoconf"

autoconf

if test -z "$*"; then
        echo "I am going to run ./configure with no arguments - if you wish "
        echo "to pass any to it, please specify them on the $0 command line."
else
	echo "running ./configure $@"
fi

$srcdir/configure "$@" && echo