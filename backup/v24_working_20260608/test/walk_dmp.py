import struct, sys

d = open(sys.argv[1], "rb").read()
sig, ver, nstreams, dirrva = struct.unpack_from("<IIII", d, 0)
streams = {}
o = dirrva
for i in range(nstreams):
    st, ds, rva = struct.unpack_from("<III", d, o); streams[st] = (ds, rva); o += 12

def mdstr(rva):
    (ln,) = struct.unpack_from("<I", d, rva)
    return d[rva+4:rva+4+ln].decode("utf-16-le", "replace")

# modules
mods = []
_, rva = streams[4]
(nmod,) = struct.unpack_from("<I", d, rva); base = rva+4
for i in range(nmod):
    mo = base + i*108
    b, s, c, t, nr = struct.unpack_from("<QIIII", d, mo)
    mods.append((b, s, mdstr(nr)))
mods.sort()
def modof(a):
    for b, s, n in mods:
        if b <= a < b+s: return (n.split("\\")[-1], a-b)
    return (None, None)

# exception thread + addr
_, er = streams[6]
tid, _ = struct.unpack_from("<II", d, er)
ecode, fl, rec, eaddr, npar = struct.unpack_from("<IIQQI", d, er+8)
print("Exc 0x%08X @ %s" % (ecode, "%s+0x%X" % modof(eaddr)))

# thread list -> find stack mem of crashing thread
_, tr = streams[3]
(nth,) = struct.unpack_from("<I", d, tr); tb = tr+4
stack = None
for i in range(nth):
    to = tb + i*48
    t_id, susp, pcl, pri, teb, stk_start, stk_size, stk_rva = struct.unpack_from("<IIIIQQII", d, to)
    if t_id == tid:
        stack = (stk_start, stk_size, stk_rva)
print("crash thread stack bytes:", stack[1] if stack else None)

# scan stack for return addresses inside modules
seen = []
start, size, srva = stack
mem = d[srva:srva+size]
last = None
for off in range(0, len(mem)-8, 8):
    (val,) = struct.unpack_from("<Q", mem, off)
    n, o2 = modof(val)
    if n:
        line = "%s+0x%X" % (n, o2)
        if line != last:
            seen.append(line); last = line

# print only non-system frames across the WHOLE stack
SYS = ("ntdll.dll","kernelbase.dll","kernel32.dll","dbgcore.dll","ucrtbase.dll",
       "dbghelp.dll","msvcp","vcruntime","combase.dll","rpcrt4.dll","user32.dll",
       "win32u.dll","gdi32","sechost","msvcrt.dll","ole32.dll")
def issys(s):
    s = s.lower()
    return any(s.startswith(k) for k in SYS)

print("\n=== non-system frames (whole stack) ===")
for s in seen:
    if not issys(s):
        mark = "   <== PLUGIN" if "VMTHexTilePoc" in s else ""
        print(s + mark)

print("\nVMTHexTilePoc on stack:", any("VMTHexTilePoc" in s for s in seen))
