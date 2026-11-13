"""Joins explorer-extras/src/*.cpp into explorer-extras/explorer-extras.wh.cpp.

Windhawk takes one file, so the source is kept in parts and joined here. Edit the
parts, run this, and never edit the generated file by hand.

    python explorer-extras/build.py            write the generated file
    python explorer-extras/build.py --check    exit 1 if it is out of date
"""
import glob
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
parts = sorted(glob.glob(os.path.join(here, 'src', '*.cpp')))
joined = b''.join(open(path, 'rb').read() for path in parts)
target = os.path.join(here, 'explorer-extras.wh.cpp')

if '--check' in sys.argv:
    current = open(target, 'rb').read() if os.path.exists(target) else b''
    sys.exit(0 if current == joined else 1)

open(target, 'wb').write(joined)
print('wrote %s from %d parts, %d bytes' % (target, len(parts), len(joined)))
