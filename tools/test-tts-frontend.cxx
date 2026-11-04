/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_tts_frontend.hxx"
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <vector>
int main(int argc,char **argv)
{
  assert(argc==3);aipet::VoiceTtsFrontend *f=nullptr;
  assert(!aipet::voice_tts_frontend_open(argv[1],argv[2],&f));
  std::vector<std::vector<int64_t>> out;
  assert(!aipet::voice_tts_frontend_text(f,"你好，测试成功。",out) && out.size()==2);
  for(auto &v:out){assert(v.size()%2==1);for(size_t i=0;i<v.size();i+=2)assert(v[i]==1);}
  assert(!aipet::voice_tts_frontend_text(f,"你好🦊",out) && !out.empty());
  std::vector<std::vector<int64_t>> named;
  assert(!aipet::voice_tts_frontend_text(f,"你好未知字符编码一二九四一八",named) && out==named);
  for(auto pair:{std::pair<const char *,const char *>{"MiniCPM","艾姆艾恩艾西皮艾姆"},
      {"OpenAI","欧皮伊恩诶艾"}}) {
    assert(!aipet::voice_tts_frontend_text(f,pair.first,out));
    assert(!aipet::voice_tts_frontend_text(f,pair.second,named) && out==named);
  }
  assert(!aipet::voice_tts_frontend_text(f,"我是MiniCPM系列模型，由团队开发。接下来继续回答你的问题。",out));
  for(const auto &v:out)assert(v.size()<=127);
  for(const char *text:{"Hello world. English TTS test.","ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
       "好帧=397 坏帧=0 字节=3609 owner=0 A7Z=1"}) {
    assert(!aipet::voice_tts_frontend_text(f,text,out));
    for(const auto &v:out)assert(v.size()<=49);
  }
  assert(aipet::voice_tts_frontend_text(f,"\xf0\x9f",out)==-EILSEQ&&out.empty());
  assert(!aipet::voice_tts_frontend_text(f,"你好（小派）测试成功",out));
  assert(!aipet::voice_tts_frontend_text(f,"你好，小派，测试成功",named) && out==named);
  assert(aipet::voice_tts_frontend_text(f,"\xf0\x80\x80\x80",out)==-EILSEQ && out.empty());
  assert(aipet::voice_tts_frontend_text(f,"\xed\xa0\x80",out)==-EILSEQ && out.empty());
  assert(aipet::voice_tts_frontend_text(f,"\xe0\x80\x8a",out)==-EILSEQ && out.empty());
  assert(aipet::voice_tts_frontend_text(f,"\xf0\x80\x80\xa0",out)==-EILSEQ && out.empty());
  assert(!aipet::voice_tts_frontend_text(f,"你好\n测试成功",out) && !out.empty());
  assert(!aipet::voice_tts_frontend_text(f,"123",out) && !out.empty());
  for(auto pair:{std::pair<const char *,const char *>{"1+1=2","一加一等于二"},
        {"-2.5℃","负二点五摄氏度"},{"50%","百分之五零"},
        {"2026-10-25","二零二六年一零月二五日"},{"2*3=6","二乘以三等于六"}}) {
    std::vector<std::vector<int64_t>> expected;
    assert(!aipet::voice_tts_frontend_text(f,pair.first,out));
    assert(!aipet::voice_tts_frontend_text(f,pair.second,expected));assert(out==expected);
  }
  assert(aipet::voice_tts_frontend_text(f,"2026-02-30",out)==-EINVAL && out.empty());
  assert(aipet::voice_tts_frontend_text(f,"1.2.3",out)==-ENOTSUP && out.empty());
  aipet::voice_tts_frontend_close(f);
  for(unsigned frames:{1u,199u,200u,201u,224u,400u,401u}) {
    std::vector<float> mel(80*frames),chunk(16002,999);
    for(unsigned i=0;i<mel.size();++i)mel[i]=float(i);
    unsigned total=0;
    for(unsigned first=0;first<frames;first+=200) {
      unsigned kept=99;assert(!aipet::voice_tts_mel_chunk(mel.data(),frames,first,chunk.data()+1,16000,&kept));
      assert(kept>0 && kept<=200);total+=kept;
      for(unsigned c=0;c<80;++c)for(unsigned t=0;t<200;++t)
        assert(chunk[1+c*200+t]==(t<kept?mel[c*frames+first+t]:-11.4f));
      assert(chunk.front()==999 && chunk.back()==999);
    }
    assert(total==frames);
    total=0;
    for(unsigned first=0;first<frames;) {
      unsigned skip=99,keep=99;
      assert(!aipet::voice_tts_vocoder_window(mel.data(),frames,first,chunk.data()+1,16000,&skip,&keep));
      assert(keep && skip+keep<=200);
      for(unsigned c=0;c<80;c++)for(unsigned t=0;t<keep;t++)
        assert(chunk[1+c*200+skip+t]==mel[c*frames+first+t]);
      total+=keep;first+=keep;
    }
    assert(total==frames); // no duplicated/missing real mel frame at joins
  }
  float pcm[]={-2,-1,0,1,2};unsigned char bytes[10]{};
  assert(!aipet::voice_tts_pcm16(pcm,5,bytes,10));
  assert(bytes[0]==0 && bytes[1]==128 && bytes[6]==255 && bytes[7]==127);
  pcm[2]=NAN;bytes[0]=42;
  assert(aipet::voice_tts_pcm16(pcm,5,bytes,10)==-EILSEQ && bytes[0]==42);
  std::puts("TTS frontend UTF8/OOV/digits/blank/mel-tail/PCM tests PASS");
}
