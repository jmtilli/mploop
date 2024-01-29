#!/bin/sh

set -e

PREFIX=~/.local

if [ "$#" -ge 1 ]; then
	PREFIX="$1"
fi

install_link()
{
	if [ -e "$PREFIX/bin/$1" ]; then
		true
	else
		echo ln -s "`pwd`/$1".py "$PREFIX"/bin/"$1"
		ln -s "`pwd`/$1".py "$PREFIX"/bin/"$1"
	fi
}

mkdir -p "$PREFIX"/bin

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
