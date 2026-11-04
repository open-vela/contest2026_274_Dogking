/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AIPET_VOICE_MODEL_STORAGE_HXX
#define AIPET_VOICE_MODEL_STORAGE_HXX
#include <cerrno>
namespace aipet {
struct VoiceModelRoots {
  const char *asr="/data/npu/general-asr",*tts="/data/npu/general-tts";
  const char *reason="UFS-not-qualified-mounted; SD-default";
};
/* Probe metadata only. Actual loaders STILL validate sizes, CRCs, contracts,
 * and CID. A present but incomplete/bad UFS set is an error, not silent SD
 * fallback. No copying, waiting for mounts, or format/repair. */
inline int voice_model_roots(bool mounted,int (*probe)(const char *),VoiceModelRoots &out) {
  out={};if(!mounted)return 0;
  const char *files[]={
    "/ufs/npu/general-asr/tokens.txt","/ufs/npu/general-asr/asr-encoder.a7tv",
    "/ufs/npu/general-asr/asr-decoder.a7tv","/ufs/npu/general-asr/asr-joiner.a7tv",
    "/ufs/npu/general-tts/tokens.txt","/ufs/npu/general-tts/lexicon.txt",
    "/ufs/npu/general-tts/model-steps-3.basic.ort","/ufs/npu/general-tts/vocoder.a7tv"};
  unsigned present=0,missing=0;
  for(auto file:files) {
    int rc=probe(file);
    if(!rc)++present;
    else if(rc==-ENOENT || rc==-ENOTDIR)++missing;
    else {out.reason="UFS-model-probe-failed; NOT falling back";return rc;}
  }
  if(!present){out.reason="UFS-voice-assets-absent; SD-default";return 0;}
  if(missing){out.reason="UFS-voice-assets-incomplete; NOT falling back";return -ENODATA;}
  out.asr="/ufs/npu/general-asr";out.tts="/ufs/npu/general-tts";
  out.reason="complete-UFS-set; loaders must still validate";return 0;
}
}
#endif
