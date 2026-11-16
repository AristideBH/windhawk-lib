r"""Reads a Windows minidump of explorer.exe (CrashDumps folder) without a debugger.

    python -I tools/crash-analysis/dump.py "%LOCALAPPDATA%\CrashDumps\explorer.exe.1234.dmp"

Prints the exception, the faulting module and offset, the address an access violation read or wrote
(AVADDR), and whether the Windhawk mod's DLL was still loaded. For a read at an address inside an
unloaded mod image, hand the low 16 bits of AVADDR to rd.py with the mod's DLL: the vtable there
names the C++ type (usually a winrt delegate) that was still alive after the unload.
"""
import struct, sys
path = sys.argv[1]
d = open(path, 'rb').read()
sig, ver, nstreams, dirrva = struct.unpack_from('<IIII', d, 0)
streams = {}
for i in range(nstreams):
    t, size, rva = struct.unpack_from('<III', d, dirrva + i * 12)
    streams.setdefault(t, []).append((size, rva))
print('streams', sorted(streams))
# modules
mods = []
size, rva = streams[4][0]
n = struct.unpack_from('<I', d, rva)[0]
for i in range(n):
    base, msize, csum, ts, nameRva = struct.unpack_from('<QIIII', d, rva + 4 + i * 108)
    ln = struct.unpack_from('<I', d, nameRva)[0]
    name = d[nameRva + 4:nameRva + 4 + ln].decode('utf-16le')
    mods.append((base, msize, name))
def which(a):
    for b, s, n in mods:
        if b <= a < b + s:
            return '%s+0x%x' % (n.split('\\')[-1], a - b)
    return None
# exception
size, rva = streams[6][0]
tid, _ = struct.unpack_from('<II', d, rva)
code, flags, rec, addr, nparams = struct.unpack_from('<IIQQI', d, rva + 8)
print('AV info', [hex(x) for x in struct.unpack_from('<QQ', d, rva + 8 + 32)], 'nparams', nparams)
print('exception thread', tid, hex(code), 'at', hex(addr), which(addr))
ctxsize, ctxrva = struct.unpack_from('<II', d, rva + 8 + 152)
# AMD64 CONTEXT: Rsp at 0x98, Rip at 0xF8
rsp = struct.unpack_from('<Q', d, ctxrva + 0x98)[0]
rip = struct.unpack_from('<Q', d, ctxrva + 0xF8)[0]
print('rsp', hex(rsp), 'rip', hex(rip), which(rip), 'ctxsize', ctxsize)
# memory list
size, rva = streams[5][0]
n = struct.unpack_from('<I', d, rva)[0]
ranges = []
for i in range(n):
    a, sz, r = struct.unpack_from('<QII', d, rva + 4 + i * 16)
    ranges.append((a, sz, r))
stack = None
for a, s, o in ranges:
    if a <= rsp < a + s:
        stack = (a, s, o)
print('stack range', stack and (hex(stack[0]), hex(stack[1])))
if stack:
    a, s, o = stack
    start = rsp - a
    seen = 0
    for p in range(start, min(s, start + 0x6000), 8):
        v = struct.unpack_from('<Q', d, o + p)[0]
        w = which(v)
        if w:
            print('  +0x%x %s' % (p - start, w))
            seen += 1
            if seen > 60:
                break
print('extras modules:', [(n.split(chr(92))[-1], hex(b)) for b,sz,n in mods if 'extras' in n.lower()], 'total', len(mods))
size, rva = streams[6][0]
av = struct.unpack_from('<Q', d, rva + 8 + 40)[0] if False else struct.unpack_from('<QQ', d, rva + 8 + 32)[1]
print('AVADDR', hex(av), which(av))
near = sorted(mods, key=lambda m: abs(m[0] - av))[:3]
print('nearest', [(n.split(chr(92))[-1], hex(b), hex(s)) for b, s, n in near])
