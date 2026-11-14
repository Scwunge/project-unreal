"""Packs a staged Linux server folder into a .tar.gz with forward-slash names, root ownership and unix modes
(scripts and binaries executable), which Windows' Compress-Archive cannot do.  Usage: make-tar.py <stage> <out.tar.gz>"""
import os, sys, tarfile

stage, out = sys.argv[1], sys.argv[2]
EXEC_NAMES = {'run-server.sh', 'McClient.sh'}

def is_exec(rel, name):
    return name in EXEC_NAMES or name.endswith('-Shipping') or name.endswith('.sh') or '/Binaries/' in rel and '.' not in name

def filt(ti):
    ti.uid = ti.gid = 0
    ti.uname = ti.gname = 'root'
    ti.mode = 0o755 if ti.isdir() or is_exec(ti.name, os.path.basename(ti.name)) else 0o644
    return ti

with tarfile.open(out, 'w:gz', compresslevel=6) as tf:
    for root, dirs, files in os.walk(stage):
        dirs.sort()
        for n in sorted(files) + dirs:
            p = os.path.join(root, n)
            tf.add(p, arcname=os.path.relpath(p, stage).replace(os.sep, '/'), recursive=False, filter=filt)
