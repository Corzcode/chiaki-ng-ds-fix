# SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
# Minidump probe: parse exception record + faulting thread context + module map.
# Usage: python minidump_probe.py <path.dmp> [out.txt]
#   - Prints exception code/address, faulting thread RIP/RSP/FP, module list.
#   - Writes the same output to <dmp>.probe.txt by default (avoids console
#     encoding mangling when invoked from PowerShell).
#   - No third-party deps (pure struct parsing), so it can be kept as a
#     permanent diagnostic tool under scripts/.
import struct, sys, os

MDMP_MAGIC = b'MDMP'
AMD64 = 0x00000009
CONTEXT_AMD64 = 0x00100000

# MINIDUMP_STREAM_TYPE constants we care about
ST_THREAD_LIST = 3
ST_MODULE_LIST = 4
ST_MEMORY_LIST = 5
ST_EXCEPTION = 6
ST_SYSTEM_INFO = 7


def read(data, fmt, off):
    sz = struct.calcsize(fmt)
    if off < 0 or off + sz > len(data):
        raise ValueError(f'truncated: {fmt} @ 0x{off:x}')
    return struct.unpack_from(fmt, data, off)


def parse(path):
    lines = []
    def P(s=''):
        lines.append(str(s))

    with open(path, 'rb') as f:
        data = f.read()
    P(f'=== minidump probe: {path} ({len(data)} bytes) ===')

    if data[0:4] != MDMP_MAGIC:
        P('NOT a minidump'); return lines
    num_streams, dir_rva = read(data, '<II', 8)
    streams = {}
    for i in range(num_streams):
        off = dir_rva + i * 12
        st, sz, rva = read(data, '<III', off)
        streams[st] = (sz, rva)

    # ---- SystemInfo (7): processor arch ----
    arch = None
    if ST_SYSTEM_INFO in streams:
        sz, rva = streams[ST_SYSTEM_INFO]
        arch = read(data, '<H', rva)[0]
        P(f'processor architecture = 0x{arch:x} (9 = AMD64)')

    # ---- ExceptionStream (6) ----
    eaddr = ecode = ethread = None
    if ST_EXCEPTION in streams:
        sz, rva = streams[ST_EXCEPTION]
        ethread = read(data, '<I', rva)[0]
        ecode = read(data, '<I', rva + 8)[0]     # ExceptionCode
        eaddr = read(data, '<Q', rva + 24)[0]    # ExceptionAddress
        nparams = read(data, '<I', rva + 8 + 20)[0]
        params = read(data, f'<{nparams}Q', rva + 8 + 24)
        P(f'EXCEPTION threadid={ethread} code=0x{ecode:08x} addr=0x{eaddr:016x}')
        P(f'  number_parameters={nparams} params=[{", ".join(f"0x{p:x}" for p in params)}]')
        # ThreadContext location after ExceptionRecord (152 bytes)
        ctx_off = rva + 8 + 152
        ctx_sz, ctx_rva = read(data, '<II', ctx_off)
        P(f'  thread context rva=0x{ctx_rva:x} size={ctx_sz}')
    else:
        P('no exception stream')

    # ---- ThreadList (3) ----
    if ST_THREAD_LIST in streams:
        sz, rva = streams[ST_THREAD_LIST]
        n = read(data, '<I', rva)[0]
        P(f'threads={n}')
        for t in range(n):
            off = rva + 4 + t * 48  # MINIDUMP_THREAD = 48 bytes, array follows count at +4
            tid = read(data, '<I', off)[0]
            ctx_sz, ctx_rva = read(data, '<II', off + 40)  # ThreadContext @ +40
            rip = rsp = rbp = 0
            tag = ''
            if ctx_sz >= 256 and ctx_rva and ctx_rva + 256 <= len(data):
                rip = read(data, '<Q', ctx_rva + 248)[0]  # RIP at CONTEXT+248
                rsp = read(data, '<Q', ctx_rva + 152)[0]  # RSP at CONTEXT+152
                rbp = read(data, '<Q', ctx_rva + 160)[0]  # RBP at CONTEXT+160
                if tid == ethread:
                    tag = '  <== FAULTING'
            P(f'  thread {t}: tid={tid} rip=0x{rip:016x} rsp=0x{rsp:016x} rbp=0x{rbp:016x}{tag}')
    else:
        P('no thread list')

    # ---- ModuleList (4) ----
    if ST_MODULE_LIST in streams:
        sz, rva = streams[ST_MODULE_LIST]
        n = read(data, '<I', rva)[0]
        P(f'modules={n}')
        for m in range(min(n, 500)):
            off = rva + 4 + m * 108  # MINIDUMP_MODULE = 108 bytes, array follows count at +4
            base = read(data, '<Q', off)[0]
            msize = read(data, '<I', off + 8)[0]
            name_rva = read(data, '<I', off + 20)[0]  # ModuleNameRva
            nlen = 0
            if 0 < name_rva < len(data) - 4:
                nlen = read(data, '<I', name_rva)[0]
            if nlen <= 0 or name_rva + 4 + nlen > len(data):
                name = f'<bad name_rva=0x{name_rva:x}>'
            else:
                name = data[name_rva + 4: name_rva + 4 + nlen].decode('utf-16le', 'replace')
            marker = ''
            if eaddr is not None and base <= eaddr < base + msize:
                marker = f'  <== EXCEPTION RVA=0x{eaddr - base:x}'
            P(f'  0x{base:016x} +0x{msize:08x} {name}{marker}')
    else:
        P('no module list')

    # ---- MemoryList (5): find region covering a given address, then dump stack ----
    if ST_MEMORY_LIST in streams and ethread:
        sz, rva = streams[ST_MEMORY_LIST]
        n = read(data, '<I', rva)[0]
        P(f'memory_regions={n}')
        regions = []
        for i in range(min(n, 2000)):
            off = rva + 4 + i * 16  # MINIDUMP_MEMORY_DESCRIPTOR = 16 bytes
            start, dsz, drva = read(data, '<QII', off)
            regions.append((start, dsz, drva))
        # find the faulting thread's stack region (contains RSP from exception ctx)
        # re-read exception ctx RSP
        if ST_EXCEPTION in streams:
            sz6, rva6 = streams[ST_EXCEPTION]
            ctx_sz, ctx_rva = read(data, '<II', rva6 + 8 + 152)
            if ctx_rva and ctx_sz >= 256:
                rsp = read(data, '<Q', ctx_rva + 152)[0]
                rip = read(data, '<Q', ctx_rva + 248)[0]
                P(f'stack unwind: RIP=0x{rip:016x} RSP=0x{rsp:016x}')
                # dump the region containing RSP (if present)
                for start, dsz, drva in regions:
                    if start <= rsp < start + dsz:
                        P(f'  stack region @0x{start:x} size=0x{dsz:x} file_off=0x{drva:x} rsp_off=0x{rsp-start:x}')
                        # walk up to 96 candidate return addresses from RSP upward
                        base_off = rsp - start
                        for j in range(0, min(dsz - base_off, 4096), 8):
                            a = read(data, '<Q', drva + base_off + j)[0]
                            if 0x7ffe00000000 <= a <= 0x7fffffffffff:
                                P(f'    [rsp+0x{j:x}]: 0x{a:016x}')
                        break
                else:
                    P('  (RSP not covered by any memory region)')
    P('=== done ===')
    return lines


def main():
    if len(sys.argv) < 2:
        print('usage: minidump_probe.py <dump.dmp> [out.txt]', file=sys.stderr)
        sys.exit(1)
    path = sys.argv[1]
    outfile = sys.argv[2] if len(sys.argv) > 2 else path + '.probe.txt'
    lines = parse(path)
    with open(outfile, 'w', encoding='utf-8', errors='replace') as f:
        f.write('\n'.join(lines) + '\n')
    print(f'wrote {outfile}')
    # also echo to console (best effort)
    for ln in lines:
        try:
            sys.stdout.write(ln + '\n')
        except Exception:
            break


if __name__ == '__main__':
    main()
