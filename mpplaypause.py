#!/usr/bin/env python3
import subprocess
subprocess.run(["sh", "-c", "echo -n p|socat - \"unix:$HOME/.mploop/sock\""])
