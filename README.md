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
* mpshuffle for shuffling the entire queue
* vimp for more complex editing of the queue, such as targeted order changes
