import struct, sys

path = sys.argv[1]
d = open(path, "rb").read()

sig, ver, nstreams, dirrva = struct.unpack_from("<IIII", d, 0)
assert sig == 0x504D444D, "not a minidump"

streams = {}
off = dirrva
for i in range(nstreams):
    stype, dsize, rva = struct.unpack_from("<III", d, off)
    streams[stype] = (dsize, rva)
    off += 12

def read_mdstring(rva):
    (ln,) = struct.unpack_from("<I", d, rva)
    raw = d[rva+4: rva+4+ln]
    return raw.decode("utf-16-le", "replace")

# Exception stream = 6
exc_addr = None
exc_code = None
if 6 in streams:
    _, rva = streams[6]
    tid, _al = struct.unpack_from("<II", d, rva)
    er = rva + 8
    exc_code, flags, rec, exc_addr, nparam = struct.unpack_from("<IIQQI", d, er)
    print("Exception code : 0x%08X" % exc_code)
    print("Exception addr : 0x%016X" % exc_addr)

# Module list = 4
mods = []
if 4 in streams:
    _, rva = streams[4]
    (nmod,) = struct.unpack_from("<I", d, rva)
    base = rva + 4
    MODSIZE = 108
    for i in range(nmod):
        mo = base + i*MODSIZE
        baseimg, sizeimg, chk, tds, namerva = struct.unpack_from("<QIIII", d, mo)
        name = read_mdstring(namerva)
        mods.append((baseimg, sizeimg, name))

mods.sort()
def find_mod(addr):
    for b, s, n in mods:
        if b <= addr < b + s:
            return (n, addr - b)
    return (None, None)

if exc_addr is not None:
    n, offs = find_mod(exc_addr)
    if n:
        print("Faulting module: %s + 0x%X" % (n, offs))
    else:
        print("Faulting module: <not in any loaded module> addr=0x%X" % exc_addr)

print("\n=== modules of interest ===")
for b, s, n in mods:
    ln = n.lower()
    if any(k in ln for k in ("vmthex", "ogs", "render", "ddraw", "nvwgf", "d3d", "graphics", "openmayarender", "foundation", "tbb")):
        print("0x%016X +0x%08X  %s" % (b, s, n))
