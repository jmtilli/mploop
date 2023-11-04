# MPloop

MPloop is a set of scripts for playing music in the background. The idea is
that the "loop" part, which is a script that reads the queue and plays songs in
that order, is started in a screen or tmux session. The "loop" uses MPlayer as
the tool for playing music.

All of the songs are played from queue in the order they are in the queue.

The user interface is simply a set of scripts managing the queue from a Unix
shell.

The scripts are:

* mpq for listing queue and enqueuing new songs
* mprm for removing individual items from the queue
* mpclear for clearing the entire queue
* mpshuffle for shuffling the entire queue
* vimp for more complex editing of the queue, such as targeted order changes

## Requirements

MPloop requires either MPlayer or the custom included music player,
mploopplayer. You decide whether mploopplayer is used simply by compiling it;
if the executable binary is detected, it is used. Also the `file` utility is
needed.

However, for full support it is heavily recommended the following tools
are installed:

* `id3v2`
* `id3tool`
* `mp3gain`
* `metaflac`
* `vorbiscomment`
* `opusinfo` or `opustags` or both

## How to install

The installation is simply done by creating the following links:

```
mkdir -p ~/.local/bin
ln -s `pwd`/mpclear.py ~/.local/bin/mpclear
ln -s `pwd`/mploop.py ~/.local/bin/mploop
ln -s `pwd`/mpq.py ~/.local/bin/mpq
ln -s `pwd`/mprm.py ~/.local/bin/mprm
ln -s `pwd`/mpshuffle.py ~/.local/bin/mpshuffle
ln -s `pwd`/vimp.py ~/.local/bin/vimp
```

By default, if you have not compiled mploopplayer, mploop uses MPlayer which
has to be installed obviously. However, you can compile mploopplayer which
requires libavcodec, libavutil, libavformat and SDL2. Compilation of
mploopplayer is as follows. First, install byacc and flex and then stirmake:

```
git clone https://github.com/Aalto5G/stirmake
cd stirmake
git submodule init
git submodule update
cd stirc
make
mkdir -p ~/.local
sh install.sh
```

Then compile mploopplayer (ensuring that libavcodec, libavutil, libavformat and
SDL2 headers are available), first entering the mploop directory:

```
smka
```

The binary location in mploop/mploopplayer directory is automatically detected.
If it's executable, this binary is used.
