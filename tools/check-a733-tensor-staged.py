#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""While build staging is active, verify critical tensor/application sources."""
import argparse,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('sdk',type=Path);p.add_argument('--ufs',action='store_true');a=p.parse_args()
repo=Path(__file__).resolve().parent.parent;overlay=repo/'board/a733-cubie-a7z/openvela-overlay'
names=['vendor/allwinnertech/chips/a733/'+n for n in ['a733_npu_runtime.c','a733_tensor_plan.c','a733_tensor_runtime.inc','a733_tensor_crc.inc','a733_tensor_client.c','include/a733_tensor_plan.h']]
names+=['nuttx/include/nuttx/npu/a733_tensor.h','nuttx/include/nuttx/npu/a733_vip2.h','apps/include/system/a733_tensor_client.h']
names+=['apps/system/aipet/'+n for n in ['CMakeLists.txt','audio_app.cxx','voice_npu_check.cxx','voice_npu_check.hxx','voice_npu_stream.inc','voice_kws.c','voice_kws.h']]
names+=['apps/system/aipet/'+n for n in ['voice_kws_input.inc','voice_frontend_audit.inc','voice_fbank.cxx','voice_fbank.hxx']]
names+=['vendor/allwinnertech/chips/a733/'+n for n in ['a733_i2s0_audio.c','a733_i2s0_playback.inc','a733_audio_client.c']]
names+=['nuttx/include/sys/a733_audio.h','apps/include/system/a733_pcm.h']
records={}
if a.ufs:
    names+=['vendor/allwinnertech/chips/a733/'+n for n in ['a733_ufs.c','a733_ufs_wire.h','a733_ufs_link.c','a733_ufs_link.h','a733_ufs_board.c','a733_ufs_board.h','a733_ufs_handoff.c','a733_ufs_handoff.h','a733_ufs_quiesce.c','a733_ufs_quiesce.h','a733_hwdiag.c','CMakeLists.txt','Kconfig','include/a733_peripherals.h']]
    names+=['vendor/allwinnertech/chips/a733/'+n for n in ['a733_ufs_nop.c','a733_ufs_nop.h','a733_ufs_nop_map.c']]
    names+=['vendor/allwinnertech/chips/a733/'+n for n in ['a733_ufs_readcheck.c','a733_ufs_readcheck.h','a733_ufs_scratch.c','a733_ufs_scratch.h','a733_ufs_write_wire.h']]
    names+=['vendor/allwinnertech/chips/a733/'+n for n in ['a733_ufs_data.c','a733_ufs_data.h','a733_boot.c','include/chip.h']]
    names+=['vendor/allwinnertech/chips/a733/a733_ufs_volume.h','apps/include/system/a733_storage.h']
    names+=['apps/system/aipet/'+n for n in ['local_llm.cxx','local_model_path.hxx']]
    names+=['apps/system/a733services/'+n for n in ['a733services_main.c','services_config.c']]
    names+=['vendor/allwinnertech/boards/a733/cubie-a7z/scripts/dramboot.ld']
    names+=['vendor/allwinnertech/boards/a733/cubie-a7z/src/a7z_boardinit.c']
for name in names:
    source=(overlay/name).read_bytes();actual=(a.sdk/name).read_bytes()
    if source!=actual:raise ValueError('Staged source mismatch: '+name)
    records[name]=hashlib.sha256(source).hexdigest()
print(json.dumps(dict(result='STAGED_SOURCES_MATCH',files=records),indent=2))
