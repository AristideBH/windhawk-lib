r"""Names the C++ object behind a faulting address in a Windhawk mod DLL.

    python -I tools/crash-analysis/rd.py "C:/ProgramData/Windhawk/Engine/Mods/arm64/<mod>.dll" <low16bits hex>

An image is mapped at a 64 KB boundary, so the low 16 bits of an address inside it are the same as in the
file. For each .rdata offset that ends in those bits, it walks back to the Itanium vtable header and
prints the type name from the typeinfo (for a winrt delegate: the lambda's enclosing function).
Slot 2 of a delegate is Release: an unloaded mod's delegate that XAML freed late.
"""
import struct,sys
f=sys.argv[1]; low=int(sys.argv[2],16)
d=open(f,'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]
nsec=struct.unpack_from('<H',d,pe+6)[0]; optsz=struct.unpack_from('<H',d,pe+20)[0]
opt=pe+24; sec=opt+optsz
base=struct.unpack_from('<Q',d,opt+24)[0]
secs=[struct.unpack_from('<8sIIII',d,sec+i*40) for i in range(nsec)]
def off(rva):
    for n,vs,va,rs,rp in secs:
        if va<=rva<va+min(vs,rs) if rs else False: return rva-va+rp
    return None
def cstr(rva,maxn=140):
    o=off(rva)
    if o is None: return None
    e=d.find(b'\0',o,o+maxn)
    s=d[o:e if e>0 else o+maxn]
    return s.decode('ascii','replace') if s and all(32<=c<127 for c in s) else None
def q(rva):
    o=off(rva); 
    return struct.unpack_from('<Q',d,o)[0] if o is not None else None
rd=[s for s in secs if s[0].startswith(b'.rdata')][0]
for r in range(rd[2], rd[2]+rd[1], 0x1000):
    pass
cands=[x for x in range(rd[2], rd[2]+rd[1]) if (x&0xffff)==low]
for R in cands:
    print('candidate rva',hex(R))
    # walk back to find vtable header: slot k with 0 and next slot a VA of typeinfo
    for back in range(0,0x300,8):
        a=R-back
        v0=q(a); v1=q(a+8)
        if v0==0 and v1 and base<=v1<base+0x400000:
            ti=v1-base
            nameva=q(ti+8)
            if nameva and base<=nameva<base+0x400000:
                nm=cstr(nameva-base)
                if nm:
                    print('  vtable at',hex(a+16),'slot',(R-(a+16))//8,'type',nm)
                    break
