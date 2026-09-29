#!/usr/bin/env python3
"""variant.py SRC DEST [--diag] [F_NAME=false ...]: copy a bot, optionally with BOT_DIAG on and module flags flipped."""
import os, re, shutil, sys

def make(src, dest, flags=(), diag=False):
    os.makedirs(dest, exist_ok=True)
    for f in ('main.cpp', 'helper.hpp', 'bot.toml'):
        shutil.copy2(os.path.join(src, f), dest)
    p = os.path.join(dest, 'main.cpp')
    s = open(p).read()
    for kv in flags:
        k, v = kv.split('=')
        s, n = re.subn(r'(constexpr (?:bool|int|double) %s = )[^;,]+' % re.escape(k), r'\g<1>' + v, s)
        if n != 1:
            s, n = re.subn(r'(\b%s = )(true|false)' % re.escape(k), r'\g<1>' + v, s)
        assert n == 1, (k, n)
    if diag:
        s = '#define BOT_DIAG\n' + s
    open(p, 'w').write(s)

if __name__ == '__main__':
    a = [x for x in sys.argv[1:] if x != '--diag']
    make(a[0], a[1], a[2:], '--diag' in sys.argv)
