#!/bin/sh
#
#	Copyright 2001 horms <horms@vergenet.net>
#		(heavily mangled by alanr)
#
#	Our goal is to not require dragging along anything
#	more than we need.  If this doesn't work on your system,
#	(i.e., your /bin/sh is broken) send us a patch.
#
#	This code loosely based on the corresponding named script in
#	enlightenment, and also on the sort-of-standard autoconf
#	bootstrap script.
#
#

# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
if
  [ X$srcdir = "X" ]
then
  srcdir=.
fi

set -e
#
#	All errors are fatal from here on out...
#	The shell will complain and exit on any "uncaught" error code.
#
#
#	And the trap will ensure sure some kind of error message comes out.
#
trap 'echo ""; echo "$0 exiting due to error (sorry!)." >&2' 0

HERE=`pwd`
cd $srcdir

RC=0

gnu="ftp://ftp.gnu.org/pub/gnu/"

for command in autoconf automake libtoolize
do
  pkg=$command
  case $command in 
    libtoolize)	pkg=libtool;;
  esac
  URL=$gnu/$pkg/
  if
    $command --version </dev/null >/dev/null 2>&1
  then
    : OK $pkg is installed
  else
    RC=$?
    cat <<-!EOF >&2

	You must have $pkg installed to compile the linux-ha package.
	Download the appropriate package for your system,
	or get the source tarball at: $URL
	!EOF
  fi
done

case $RC in
  0)	;;
  *)	exit $RC;;
esac

if
  [ $# -lt 1 ]
then
  cat <<-!
	Running $srcdir/configure with no arguments.
	If you wish to pass any arguments to it, please specify them
	       on the $0 command line.
	!
fi

aclocal $ACLOCAL_FLAGS

if
  autoheader --version  < /dev/null > /dev/null 2>&1
then
  autoheader
fi

libtoolize --ltdl --force --copy
automake --add-missing --include-deps
autoconf

cd $HERE

$srcdir/configure "$@"

echo 
echo "Now type 'make' to compile the system."
trap '' 0
