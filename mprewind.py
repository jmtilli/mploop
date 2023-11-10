#!/usr/bin/env python3
from __future__ import print_function
from __future__ import division
import libmploop
libmploop.send_mploop_command("?", 30*"\x1b[6~")
