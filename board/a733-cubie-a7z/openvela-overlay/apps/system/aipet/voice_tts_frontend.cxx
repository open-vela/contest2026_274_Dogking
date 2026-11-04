/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_tts_frontend.hxx"
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <map>
#include <new>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace aipet {
struct VoiceTtsFrontend {
  std::map<std::string, int64_t> token;
  std::map<std::string, std::vector<int64_t>> lexicon;
};
static int utf8(const char *s, std::vector<std::string> &out)
{
  const auto *p = reinterpret_cast<const unsigned char *>(s);
  const std::size_t length=std::strlen(s);
  for (std::size_t at = 0; p[at];) {
    unsigned n = 1, cp = p[at];
    if (cp >= 0xc2 && cp <= 0xdf) { n = 2; cp &= 31; }
    else if (cp >= 0xe0 && cp <= 0xef) { n = 3; cp &= 15; }
    else if (cp >= 0xf0 && cp <= 0xf4) { n = 4; cp &= 7; }
    else if (cp >= 0x80) return -EILSEQ;
    for (unsigned i = 1; i < n; ++i) {
      if(i>=length-at)return -EILSEQ;
      if ((p[at+i] & 0xc0) != 0x80) return -EILSEQ;
      cp = (cp << 6) | (p[at+i] & 63);
    }
    if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) ||
        (n == 4 && cp < 0x10000) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff)) return -EILSEQ;
    if (cp==9 || cp==10 || cp==13 || cp==0xa0 || cp==0x3000) {
      out.emplace_back(" ");at+=n;continue;
    }
    if (cp < 0x20 || cp==0x7f) return -EILSEQ;
    out.emplace_back(s + at, n); at += n;
  }
  return 0;
}
static bool punct(const std::string &s)
{
  static const char *const values[] = {"，","。","！","？","、","：","；",",",".","!","?",":",";","“","”","\"","'","‘","’"};
  for (auto v : values) if (s == v) return true;
  return s.size() == 1 && s[0] != ' ' &&
    !((s[0] >= '0' && s[0] <= '9') || (s[0] >= 'A' && s[0] <= 'Z') ||
      (s[0] >= 'a' && s[0] <= 'z'));
}
static int read_lines(const char *path, bool tokens, VoiceTtsFrontend &f)
{
  int fd = open(path, O_RDONLY | O_NOFOLLOW);
  if (fd < 0) return -errno;
  struct stat st{};
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > 8*1024*1024) {
    close(fd); return -EINVAL;
  }
  FILE *in = fdopen(fd, "rb");
  if (!in) { int e = errno; close(fd); return -e; }
  char line[2048]; unsigned count = 0; int result = 0;
  std::vector<bool> seen(4097,false);
  while (true) {
    long before=std::ftell(in);
    if(!std::fgets(line,sizeof(line),in))break;
    long after=std::ftell(in);
    if(before<0 || after<before || static_cast<unsigned long>(after-before)!=std::strlen(line)) {
      result=-EILSEQ;break;
    }
    if (++count > 100000 || (!std::strchr(line, '\n') && !std::feof(in))) { result=-E2BIG; break; }
    std::size_t n=std::strlen(line);
    while (n && (line[n-1]=='\n' || line[n-1]=='\r')) line[--n]=0;
    if (!n) continue;
    if (tokens) {
      char *space=std::strrchr(line,' '); int64_t id=0;
      if (!space || !space[1]) { result=-EINVAL;break; }
      for (char *p=space+1;*p;++p) {
        if (*p<'0' || *p>'9' || id>4096) { result=-EINVAL;break; }
        id=id*10+*p-'0';
      }
      if (result || id>4096 || seen[id]) { result=-EINVAL;break; }
      seen[id]=true;
      *space=0;
      std::string key = space==line ? " " : line;
      std::vector<std::string> chars;
      if (utf8(key.c_str(),chars) || !f.token.emplace(key,id).second) { result=-EINVAL;break; }
    } else {
      char *p=line;
      while (*p && *p!=' ' && *p!='\t') { if (*p>='A' && *p<='Z') *p+=32; ++p; }
      if (!*p) { result=-EINVAL;break; }
      *p++=0;std::string word=line;std::vector<int64_t> ids;
      bool known=true;
      while (*p) {
        while (*p==' ' || *p=='\t') ++p;
        if (!*p) break;
        char *start=p;while (*p && *p!=' ' && *p!='\t') ++p;
        char end=*p;*p=0;auto found=f.token.find(start);
        if (found==f.token.end()) known=false;
        else ids.push_back(found->second);
        if (ids.size()>128) { result=-E2BIG;break; }
        if (!end) break;
        ++p;
      }
      if (result) break;
      std::vector<std::string> chars;
      if (utf8(word.c_str(),chars)) { result=-EILSEQ;break; }
      if (known && !ids.empty()) f.lexicon.emplace(word,std::move(ids)); /* first pronunciation */
    }
  }
  if (std::ferror(in) && !result) result=-EIO;
  if (std::fclose(in) && !result) result=-errno;
  return result;
}
int voice_tts_frontend_open(const char *tokens,const char *lexicon,VoiceTtsFrontend **out)
{
  if (!out) return -EINVAL;
  *out=nullptr;if (!tokens || !lexicon) return -EINVAL;
  auto *f=new(std::nothrow) VoiceTtsFrontend;
  if (!f) return -ENOMEM;
  int rc=0;
#if defined(__cpp_exceptions)
  try {
#endif
    rc=read_lines(tokens,true,*f);
    if (!rc && (f->token.find("_")==f->token.end() || f->token["_"]!=1)) rc=-EINVAL;
    const char *alias[][2]={{",","，"},{".","。"},{"!","！"},{"?","？"},{":","："},{";","；"},
      {"\"","“"},{"\"","”"},{"'","‘"},{"'","’"},{"，","、"}};
    for (auto &a:alias) {
      auto i=f->token.find(a[0]),j=f->token.find(a[1]);
      if (i!=f->token.end() && j==f->token.end()) f->token.emplace(a[1],i->second);
      else if (j!=f->token.end() && i==f->token.end()) f->token.emplace(a[0],j->second);
    }
    if (!rc) rc=read_lines(lexicon,false,*f);
    if (!rc && f->lexicon.empty()) rc=-ENODATA;
#if defined(__cpp_exceptions)
  } catch (const std::bad_alloc &) { rc=-ENOMEM; }
    catch (const std::exception &) { rc=-EIO; }
#endif
  if (rc) { delete f;return rc; }
  *out=f;return 0;
}
static int prepare(const VoiceTtsFrontend *f,const char *text,
                    std::vector<std::vector<int64_t>> &sentences)
{
  std::vector<std::string> words,cleaned;
  int rc=utf8(text,words);if (rc) return rc;
  const char *digits[]={"零","一","二","三","四","五","六","七","八","九"};
  // Explicit Mandarin letter-name spelling, NOT natural English G2P. It
  // preserves each Latin letter instead of aborting the remaining reply.
  const char *letters[]={"诶","比","西","迪","伊","艾弗","吉","艾尺","艾","杰","开","艾勒","艾姆",
    "恩","欧","皮","丘","阿尔","艾丝","提","优","维","达布溜","艾克斯","歪","贼德"};
  std::vector<std::string> expanded;
  auto digit=[](const std::string &s){return s.size()==1&&s[0]>='0'&&s[0]<='9';};
  auto add=[&](const char *s){return utf8(s,expanded);};
  for(std::size_t at=0;at<words.size();) {
    if(words[at].size()==1 && ((words[at][0]>='A' && words[at][0]<='Z') ||
                              (words[at][0]>='a' && words[at][0]<='z'))) {
      unsigned char c=words[at++][0];if(c>='a')c-=32;add(letters[c-'A']);continue;
    }
    if(digit(words[at])) {
      std::size_t end=at;while(end<words.size()&&digit(words[end]))++end;
      // A deliberately limited, deterministic numeric profile, not a TN/FST.
      // ISO dates are read as dates, never mistaken for repeated subtraction.
      if(end-at==4 && end<words.size() && words[end]=="-") {
        std::size_t m=end+1,me=m;while(me<words.size()&&digit(words[me]))++me;
        if(me>m&&me-m<=2&&me<words.size()&&words[me]=="-") {
          std::size_t d=me+1,de=d;while(de<words.size()&&digit(words[de]))++de;
          if(de>d&&de-d<=2) {
            unsigned y=0,month=0,day=0;
            for(std::size_t i=at;i<end;i++)y=y*10+words[i][0]-'0';
            for(std::size_t i=m;i<me;i++)month=month*10+words[i][0]-'0';
            for(std::size_t i=d;i<de;i++)day=day*10+words[i][0]-'0';
            const unsigned days[]={0,31,28,31,30,31,30,31,31,30,31,30,31};
            if(!y||month<1||month>12||!day||day>days[month]+
                unsigned(month==2 && y%4==0 && (y%100!=0||y%400==0)))return -EINVAL;
            for(std::size_t i=at;i<end;i++)expanded.emplace_back(digits[words[i][0]-'0']);
            add("年");for(std::size_t i=m;i<me;i++)expanded.emplace_back(digits[words[i][0]-'0']);
            add("月");for(std::size_t i=d;i<de;i++)expanded.emplace_back(digits[words[i][0]-'0']);
            add("日");at=de;continue;
          }
        }
      }
      std::size_t decimal=end;
      if(end+1<words.size()&&words[end]=="."&&digit(words[end+1])) {
        ++end;while(end<words.size()&&digit(words[end]))++end;
        if(end+1<words.size()&&words[end]=="."&&digit(words[end+1]))return -ENOTSUP;
      }
      bool percent=end<words.size()&&words[end]=="%";
      if(percent){if(end+1<words.size()&&digit(words[end+1]))return -ENOTSUP;add("百分之");}
      for(std::size_t i=at;i<end;i++){
        if(i==decimal && words[i]==".")add("点");else expanded.emplace_back(digits[words[i][0]-'0']);}
      at=end+unsigned(percent);continue;
    }
    const auto &w=words[at];const char *replacement=nullptr;
    if(w=="+")replacement="加";else if(w=="=")replacement="等于";
    else if(w=="*"||w=="×")replacement="乘以";else if(w=="/"||w=="÷")replacement="除以";
    else if(w=="-")replacement=at==0||words[at-1]=="="||words[at-1]=="("?"负":"减";
    else if(w=="℃")replacement="摄氏度";
    if(replacement)add(replacement);else expanded.push_back(w);++at;
    if(expanded.size()>16384)return -E2BIG;
  }
  for (auto &w:expanded) {
    // Explicit nonverbal emoji profile: render as a pause, not a long spoken
    // Unicode number. This does not skip words or claim expressive acoustics.
    if(w=="😊" || w=="🙂" || w=="😄" || w=="😢" || w=="🥰" || w=="😉" || w=="❤️")w="，";
    // Typography is a pause, not an unknown spoken Unicode number. Keep
    // every word inside parentheses/quotes; do not silently omit content.
    if(w=="（" || w=="）" || w=="(" || w==")" || w=="【" || w=="】" ||
       w=="“" || w=="”" || w=="‘" || w=="’" || w=="\"")w="，";
    if (w==":" || w=="：" || w=="、" || w=="；") w="，";
    else if (w==".") w="。";else if (w=="?") w="？";else if (w=="!") w="！";
    if (!cleaned.empty() && (w==" " || punct(w)) &&
        (cleaned.back()==" " || punct(cleaned.back()))) continue;
    cleaned.push_back(w);
  }
  std::vector<int64_t> current;
  auto flush=[&](){
    if(current.empty())return;
    std::vector<int64_t> ids(2*current.size()+1,1);
    for(std::size_t i=0;i<current.size();++i)ids[2*i+1]=current[i];
    sentences.emplace_back(std::move(ids));current.clear();
  };
  auto append=[&](const std::vector<int64_t> &phones)->int {
    // 127 is a tensor input limit, NOT a safe predicted-duration limit.
    // The real acoustic output is bounded to 861 mel frames. Keep complete
    // lexicon words together, but use shorter normal chunks (49 IDs max).
    if(phones.size()>24)return -E2BIG;
    if(current.size()+phones.size()>24)flush();
    current.insert(current.end(),phones.begin(),phones.end());
    return sentences.size()>64?-E2BIG:0;
  };
  for (std::size_t at=0;at<cleaned.size();) {
    std::string word=cleaned[at];std::size_t take=1;
    const bool ascii=word.size()==1 && ((word[0]>='A' && word[0]<='Z') ||
      (word[0]>='a' && word[0]<='z') || punct(word));
    if (!ascii) {
      std::string candidate=word;
      for (std::size_t n=2;n<=10 && at+n<=cleaned.size();++n) {
        candidate+=cleaned[at+n-1];
        if (f->lexicon.find(candidate)!=f->lexicon.end()) { word=candidate;take=n; }
      }
    }
    auto lex=f->lexicon.find(word);
    if (lex!=f->lexicon.end()) {rc=append(lex->second);if(rc)return rc;}
    else {
      auto token=f->token.find(word);
      if (token!=f->token.end()) {rc=append({token->second});if(rc)return rc;}
      else if (word!=" ") {
        unsigned cp=static_cast<unsigned char>(word[0]);
        if(word.size()>1){cp&=word.size()==2?31:word.size()==3?15:7;
          for(std::size_t i=1;i<word.size();i++)cp=(cp<<6)|(static_cast<unsigned char>(word[i])&63);}
        std::vector<std::string> named;utf8("未知字符编码",named);
        char decimal[16];std::snprintf(decimal,sizeof(decimal),"%u",cp);
        for(const char *p=decimal;*p;p++)named.emplace_back(digits[*p-'0']);
        // Never recursively replace a broken lexicon, or silently omit OOV.
        for(auto &ch:named) {
          auto found=f->lexicon.find(ch);
          if(found==f->lexicon.end())return -ENODATA;
          rc=append(found->second);if(rc)return rc;
        }
        std::fprintf(stderr,"tts-normalize-v214: OOV U+%04X spoken as decimal Unicode codepoint; not omitted\n",cp);
      }
    }
    if (current.size()>512) return -E2BIG;
    at+=take;
    if ((punct(word) || at==cleaned.size()) && !current.empty()) {
      flush();
      if (sentences.size()>64) return -E2BIG;
    }
  }
  return sentences.empty()?-ENODATA:0;
}
int voice_tts_frontend_text(const VoiceTtsFrontend *f,const char *text,
                            std::vector<std::vector<int64_t>> &out)
{
  out.clear();
  if (!f || !text || !*text || strnlen(text,4097)>4096) return -EINVAL;
  std::vector<std::vector<int64_t>> result;int rc;
#if defined(__cpp_exceptions)
  try {
#endif
    rc=prepare(f,text,result);
#if defined(__cpp_exceptions)
  } catch (const std::bad_alloc &) { rc=-ENOMEM; }
    catch (const std::exception &) { rc=-EIO; }
#endif
  if (!rc) out=std::move(result);
  return rc;
}
void voice_tts_frontend_close(VoiceTtsFrontend *f) { delete f; }
int voice_tts_mel_chunk(const float *mel,unsigned frames,unsigned first,
                        float *out,std::size_t floats,unsigned *kept)
{
  if (!kept) return -EINVAL;
  *kept=0;
  if (!mel || !out || !frames || frames>4000 || first>=frames || floats<80*200) return -EINVAL;
  unsigned take=frames-first;if (take>200) take=200;
  for (unsigned c=0;c<80;++c) for (unsigned t=0;t<take;++t)
    if (!std::isfinite(mel[c*frames+first+t])) return -EILSEQ;
  for (unsigned c=0;c<80;++c) for (unsigned t=0;t<200;++t)
    out[c*200+t]=t<take?mel[c*frames+first+t]:-11.4f;
  *kept=take;return 0;
}
int voice_tts_vocoder_window(const float *mel,unsigned frames,unsigned first,
    float *out,std::size_t floats,unsigned *skip,unsigned *keep)
{
  if(!skip || !keep)return -EINVAL;
  *skip=*keep=0;
  unsigned window=first>16?first-16:0,valid=0;
  int rc=voice_tts_mel_chunk(mel,frames,window,out,floats,&valid);
  if(rc)return rc;
  *skip=first-window;
  if(first>=frames || *skip>=valid){*skip=0;return -EINVAL;}
  *keep=valid-*skip;
  if(window+200<frames)*keep-=16;
  return 0;
}
int voice_tts_pcm16(const float *wave,std::size_t samples,unsigned char *out,std::size_t bytes)
{
  if (!wave || !out || !samples || samples>1024000 || samples>bytes/2) return -EINVAL;
  for (std::size_t i=0;i<samples;++i) if (!std::isfinite(wave[i])) return -EILSEQ;
  for (std::size_t i=0;i<samples;++i) {
    float v=wave[i];if (v>1) v=1;else if (v<-1) v=-1;
    long value=std::lround(v*32768.0f);if (value>32767) value=32767;
    uint16_t bits=static_cast<uint16_t>(value);
    out[2*i]=bits&255;out[2*i+1]=bits>>8;
  }
  return 0;
}
}
