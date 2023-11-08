#!/usr/bin/env python3
import subprocess
subprocess.run(["sh", "-c", "echo|socat - \"unix:$HOME/.mploop/sock\""])
