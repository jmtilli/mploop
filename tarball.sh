#!/bin/sh

GITREV="`git describe --tags --always`"

tar -czvf mploop-$GITREV.tar.gz --transform="s|^|mploop-$GITREV/|" *.py install.sh LICENSE.md README.md *.1 Stirfile mploopplayer/Stirfile mploopplayer/*.c mploopplayer/*.h
