#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build only a NEW regular v1.0/v217 image; preserve old images and disks."""
from pathlib import Path
p=Path(__file__).with_name('package-a733-voice-v211.py');s=p.read_text()
s=s.replace('v211','v217').replace('V211_','V217_').replace('release=211','release=217')
s=s.replace('def voice_checks(image,report):','def voice_checks(image,report,firmware_commit):')
s=s.replace('fixtures,fat_end=voice_checks(image,report)','fixtures,fat_end=voice_checks(image,report,a.firmware_commit)')
needle="        dest='::/npu/general-asr/checks'"
assert s.count(needle)==1
s=s.replace(needle,"""        soul=repo/'assets/aipet-online/SOUL.md'
        assert subprocess.check_output(['git','show',firmware_commit+':assets/aipet-online/SOUL.md'],cwd=repo)==soul.read_bytes()
        run(['mcopy','-o','-i',fat,soul,'::/ai_agent/config/SOUL.md'])
        check=tmp/'persona';run(['mcopy','-i',fat,'::/ai_agent/config/SOUL.md',check]);assert old.digest(check)==old.digest(soul)
        model=Path(os.environ['A733_PRODUCT_LLM']).resolve(strict=True)
        digest='81b64d05a23b17b34c475f42b3e72fbde62d4b92cc34541f7a8031d0752deafa'
        assert model.is_file() and model.stat().st_size==688065920 and old.digest(model)==digest
        if not exists('::/models'):run(['mmd','-i',fat,'::/models'])
        target='::/models/MiniCPM5-1B-Q4_K_M.gguf'
        run(['mcopy','-o','-i',fat,model,target])
        check=tmp/'llm';run(['mcopy','-i',fat,target,check]);assert check.stat().st_size==688065920 and old.digest(check)==digest;check.unlink()
        records.append(dict(path='/data/models/MiniCPM5-1B-Q4_K_M.gguf',bytes=688065920,sha256=digest,kind='private-LLM-SD-backup-UFS-still-preferred'))
        yolo=tmp/'yolo';run(['mcopy','-i',fat,'::/npu/yolov8n-pcq-v3.a7pm',yolo])
        yolo_sha='3c2891ef77ce9a44dd02cd32bdd7db5ec92d14185014e6de8db5803a6cbecce2'
        assert len(yolo_sha)==64 and yolo.stat().st_size==8017024 and old.digest(yolo)==yolo_sha,('frozen YOLO mismatch',old.digest(yolo))
        records.append(dict(path='/data/npu/yolov8n-pcq-v3.a7pm',bytes=8017024,sha256=old.digest(yolo),kind='inherited-frozen-COCO80-six-head-model'))
"""+needle)
s=s.replace('fat_changed_only_for_fixtures=True','fat_changed_for_fixtures_persona_and_private_llm=True')
s=s.replace('models_fat_unchanged=True','models_fat_unchanged=False,private_llm_included=True')
exec(compile(s,str(p),'exec'))
