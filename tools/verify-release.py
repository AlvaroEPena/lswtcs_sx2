#!/usr/bin/env python3
"""Verify the normal NRO's platform and absence of port capture machinery."""
import hashlib
import json
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]


def symbols(path):
    raw=path.read_bytes()
    if raw[:6]!=b'\x7fELF\x02\x01':
        raise ValueError('Expected an ELF64 little-endian host executable')
    offset=struct.unpack_from('<Q',raw,40)[0]
    size,count=struct.unpack_from('<HH',raw,58)
    sections=[struct.unpack_from('<IIQQQQIIQQ',raw,offset+i*size) for i in range(count)]
    result=set()
    for section in sections:
        if section[1] not in (2,11):continue
        strings=sections[section[6]]
        names=raw[strings[4]:strings[4]+strings[5]]
        for pos in range(section[4],section[4]+section[5],24):
            name,_,_,index,_,_=struct.unpack_from('<IBBHQQ',raw,pos)
            if index:result.add(names[name:names.find(b'\0',name)].decode('utf-8','replace'))
    return result


def main():
    nro=ROOT/'lswtcs-sx2.nro'
    raw=nro.read_bytes()
    assert raw[16:20]==b'NRO0','Invalid NRO header'
    assert b'OUN0'+struct.pack('<II',2,1) in raw[:4096],'Missing native Switch 2 platform record'
    names=symbols(ROOT/'lswtcs-sx2.elf')
    forbidden_prefixes=('load_trace_','flight_','thread_samples_','crash_report_',
                        'diag_io_','gl_trace_','guest_malloc_recorded','guest_calloc_recorded',
                        'guest_realloc_recorded','guest_free_recorded')
    found=sorted(name for name in names if name.startswith(forbidden_prefixes))
    assert not found,found
    assert '__wrap_pthread_create' not in names,'Thread-sampling wrapper still linked'
    for name in ('NuVecXZDistSqr_hub_safe','Players_Init_restore_control',
                 'port_menu_handle_input','port_menu_frame_presented','port_overlay_draw'):
        assert name in names,'Required fix/menu symbol missing: '+name
    for token in (b'LSWFLT01',b'LSWSAMP1',b'LSWCRSH1',b'debug.log',
                  b'runtime-errors.log',b'flight-recorder.bin',b'thread-samples.bin',b'crash-report.bin'):
        assert token not in raw,'Capture marker/path remains in NRO: '+repr(token)
    link=(ROOT/'build/lswtcs-sx2.map').read_text(encoding='utf-8')
    assert '/portlibs/switch2/lib/libouncevk-gl.a' in link
    assert '/libounce/lib/libounce_core.a' in link
    print(json.dumps({'version':'0.4.0','bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),
                      'switch2_platform_verified':True,'capture_machinery_absent':True,
                      'save_load_fixes_and_menu_present':True},indent=2))


if __name__=='__main__':main()
