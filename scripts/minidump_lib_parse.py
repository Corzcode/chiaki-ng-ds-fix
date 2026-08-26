# SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
# Parse a Windows minidump using the pip 'minidump' library (minidump 0.0.24 API).
# Usage: python minidump_lib_parse.py <dump.dmp>
# Outputs exception records, thread count, module map and resolution of the
# exception address to a module.
import sys
from minidump.minidumpfile import MinidumpFile

def out(s):
    print(s)

def parse(path):
    md = MinidumpFile.parse(path)
    out(f'=== minidump(lib) parse: {path} ===')

    # Exception stream
    eaddr = None
    if md.exception is not None:
        for mes in md.exception.exception_records:
            er = mes.ExceptionRecord
            eaddr = int(er.ExceptionAddress)
            ecode = getattr(er.ExceptionCode, 'value', er.ExceptionCode)
            out(f'EXCEPTION code=0x{int(ecode):08x} addr=0x{eaddr:016x}')

    # Threads
    if md.threads is not None:
        out(f'THREADS count={len(md.threads.threads)}')

    # Modules
    if md.modules is not None:
        out('MODULES:')
        for m in md.modules.modules:
            out(f'  0x{m.baseaddress:016x} +0x{m.size:08x} {m.name}')

    # Resolve exception address
    if eaddr is not None and md.modules is not None:
        for m in md.modules.modules:
            if m.baseaddress <= eaddr < m.baseaddress + m.size:
                out(f'EXCEPTION ADDR 0x{eaddr:x} -> {m.name} rva=0x{eaddr - m.baseaddress:x}')
                break
    out('=== done ===')

if __name__ == '__main__':
    parse(sys.argv[1])
