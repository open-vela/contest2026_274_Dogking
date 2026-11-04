#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import importlib.util,re,unittest
from pathlib import Path
def module(name,path):
    s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
root=Path(__file__).resolve().parent
a=module('wav_audit',root/'npu-voice/audit-openvela-kws-wav.py')
baseline=module('profile_test',root/'test-openvela-npukws-profile.py')
class Test(unittest.TestCase):
    def make(self):
        e,t=baseline.Test().make();manifest=dict(cases=[])
        t=t.replace('validation=all-native-CRCs','validation=all-native-CRCs driver-crc=slice8 app-crc=slice8')
        parts=t.split('npukws: cpu-policy=')[1:];out=[]
        for i,b in enumerate(parts):
            frames=210 if i<4 else 332;chunks=12 if i<4 else 19;tail=frames-(29+(chunks-1)*16)
            manifest['cases'].append(dict(case=i,frames=frames,feature_crc32='abcdef12'))
            b=re.sub(r'npukws: status=0 calls=50 hits=0 chain=12345678 elapsed-us=999; resident released',
              'npukws-input: status=0 calls=50 candidates=0 known=50 unknown=0 chain=12345678 feature-crc=abcdef12 frames=%d unused-tail=%d elapsed-us=999; resident released; acoustic accuracy NOT accepted'%(frames,tail),b)
            out.append('npukws-input: buffered frontend PCM=16000/mono/S16 frames=%d bins=80; capture already stopped; NOT live streaming\nnpukws: cpu-policy='%frames+b)
        return e,manifest,''.join(out)
    def test_valid(self):self.assertTrue(a.audit(*self.make())['wav_frontend'])
    def test_unknown(self):
        e,m,t=self.make()
        with self.assertRaises(ValueError):a.audit(e,m,t.replace('known=50 unknown=0','known=49 unknown=1',1))
    def test_crc(self):
        e,m,t=self.make()
        with self.assertRaises(ValueError):a.audit(e,m,t.replace('feature-crc=abcdef12','feature-crc=00000000',1))
    def test_frontend_missing(self):
        e,m,t=self.make()
        with self.assertRaises(ValueError):a.audit(e,m,t.replace('buffered frontend','missing',1))
    def test_failed(self):
        e,m,t=self.make()
        with self.assertRaises(ValueError):a.audit(e,m,t.replace('npukws-input: status=0','npukws-input: status=-5',1))
if __name__=='__main__':unittest.main()
