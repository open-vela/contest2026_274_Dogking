#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Export private, exact-address A7TV from validated A733 Linux voice traces.

Not an NBG converter, relocator or general command safety verifier. Only the
observed single-subtask LOAD_STATE/STALL/END profile is accepted. No NPU calls.
"""
import argparse
import importlib.util
import json
import struct
from pathlib import Path

spec=importlib.util.spec_from_file_location("layout",Path(__file__).with_name("capture-voice-layout.py"))
L=importlib.util.module_from_spec(spec);spec.loader.exec_module(L)
R,sha,crc=L.R,L.sha,L.SK.crc
H=struct.Struct("<12I32s12I")
REGION=struct.Struct("<8I")
TENSOR=struct.Struct("<16I")
PAGE=4096
def align(value,n):return (value+n-1)&~(n-1)
def region_read(path,n,max_bytes=32*1024**2):
    R(path.is_file() and not path.is_symlink() and 0<n<=max_bytes and path.stat().st_size==n,
      "Bad bounded region/model file")
    return path.read_bytes()
def write_json(path,value):
    with path.open("x",encoding="utf-8") as stream:json.dump(value,stream,indent=2,sort_keys=True);stream.write("\n")

def command_audit(data,used,sequential_link_base=None):
    # Source: vip_drv_task_get_cmd_info returns the allocation BASE, not
    # allocation_size-command_size. The extra56 bytes are tail capacity!
    R(len(data)==used+56 and used>0 and not used%8,"Unknown command capacity profile")
    if sequential_link_base is not None:
        R(0<=sequential_link_base<=0xffffffff-len(data) and
          sequential_link_base%8==0,"Invalid sequential FE base")
    words=struct.unpack("<%dI"%(len(data)//4),data);i=0;loads=stalls=links=0
    while i<len(words):
        op=words[i]>>27
        if op==1:
            n=(words[i]>>16)&1023
            R(n>0 and i+1+n<=len(words),"Bad LOAD_STATE span")
            i+=align(1+n,2);loads+=1
        elif op==9:
            R(i+2<=len(words),"Truncated STALL");i+=2;stalls+=1
        elif op==8 and sequential_link_base is not None:
            # VIP2 vip_drv_hardware.c cmd_link emits MAX_CMD_SIZE>>3 in
            # the low bits. The observed general-ASR compiler splits FE
            # fetches with a link to the NEXT instruction in this SAME
            # allocation. No branch, outside target, loop or relocation.
            R(words[i]==0x4000ffff and i+2<len(words) and
              words[i+1]==sequential_link_base+(i+2)*4 and
              words[i+1]%8==0,"Unqualified non-sequential FE LINK")
            i+=2;links+=1
        elif op==2:
            R(words[i:i+2]==(0x10000000,0) and
              words[i-2:i]==(0x08010e01,0x44) and i*4>=used and
              all(v==0 for v in words[i+2:]),"Unknown event/END/tail profile")
            return dict(entry_offset=0,native_command_bytes=used,end_offset=i*4,
                        load_states=loads,stalls=stalls,links=links)
        else:raise ValueError("Unsupported FE opcode %d at %d"%(op,i*4))
    raise ValueError("No END in captured command")

def model(base,report,contracts,fixture,name,max_region=32*1024**2,max_pool=24*1024**2,max_package=32*1024**2,allow_sequential_links=False):
    R(0 < max_region <= 64*1024**2 and 0 < max_pool <= 64*1024**2 and
      0 < max_package <= 64*1024**2,"Unbounded prepared profile")
    folder=report/(name+"-trace");log=L.read(report/(name+".log")).decode()
    R(L.SK.metadata(log)==contracts[name] and "process-exit=0 tee-exit=0" in log,"Native run/metadata failed")
    files=L.trace_manifest(folder,max_region)
    R(files==json.loads(L.read(report/(name+"-trace-manifest.json"))),"Trace manifest changed")
    records=[json.loads(s) for s in L.read(folder/"trace.jsonl").decode().splitlines()]
    final=[r for r in records if r.get("kind")=="final"]
    R(len(final)==1 and final[0]["failed"]==0 and final[0]["snapshots"]==2,"Failed/non-single trace")
    payloads=[r for r in records if r.get("kind")=="payload"]
    def payload(command,phase):return [(r,L.read(folder/r["file"])) for r in payloads if r["command"]==command and r["phase"]==phase]
    returns=[r for r in records if r.get("kind")=="return"]
    R(all(r["rc"]==0 and r["error"]==0 for r in returns),"Native ioctl failure")
    tasks=payload(27,"post");submits=payload(5,"pre");waits=payload(4,"post")
    R(len(tasks)==len(submits)==len(waits)==1,"Need one task/submit/wait")
    task=struct.unpack("<I16sI",tasks[0][1]);submit=struct.unpack("<9I",submits[0][1]);wait=struct.unpack("<2I",waits[0][1])
    R(task[2]==1 and submit==(task[0],0,1,0,0,1,0,5000,1) and wait==(task[0],0),"Unknown task/core/timeout profile")
    props=[(r,struct.unpack("<9I",d)) for r,d in payload(28,"pre") if r["seq"]<submits[0][0]["seq"]]
    R(len(props)==1,"Extra init/preload/patch task dependency")
    prop=props[0][1]
    R(prop[:4]==(task[0],0,0,0) and prop[6:]==(0,0,0),"Patched/multi command task")
    command_id,used=prop[4:6]
    before=[r for r in records if r.get("kind")=="snapshot" and r["phase"]=="pre-submit"]
    after=[r for r in records if r.get("kind")=="snapshot" and r["phase"]=="post-wait"]
    R(len(before)==len(after)<=128 and len({r["id"] for r in before})==len(before),"Snapshot regions")
    before.sort(key=lambda r:int(r["vip"],16));post={r["id"]:r for r in after}
    inputs=[];outputs=[]
    for i,entry in enumerate(fixture["models"][name]["inputs"]):
        data=L.read(base/entry["path"],contracts[name]["input"][i]["bytes"])
        R(sha(data)==entry["sha256"],"Golden input changed");inputs.append(data)
    for i,entry in enumerate(fixture["models"][name]["outputs"]):
        data=L.read(report/(name+"-output")/("output%d.bin"%i),contracts[name]["output"][i]["bytes"])
        L.SK.floats(data);R(sha(data)==entry["sha256"],"Golden output changed");outputs.append(data)
    bound=L.bindings(log,folder,records,inputs,outputs,max_region)
    R(bound==json.loads(L.read(report/(name+"-bindings.json"))),"I/O bindings changed")
    data=[];pool=0;windows=set();command_index=None
    for i,r in enumerate(before):
        vip=int(r["vip"],16);n=r["bytes"];rounded=align(n,PAGE)
        R(n>0 and not vip%PAGE and vip+rounded<=0xffffffff,"Bad region address")
        R(not(vip<=0x01001000<vip+rounded),"Internal RTOS init page conflict")
        if i:R(int(before[i-1]["vip"],16)+align(before[i-1]["bytes"],PAGE)<=vip,"Page overlap")
        R(r["id"] in post and all(post[r["id"]][k]==r[k] for k in ("id","vip","logical","bytes")),"Region changed during run")
        data.append(region_read(folder/r["file"],n,max_region));pool+=rounded
        windows.update(range(vip>>24,((vip+rounded-1)>>24)+1))
        if r["id"]==command_id:command_index=i
    R(command_index is not None,"Command snapshot missing")
    cmd=command_audit(data[command_index],used,
        int(before[command_index]['vip'],16) if allow_sequential_links else None)
    tail=int(before[command_index]["vip"],16)+align(len(data[command_index]),PAGE)
    R(all(not(int(r["vip"],16)<=tail<int(r["vip"],16)+align(r["bytes"],PAGE)) for r in before),"END page conflict")
    R(tail!=0x01001000 and tail+PAGE<=0xffffffff and pool+PAGE<=max_pool,"Runtime pool/tail bound")
    windows.update((tail>>24,1));R(len(windows)<=16,"MMU window bound")
    desc=[]
    io_regions=set()
    for kind,buffers in (("input",inputs),("output",outputs)):
        out=[]
        for i,(binding,raw) in enumerate(zip(bound[kind],buffers)):
            matches=[j for j,r in enumerate(before) if r["file"]==binding["file"]] if kind=="input" else [j for j,r in enumerate(before) if post[r["id"]]["file"]==binding["file"]]
            R(len(matches)==1 and matches[0]!=command_index,"I/O region missing/command alias")
            j=matches[0];io_regions.add(j);meta=contracts[name][kind][i]
            out.append((j,binding["allocation_offset"],len(raw),crc(raw),meta["format"],meta["quant"],len(meta["dims"]),*meta["dims"],*([0]*(4-len(meta["dims"]))),*([0]*5)))
        desc.append(out)
    # Native bindings enforce disjoint application tensors, including slices
    # in the same allocation; never silently lose private padding bytes.
    slices=[]
    for d in desc[0]+desc[1]:
        R(all(d[0]!=e[0] or d[1]+d[2]<=e[1] or e[1]+e[2]<=d[1] for e in slices),"Overlapping tensor slices")
        slices.append(d)
    cursor=align(H.size+len(before)*REGION.size+(len(inputs)+len(outputs))*TENSOR.size,64)
    regions=[];payload=[]
    out_regions={d[0] for d in desc[1]}
    for i,(r,raw) in enumerate(zip(before,data)):
        flags=1|(2 if i in out_regions else 0)|(4 if i==command_index else 0)
        regions.append((int(r["vip"],16),cursor,len(raw),crc(raw),flags,0,0,0));payload.append((cursor,raw));cursor=align(cursor+len(raw),64)
    R(cursor<=max_package,"Package file bound")
    label=task[1].split(b"\0")[0];R(label and len(label)<32,"Task name")
    values=[0x56543741,1,128,0x1000003b,cursor,len(regions),len(inputs),len(outputs),command_index,0,0,0,label.ljust(32,b"\0"),*([0]*12)]
    manifest=H.pack(*values)+b"".join(REGION.pack(*d) for d in regions)+b"".join(TENSOR.pack(*d) for group in desc for d in group)
    values[10]=crc(manifest);manifest=H.pack(*values)+manifest[H.size:]
    image=bytearray(cursor);image[:len(manifest)]=manifest
    for offset,raw in payload:image[offset:offset+len(raw)]=raw
    return bytes(image),dict(model=name,command=cmd,regions=len(regions),pool_bytes=pool+PAGE,mmu_windows=len(windows),
       model_sha256=sha(region_read(base/fixture["models"][name]["model"],(base/fixture["models"][name]["model"]).stat().st_size,max_region)),trace_json_sha256=sha(L.read(folder/"trace.jsonl")),
       inputs=[dict(bytes=len(v),sha256=sha(v)) for v in inputs],outputs=[dict(bytes=len(v),sha256=sha(v)) for v in outputs],
       exact_reference_addresses=True,relocated=False,hardware_replay_passed=False,openvela_passed=False)

def export(base,report,target):
    R(not target.exists(),"Use a new output directory")
    contracts,fixture=L.check(base);summary=json.loads(L.read(report/"layout-summary.json"))
    R(summary["result"]=="NATIVE_GOLDENS_AND_LAYOUT_CAPTURED","Unaccepted capture")
    # Validate ALL models in memory before creating any output artifact.
    models=[model(base,report,contracts,fixture,name) for name in ("encoder","decoder","joiner","vocoder")]
    target.mkdir();index=[]
    for image,meta in models:
        path=target/(meta["model"]+".a7tv")
        with path.open("xb") as stream:stream.write(image)
        meta.update(package=path.name,package_bytes=len(image),package_sha256=sha(image));index.append(meta)
    write_json(target/"export-index.json",dict(schema=1,cid="1000003b",models=index,private_vendor_derived=True,
               source_report=report.name,source_layout_sha256=sha(L.read(report/"layout-summary.json")),
               generic_nbg_converter=False,security_sandbox=False))
    return index

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("kit",type=Path);p.add_argument("report",type=Path);p.add_argument("new_output",type=Path);a=p.parse_args()
    try:
        for m in export(a.kit,a.report,a.new_output):print(m["model"],m["package_bytes"],m["package_sha256"],m["command"])
    except (ValueError,OSError,KeyError,struct.error) as e:p.exit(2,"STOP: "+str(e)+"\n")
