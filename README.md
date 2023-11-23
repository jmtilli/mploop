# MPloop

MPloop is a set of scripts for playing music in the background. The idea is
that the "loop" part, which is a script that reads the queue and plays songs in
that order, is started in a screen or tmux session. The "loop" uses MPlayer or
a custom included music player as the tool for playing music.

All of the songs are played from queue in the order they are in the queue.

The user interface is simply a set of scripts managing the queue from a Unix
shell, and a set of scripts controlling the player.

The scripts for managing the queue are:

* mpq for listing queue and enqueuing new songs
* mprm for removing individual items from the queue
* mpclear for clearing the entire queue
* mpshuffle for shuffling the entire queue
* vimp for more complex editing of the queue, such as targeted order changes

The scripts controlling the player are:

* mpprev for jumping to the previous song or N songs backwards
* mpnext for jumping to the next song or N songs forwards
* mpplaypause for toggling between playing and paused states
* mprewind for rewinding back to the beginning of the current song
* mpseek for seeking back/forwards the specified number of seconds

## Supported file formats

There are two implementations of the actual software that plays music. One is
using MPlayer that offers a very wide variety of file formats. A second one is
MPloop's custom music player. It is based on libavformat and libavcodec, so a
file format that is supported by MPlayer but not by libavformat and/or
libavcodec does not work then. However, in practice most of MPlayer's format
support happens via libavformat and libavcodec, so the format support is good
no matter which implementation is used.

However, metadata support for different formats is more limited. ReplayGain is
supported for only Ogg Vorbis, Opus, FLAC, MP3 and MP4 (AAC). Also comment tags
are supported for the same formats, and MP3 ID3 tags and MP4 tags are
automatically converted to Vorbis comments.

## Stream support

Through MPlayer and libavformat that is used in MPloop's custom music player,
you get a large variety of streaming protocols such as HTTP and RTP. However,
since the tag and ReplayGain support in MPloop does not go through these
mechanisms, but rather uses tools operating only on the local file system,
all metadata including ReplayGain is lost when playing remote streams.

To play a stream, use the argument `-u` to mpq. An example:

```
mpq -u https://upload.wikimedia.org/wikipedia/en/2/26/Europe_-_The_Final_Countdown.ogg
```

## Philosophy

MPloop has a certain philosophy behind it. Because music is not visual, playing
music should not require any X or Wayland server, but rather occur from command
line. MPloop is intended to be used in a screen or tmux session so that if it
is controlled via X or Wayland server, it can survive a logout, and if it is
controlled via SSH, it can survive a network connection failure.

MPloop is not a daemon. It prints status information, such as information on
the song being played, and a running status line, to stdout. It also takes
input from stdin, allowing seeking via cursor keys (left: minus 10 sec, right:
plus 10 sec, down: minus 60 sec, up: plus 60 sec, page down: minus 600 sec,
page up: plus 600 sec). It can also be paused and resumed by pressing space bar
or the `p` key. The current song can be skipped by pressing enter or the `q`
key. Volume can be adjusted using `9` and `0` or `/` and `*`. These keyboard
shortcuts are inherited from MPlayer, but also work for the custom player
included in MPloop currently.

MPloop does not want to know where your music is stored. Maybe you have a
number of directories on different hard disk drives that each contain part of
your music library. You don't need to organize your music in a tidy manner.
MPloop can play music from any directory, and it doesn't want to scan your
entire music library. This is where it differs from MPD (music player daemon).

MPloop has been designed in such a manner that you use the Unix command line to
control it. Want to find some song? Just use `find Music | grep -i
name.of.song`. Want to play an entire album in shuffled order? Just use `cd
albumdirectory; mpq -s *.ogg`. The Unix shell is an exceptionally capable tool
of working with files, and creating some interface that is isolated from Unix
shell just for playing music would be a major crime.

## Requirements

MPloop requires either MPlayer or the custom included music player,
mploopplayer. You decide whether mploopplayer is used simply by compiling it;
if the executable binary is detected, it is used. Also the `file` utility is
needed.

However, for full support it is heavily recommended the following tools
are installed:

* `metaflac`
* `opusinfo` or `opustags` or both
* `AtomicParsley`

## How to install

The installation is done by executing the script install.sh

```
./install.sh
```

By default, it is installed to `~/.local/bin`. You can install into
`/usr/local/bin` by:

```
./install.sh /usr/local
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
