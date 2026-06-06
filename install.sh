#!/bin/sh

set -e

die()
{
	echo "$@" > /dev/stderr
	exit 1
}

PREFIX=~/.local

if [ "$#" -ge 1 ]; then
	PREFIX="$1"
fi

H="`hostname`"
P="$PREFIX"

install_bin()
{
	cp "$1/$2" "$PREFIX/bin/.tmp.mploopinst.$$.$2" || die "Can't create temporary binary"
	mv "$PREFIX/bin/.tmp.mploopinst.$$.$2" "$PREFIX/bin/$2" || die "Can't rename temporary binary"
}

install_share()
{
	cp "$1" "$PREFIX/share/mploop/.tmp.mploopinst.$$.$1" || die "Can't create temporary file"
	mv "$PREFIX/share/mploop/.tmp.mploopinst.$$.$1" "$PREFIX/share/mploop/$1" || die "Can't rename temporary file"
}

instman()
{
  mkdir -p "$P/man/man$2" || die "Can't create man directory"
  cp "$1.$2" "$P/man/man$2/.$1.$2.mploopinst.$$.$H" || die "Can't install man page"
  mv "$P/man/man$2/.$1.$2.mploopinst.$$.$H" "$P/man/man$2/$1.$2" || die "Can't rename man page"
}

install_link()
{
	ln -s "$PREFIX/share/mploop/$1.py" "$PREFIX/bin/.tmp.mploopinst.$$.$1" || die "Can't create symlink"
	mv "$PREFIX/bin/.tmp.mploopinst.$$.$1" "$PREFIX/bin/$1" || die "Can't rename symlink"
}

mkdir -p "$PREFIX"/bin || die "Can't create bin directory"
mkdir -p "$PREFIX"/share/mploop || die "Can't create share/mploop directory"

if [ -e mploopplayer/mploopplayer ]; then
  install_bin mploopplayer mploopplayer
else
  echo "No binary, not a problem, using MPlayer"
fi

install_share libmploopflac.py
install_share libmploopmp4.py
install_share libmploopogg.py
install_share libmploop.py
install_share libplaylist.py
install_share libtag.py

install_share mploop.py
install_share mpclear.py
install_share mpq.py
install_share mprm.py
install_share mpshuffle.py
install_share vimp.py
install_share mpnext.py
install_share mpplaypause.py
install_share mpprev.py
install_share mprewind.py
install_share mpseek.py
install_share mpnp.py

install_link mploop
install_link mpclear
install_link mpq
install_link mprm
install_link mpshuffle
install_link vimp
install_link mpnext
install_link mpplaypause
install_link mpprev
install_link mprewind
install_link mpseek
install_link mpnp

instman mploop 1
instman mpclear 1
instman mpq 1
instman mprm 1
instman mpshuffle 1
instman vimp 1
instman mpnext 1
instman mpplaypause 1
instman mpprev 1
instman mprewind 1
instman mpseek 1
instman mpnp 1
