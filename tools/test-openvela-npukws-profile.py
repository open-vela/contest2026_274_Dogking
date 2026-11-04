#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import importlib.util,unittest
from pathlib import Path
s=importlib.util.spec_from_file_location('profile_audit',Path(__file__).parent/'npu-voice/audit-openvela-npukws-profile.py');a=importlib.util.module_from_spec(s);s.loader.exec_module(a)
class Test(unittest.TestCase):
    def make(self,full=False):
        expected=dict(cases=[dict(tag=str(i),calls=50,hits=0,chain='12345678',names=[]) for i in range(6)]);blocks=[]
        for i in range(6):
            n=12 if i<4 else 19;rows=[]
            for id,count in enumerate([n,1,50-n-1]):
                rows.append('npukws profile[%d]: validate/rebuild/map/clean/hw/check/commit/restore/total-us=10/20/30/40/50/60/70/80/400 clean-bytes=%d tables=%d rebuilt=%d app-check-us=10\n'%(id,count*(24*1024*1024 if full else a.POOLS[id]),count*(262144 if full else 16384),count*a.POOLS[id]))
            blocks.append('npukws: cpu-policy=cpu6 clean=%s validation=all-native-CRCs\nnpukws: resident=3 cold-prepare-us=55\n'%('full' if full else 'bounded')+''.join(rows)+
                'npukws timing: chunks=%d completed=%d sum-us=%d p50-us=100000 p95-us=100000 max-us=100000 over-160ms=0 search-us=1 affinity-restored=1\n'%(n,n,n*100000)+
                'npukws: status=0 calls=50 hits=0 chain=12345678 elapsed-us=999; resident released\n')
        return expected,''.join(blocks)
    def test_bounded(self):self.assertEqual(len(a.audit(*self.make())['cases']),6)
    def test_full(self):self.assertEqual(a.audit(*self.make(True))['cases'][0]['cache_policy'],'full')
    def test_missing(self):
        e,t=self.make()
        with self.assertRaises(ValueError):a.audit(e,t.replace('npukws profile[0]','invalid',1))
    def test_restore(self):
        e,t=self.make()
        with self.assertRaises(ValueError):a.audit(e,t.replace('affinity-restored=1','affinity-restored=0',1))
    def test_clean_policy(self):
        e,t=self.make()
        with self.assertRaises(ValueError):a.audit(e,t.replace('clean=bounded','clean=full',1))
    def test_deadline(self):
        e,t=self.make()
        with self.assertRaises(ValueError):a.audit(e,t.replace('over-160ms=0','over-160ms=1',1))
    def test_crc_missing(self):
        e,t=self.make()
        with self.assertRaises(ValueError):a.audit(e,t,'slice8')
    def test_crc_slice(self):
        e,t=self.make();t=t.replace('validation=all-native-CRCs','validation=all-native-CRCs driver-crc=slice8 app-crc=slice8')
        self.assertEqual(len(a.audit(e,t,'slice8')['cases']),6)
        with self.assertRaises(ValueError):a.audit(e,t,'legacy')
    def test_crc_legacy(self):
        e,t=self.make();t=t.replace('validation=all-native-CRCs','validation=all-native-CRCs driver-crc=legacy app-crc=slice8')
        self.assertEqual(len(a.audit(e,t,'legacy')['cases']),6)
if __name__=='__main__':unittest.main()
