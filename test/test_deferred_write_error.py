#!/usr/bin/env python3

# Copyright 2022 David Robillard <d@drobilla.net>
# SPDX-License-Identifier: ISC

"""Test errors that only really happen when the output file is closed."""

import argparse
import sys
import shlex
import subprocess
import os

parser = argparse.ArgumentParser(description=__doc__)

parser.add_argument("--tool", default="tools/serd-pipe", help="executable")
parser.add_argument("--wrapper", default="", help="executable wrapper")
parser.add_argument("input", help="valid input file")

args = parser.parse_args(sys.argv[1:])
command = shlex.split(args.wrapper) + [args.tool, '-b', '1024', args.input]

if os.path.exists("/dev/full"):

    with open("/dev/full", "w") as out:
        proc = subprocess.run(
            command, check=False, stdout=out, stderr=subprocess.PIPE
        )

    assert proc.returncode != 0
    assert "error" in proc.stderr.decode("utf-8")

else:
    sys.stderr.write("warning: /dev/full not present, skipping test")
