#!/usr/bin/env python3
"""Extract inert test bytes from our existing capture; never opens a GPU device."""
import hashlib,json,pathlib,struct,sys
src=pathlib.Path(sys.argv[1]);out=pathlib.Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
records={};provenance={}
for line in src.open():
 r=json.loads(line)
 if r['edge']=='before':records[r['id']]=r;continue
 q=records.pop(r['id'])
 if q['nr']!=42:continue
 cmd=struct.unpack_from('<I',bytes.fromhex(q['outer']),8)[0];name=None
 if cmd==0x2080a630:name='policy.bin'
 elif cmd==0x20801801:name='pci.bin'
 elif cmd==0x2080a618:name='policy_info.bin'
 elif cmd==0x20800808:name='bios.bin'
 elif cmd==0x20800301:
  index=struct.unpack_from('<I',bytes.fromhex(q['params']),0)[0]
  name=f'notify-{index}.bin'
 elif cmd==0x13b:
  sub=struct.unpack_from('<I',bytes.fromhex(q['params']),16)[0]
  if sub==2:name='supported.bin'
  elif sub==4:name='static.bin'
  elif sub==3:
   after=bytes.fromhex(r['params']);ctgp=struct.unpack_from('<i',after,20)[0];boost=struct.unpack_from('<i',after,28)[0]
   name={ (0,0):'quiet.bin',(55000,0):'balanced.bin',(55000,25000):'performance.bin'}.get((ctgp,boost))
 elif cmd==0x2080e634:
  mw=struct.unpack_from('<I',bytes.fromhex(q['params']),12)[0]
  if mw in (95000,150000,175000):name=f'set-{mw}.bin'
 if not name or name in provenance:continue
 b=bytes.fromhex(q['params'] if name.startswith(('set-','notify-')) else r['params']);(out/name).write_bytes(b)
 provenance[name]={'capture_id':q['id'],'cmd':hex(cmd),'sha256':hashlib.sha256(b).hexdigest(),'source':'powerd-Mitschnitt-Ubuntu-2026-11-08/ioctl.jsonl'}
(out/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
assert len(provenance)==16,provenance.keys()
