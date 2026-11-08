#!/usr/bin/env python3
"""Reconstruct owner-local Emulator X3 banks from independent research vectors.
No original installer or bank payload is ever checked into 125A-Z-Plane-FX.
Use only for authorized local testing; not a redistribution license.
"""
from pathlib import Path
import argparse,bisect,collections,hashlib,json,struct,sys
ap=argparse.ArgumentParser()
ap.add_argument("--research",required=True,type=Path)
ap.add_argument("--out",required=True,type=Path)
args=ap.parse_args()
root=args.research.resolve()
sys.path.insert(0,str(root))
from emux_filters import FactoryBank,decode_word,f32,SAMPLE_RATES
from emux_extended import ExtendedBank,STANDARD_NAMES
args.out.mkdir(parents=True,exist_ok=True)
vectors=json.loads((root/"research/extended-vectors.json").read_text(encoding="utf-8"))
reference=(root/"research/cpp-vectors.bin").read_bytes()
cases={}; pos=4
total=struct.unpack_from("<I",reference,0)[0]
assert total==924
for case in range(total):
    index,rate,freq,res=struct.unpack_from("<IIff",reference,pos);pos+=16
    coefs=struct.unpack_from("<30f",reference,pos);pos+=120
    pos+=128*4
    cases[(index,rate,freq,res)]=[coefs[i:i+5] for i in range(0,30,5)]
assert len(cases)==924
decoded=sorted((decode_word(w),w) for w in range(65536))
values=[p[0] for p in decoded]
inverse={struct.pack("<f",v):w for v,w in decoded}
assert len(inverse)==65536
def recover(combined,other):
    desired=struct.pack("<f",combined)
    center=f32(f32(combined-other)*.25)
    k=bisect.bisect_left(values,center)
    candidates=[w for v,w in decoded[max(0,k-50):min(65536,k+50)]
                if struct.pack("<f",f32(f32(4.*v)+other))==desired]
    assert len(candidates)==1,("Ambiguous packed coefficient",combined,other)
    return candidates[0]
factory=bytearray()
for index in range(33):
  for rate in SAMPLE_RATES:
    for section in range(6):
      for freq,res in ((0.,0.),(1.,0.),(0.,1.),(1.,1.)):
        a,b,c,d,g=cases[index,rate,freq,res][section]
        words=[recover(a,b),inverse[struct.pack("<f",b)],
               recover(c,d),inverse[struct.pack("<f",d)],
               inverse[struct.pack("<f",f32(g*.25))]]
        factory+=struct.pack("<5H",*words)
assert len(factory)==31680
fb=FactoryBank(factory)
for (index,rate,freq,res),original in cases.items():
    actual=fb.coefficients(index,freq,res,rate)
    assert all(struct.pack("<5f",*a)==struct.pack("<5f",*b) for a,b in zip(actual,original))
standard=bytearray(); filters=[]
for name in STANDARD_NAMES:
    rates=[]
    for ri in range(4):
        matches=[v["frames"] for v in vectors if v["label"]==[name,ri]]
        assert matches and all(x==matches[0] for x in matches)
        rates.append(matches[0])
        for frame in matches[0]:
            for section in frame:
                standard+=struct.pack("<5H",*section)
    filters.append({"name":name,"frames":rates,
      "gain_factor":5. if name in STANDARD_NAMES[8:11] else 4.})
assert len(standard)==4960
# The 4x base/slope values were uniquely recovered from reference vectors.
support={
 "morph_base":[4896,4500,900,220],
 "morph_slope":[442,440,405,350],
 "designer_base":[18,18,4,1],
 "designer_slope":[220,220,200,177],
 "shelf_base":[18,18,4,1],
 "shelf_slope":[220,220,200,177],
 "resonance_scale":-63., "index_scale":16.,
 "lp_morph":[], "expression":[], "neutral":[57343,65535,57343,65535,57344]
}
for idx in range(16):
 for name,key in (("Dual EQ + LP Morph","lp_morph"),
                  ("Dual EQ Morph/Expression","expression")):
    matches=[]
    for v in vectors:
       if v["label"][0]!=name:continue
       if min(15,int(f32(f32(v["resonance"])*16.)))!=idx:continue
       frames=v["frames"]
       words=frames[0][0][2:5]+(frames[1][0][2:5] if key=="lp_morph" else [])
       matches.append(tuple(words))
    assert matches and len(set(matches))==1,(key,idx)
    support[key].extend(matches[0])
assert len(support["lp_morph"])==96 and len(support["expression"])==48
from emux_filters import SUPPORTED_SHA256
full={"format":"emux-extended-v1","source_sha256":SUPPORTED_SHA256,
      "filters":filters,"support":support}
bank=ExtendedBank(full)
for v in vectors:
  name,ri,*rest=v["label"]
  rate=SAMPLE_RATES[ri];frequency=v["frequency"];res=v["resonance"]
  if name in STANDARD_NAMES:
    actual=bank.frames(name,rate)
    coefs=bank.coefficients(name,frequency,res,rate)
  elif name=="Morph Designer":
    actual=bank.designer_frames(rest[0],res,rate)
    coefs=bank.designer_coefficients(rest[0],frequency,res,rate)
  else:
    actual=bank.morph_frames(name,rest[0],res,rate)
    coefs=bank.morph_coefficients(name,rest[0],frequency,res,rate)
  assert actual==v["frames"],("Frames",v["label"])
  assert all(struct.pack("<5f",*a)==struct.pack("<5f",*b)
             for a,b in zip(coefs,v["coefficients"])),("Coefficients",v["label"])
binary=bytearray()
for key,fmt in (("morph_base","4I"),("morph_slope","4I"),("lp_morph","96H"),
                ("expression","48H"),("shelf_base","4I"),("shelf_slope","4I"),
                ("neutral","5H"),("designer_base","4I"),("designer_slope","4I"),
                ("resonance_scale","f"),("index_scale","f")):
    v=support[key] if isinstance(support[key],list) else [support[key]]
    binary+=struct.pack("<"+fmt,*v)
assert len(binary)==402
for name,payload,expected in (
 ("factory-bank.bin",factory,"a630fc5cbe06019413b26266abced6b70ed248c601c3b2e5ff80550425498d2e"),
 ("standard-bank.bin",standard,"1dcc23515bf64e84ef99362ca0019ff9256bc1df33b2e3ab355d7facf4a12b58"),
 ("morph-support.bin",binary,"ce5f9b8411e86a5d21d6a93064811c8c6eda91548f62e871643754528c87c215")):
    digest=hashlib.sha256(payload).hexdigest()
    assert digest==expected,(name,digest)
    (args.out/name).write_bytes(payload)
    print(name,len(payload),digest)
print("Validated 924/924 factory and",len(vectors),"/3156 extended original references.")
