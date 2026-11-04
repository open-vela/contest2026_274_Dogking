/* SPDX-License-Identifier: Apache-2.0 */
/* Included by voice_npu_check.cxx. Explicit bounded file-feature checkpoint.
 * No live recording, cloud, ASR, speech, Agent dispatch or physical actions.
 * Actual encoder output[1..37] feeds the next input; input38 is feature offset.
 */
namespace {
struct KwsWordDisk { aipet_kws_word word; char name[64]; };
struct KwsReference { uint32_t model, inputs, outputs, in[39], out[39]; };
static_assert(sizeof(KwsWordDisk)==140 && sizeof(KwsReference)==324,"Fixture ABI");
struct KwsAffinity {
#if defined(__NuttX__) || defined(AIPET_NPU_TARGET_TEST)
  cpu_set_t saved{};bool changed=false;
  int enter(int cpu) {
    if(cpu<0)return 0;
    if(sched_getaffinity(0,sizeof(saved),&saved)<0)return -errno;
    if(!CPU_ISSET(cpu,&saved))return -EPERM;
    cpu_set_t selected;CPU_ZERO(&selected);CPU_SET(cpu,&selected);
    if(sched_setaffinity(0,sizeof(selected),&selected)<0)return -errno;
    changed=true;return 0;
  }
  int restore() {
    if(!changed)return 0;
    changed=false;return sched_setaffinity(0,sizeof(saved),&saved)<0?-errno:0;
  }
#else
  int enter(int) { return 0; } /* portable host oracle has no board affinity */
  int restore() { return 0; }
#endif
};
static uint32_t kws_key(uint32_t model,const uint32_t *input,unsigned count)
{
  uint32_t hash=2166136261u^model;
  for(unsigned i=0;i<count;++i)hash=(hash^input[i])*16777619u;
  return hash;
}
static int64_t kws_us()
{
  timespec now{};clock_gettime(CLOCK_MONOTONIC,&now);
  return static_cast<int64_t>(now.tv_sec)*1000000+now.tv_nsec/1000;
}
static bool kws_interrupted()
{
  sigset_t pending;
  return sigpending(&pending)==0 && sigismember(&pending,SIGINT)==1;
}
static int kws_file(const char *path,void **out,size_t *bytes,size_t cap)
{
  *out=nullptr;*bytes=0;
  int fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0)return -errno;
  struct stat st{};int ret=0;void *data=nullptr;size_t used=0;
  if(fstat(fd,&st)<0)ret=-errno;
  else if(!S_ISREG(st.st_mode) || st.st_size<=0 || static_cast<uint64_t>(st.st_size)>cap)ret=-EFBIG;
  if(!ret) {data=std::malloc(st.st_size);if(!data)ret=-ENOMEM;}
  while(!ret && used<static_cast<size_t>(st.st_size)) {
    ssize_t got=read(fd,static_cast<unsigned char *>(data)+used,st.st_size-used);
    if(got<0 && errno==EINTR)continue;
    if(got<=0)ret=got<0?-errno:-EIO;else used+=got;
  }
  if(close(fd)<0 && !ret)ret=-errno;
  if(ret)std::free(data);else {*out=data;*bytes=used;}
  return ret;
}
struct KwsSession {
  Work model[3];
  void *features=nullptr,*word_file=nullptr,*reference_file=nullptr,*search=nullptr;
  KwsWordDisk *words=nullptr;
  KwsReference *reference=nullptr;
  size_t feature_bytes=0,word_bytes=0,reference_bytes=0;
  uint32_t word_count=0,reference_count=0,calls=0,chain=0,hits=0,completed=0;
  uint32_t *reference_index=nullptr,index_size=0;
  int64_t deadline=0,run_us[3]{},prepare_us=0;
  uint64_t check_us[3]{},search_us=0,chunk_us[64]{};
  a733_tensor_client_profile profile[3]{};
  bool quiet=false,full_clean=false,crc_legacy=false,dynamic=false;
  uint32_t known_calls=0,unknown_calls=0;
  aipet_kws_hit candidates[32]{};
  struct Decoder { int32_t context[2];float data[320]; } decoder[64]{};
  uint32_t decoders=0;
  float logits[17*263]{}; /* heap session, never put 18 KiB on NSH's 16 KiB stack */
  ~KwsSession() {
    if(features)std::memset(features,0,feature_bytes);
    std::free(features);std::free(word_file);std::free(reference_file);std::free(search);
    std::free(reference_index);
  }
  int invoke(unsigned id) {
    if(dynamic && kws_interrupted())return -ECANCELED;
    if(calls>=6000 || kws_us()>deadline)return -ETIMEDOUT;
    int64_t checking=kws_us();
    Work &m=model[id];uint32_t input_crc[39]{};
    for(unsigned i=0;i<m.contract.inputs;++i)input_crc[i]=crc(m.input[i].data,m.input[i].bytes);
    const KwsReference *expected=nullptr;
    uint32_t slot=kws_key(id,input_crc,m.contract.inputs)&(index_size-1u);
    for(unsigned n=0;n<index_size;++n,slot=(slot+1u)&(index_size-1u)) {
      uint32_t index=reference_index[slot];if(index==UINT32_MAX)break;
      const KwsReference &r=reference[index];
      if(r.model==id && !std::memcmp(r.in,input_crc,m.contract.inputs*4u)){expected=&r;break;}
    }
    /* Missing golden input is an error BEFORE hardware, not an invitation to
     * replay outputs or silently change the search policy. */
    if(!expected && !dynamic)return -ENODATA;
    check_us[id]+=kws_us()-checking;
    int64_t start=kws_us();
    int ret=a733_tensor_client_execute(m.client,m.input,m.output,5000,0);
    run_us[id]+=kws_us()-start;++calls;
    if(ret)return ret;
    a733_tensor_client_profile last{};
    ret=a733_tensor_client_get_profile(m.client,&last);if(ret)return ret;
    for(unsigned i=0;i<A733_TENSOR_CLIENT_PHASES;++i)profile[id].us[i]+=last.us[i];
    profile[id].clean_bytes+=last.clean_bytes;
    profile[id].table_clean_bytes+=last.table_clean_bytes;
    profile[id].rebuild_bytes+=last.rebuild_bytes;
    checking=kws_us();
    uint32_t record[81]{};size_t count=0;record[count++]=id;
    for(unsigned i=0;i<m.contract.inputs;++i)record[count++]=input_crc[i];
    for(unsigned i=0;i<m.contract.outputs;++i) {
      uint32_t actual=crc(m.output[i].data,m.output[i].bytes);
      if(expected && actual!=expected->out[i])return -EILSEQ;
      auto values=static_cast<const float *>(m.output[i].data);
      for(size_t f=0;f<m.output[i].bytes/4;++f)if(!std::isfinite(values[f]))return -EILSEQ;
      record[count++]=actual;
    }
    /* Chain all inputs and ALL outputs, not just the first state. */
    chain^=crc(record,count*4u);chain=(chain<<1)|(chain>>31);
    if(expected)++known_calls;else ++unknown_calls;
    check_us[id]+=kws_us()-checking;
    return 0;
  }
  int shutdown() {
    int result=0;
    for(auto &m:model)if(m.client) {
      int ret=a733_tensor_client_close(m.client);m.client=nullptr;
      if(!result && ret)result=ret;
    }
    return result;
  }
};
static int kws_path(char *out,size_t cap,const char *base,const char *name)
{
  int n=std::snprintf(out,cap,"%s/%s",base,name);
  return n<0 || static_cast<size_t>(n)>=cap?-ENAMETOOLONG:0;
}
static int kws_assets(KwsSession &s,const char *base,const char *features)
{
  char path[256];int ret=s.features?0:kws_file(features,&s.features,&s.feature_bytes,320u*1024u);
  if(ret)return ret;
  if(s.feature_bytes%320 || s.feature_bytes<29*320)return -EINVAL;
  if(s.dynamic && s.feature_bytes>320000)return -E2BIG;
  auto values=static_cast<const float *>(s.features);
  for(size_t i=0;i<s.feature_bytes/4;++i)
    if(!std::isfinite(values[i]) || (s.dynamic && std::fabs(values[i])>1000.f))return -EILSEQ;
  ret=kws_path(path,sizeof(path),base,"kws/words.bin");if(ret)return ret;
  ret=kws_file(path,&s.word_file,&s.word_bytes,16+8*sizeof(KwsWordDisk));if(ret)return ret;
  if(s.word_bytes<16)return -EPROTO;
  auto header=static_cast<const uint32_t *>(s.word_file);
  s.word_count=header[2];
  if(header[0]!=0x574b3741 || header[1]!=1 || header[3] ||
     !s.word_count || s.word_count>8 || s.word_bytes!=16+s.word_count*sizeof(KwsWordDisk))return -EPROTO;
  s.words=reinterpret_cast<KwsWordDisk *>(static_cast<unsigned char *>(s.word_file)+16);
  aipet_kws_word words[8]{};
  for(unsigned i=0;i<s.word_count;++i) {
    if(!s.words[i].name[0] || !std::memchr(s.words[i].name,0,64))return -EPROTO;
    words[i]=s.words[i].word;
  }
  size_t bytes=aipet_kws_bytes();if(!bytes || bytes>16384)return -EPROTO;
  s.search=std::malloc(bytes);if(!s.search)return -ENOMEM;
  aipet_kws_config policy{75,15,12,12,.0001f,.12f,2};
  ret=aipet_kws_init(s.search,bytes,words,s.word_count,&policy);if(ret)return ret;
  ret=kws_path(path,sizeof(path),base,"kws/reference.bin");if(ret)return ret;
  ret=kws_file(path,&s.reference_file,&s.reference_bytes,16+4096*sizeof(KwsReference));if(ret)return ret;
  if(s.reference_bytes<16)return -EPROTO;
  header=static_cast<const uint32_t *>(s.reference_file);s.reference_count=header[2];
  if(header[0]!=0x524b3741 || header[1]!=1 || header[3] || !s.reference_count ||
     s.reference_count>4096 || s.reference_bytes!=16+s.reference_count*sizeof(KwsReference))return -EPROTO;
  s.reference=reinterpret_cast<KwsReference *>(static_cast<unsigned char *>(s.reference_file)+16);
  const char *names[]={"encoder","decoder","joiner"};
  for(unsigned id=0;id<3;++id) {
    Work &m=s.model[id];ret=contract(names[id],m.contract);if(ret)return ret;
    for(unsigned i=0;i<m.contract.inputs;++i) {
      void *p=std::calloc(1,m.contract.input_bytes[i]);m.input[i]={p,m.contract.input_bytes[i]};if(!p)return -ENOMEM;
    }
    for(unsigned i=0;i<m.contract.outputs;++i) {
      void *p=std::calloc(1,m.contract.output_bytes[i]);m.output[i]={p,m.contract.output_bytes[i]};if(!p)return -ENOMEM;
    }
  }
  for(unsigned n=0;n<s.reference_count;++n) {
    const auto &r=s.reference[n];if(r.model>2)return -EPROTO;
    const auto &c=s.model[r.model].contract;
    if(r.inputs!=c.inputs || r.outputs!=c.outputs)return -EPROTO;
  }
  s.index_size=2;while(s.index_size<s.reference_count*2u)s.index_size*=2u;
  s.reference_index=static_cast<uint32_t *>(std::malloc(s.index_size*sizeof(uint32_t)));
  if(!s.reference_index)return -ENOMEM;
  std::memset(s.reference_index,0xff,s.index_size*sizeof(uint32_t));
  for(unsigned n=0;n<s.reference_count;++n) {
    const auto &r=s.reference[n];uint32_t slot=kws_key(r.model,r.in,r.inputs)&(s.index_size-1u);
    while(s.reference_index[slot]!=UINT32_MAX) {
      const auto &other=s.reference[s.reference_index[slot]];
      if(other.model==r.model && !std::memcmp(other.in,r.in,r.inputs*4u))return -EPROTO;
      slot=(slot+1u)&(s.index_size-1u);
    }
    s.reference_index[slot]=n;
  }
  return 0;
}
static int kws_run(KwsSession &s,const char *base)
{
  const char *names[]={"encoder.a7tv","decoder.a7tv","joiner.a7tv"};
  char path[256];int ret=0;int64_t start=kws_us();
  for(unsigned id=0;!ret && id<3;++id) {
    ret=kws_path(path,sizeof(path),base,names[id]);if(ret)break;
    ret=a733_tensor_client_open(path,&s.model[id].contract,&s.model[id].client);
    if(!ret)ret=a733_tensor_client_prepare(s.model[id].client);
    if(!ret)ret=a733_tensor_client_full_clean(s.model[id].client,s.full_clean?1:0);
    if(!ret)ret=a733_tensor_client_crc_legacy(s.model[id].client,s.crc_legacy?1:0);
  }
  s.prepare_us=kws_us()-start;if(ret)return ret;
  std::printf("npukws: resident=3 cold-prepare-us=%lld; buffered features only; no cloud/ASR/Agent actions\n",
              static_cast<long long>(s.prepare_us));
  s.deadline=kws_us()+(s.dynamic?60000000:900000000);
  size_t chunks=(s.feature_bytes/320-29)/16+1;
  if(chunks>64)return -E2BIG;
  Work &enc=s.model[0],&dec=s.model[1],&join=s.model[2];
  for(unsigned chunk=0;chunk<chunks;++chunk) {
    int64_t chunk_start=kws_us();
    std::memcpy(const_cast<void *>(enc.input[0].data),static_cast<unsigned char *>(s.features)+chunk*16*320,9280);
    float offset=static_cast<float>(chunk*16);
    std::memcpy(const_cast<void *>(enc.input[38].data),&offset,4);
    ret=s.invoke(0);if(ret)return ret;
    for(unsigned i=1;i<38;++i)std::memcpy(const_cast<void *>(enc.input[i].data),enc.output[i].data,enc.input[i].bytes);
    for(unsigned f=0;f<4;++f) {
      int32_t contexts[34];int rows=aipet_kws_contexts(s.search,contexts,34);
      if(rows<1 || rows>17)return -EPROTO;
      for(int row=0;row<rows;++row) {
        unsigned index=0;
        while(index<s.decoders && std::memcmp(s.decoder[index].context,contexts+row*2,8))++index;
        if(index==s.decoders) {
          if(index>=64)return -E2BIG;
          float tokens[2]={static_cast<float>(contexts[row*2]),static_cast<float>(contexts[row*2+1])};
          std::memcpy(const_cast<void *>(dec.input[0].data),tokens,8);
          ret=s.invoke(1);if(ret)return ret;
          std::memcpy(s.decoder[index].context,contexts+row*2,8);
          std::memcpy(s.decoder[index].data,dec.output[0].data,1280);++s.decoders;
        }
        std::memcpy(const_cast<void *>(join.input[0].data),static_cast<unsigned char *>(enc.output[0].data)+f*1280,1280);
        std::memcpy(const_cast<void *>(join.input[1].data),s.decoder[index].data,1280);
        ret=s.invoke(2);if(ret)return ret;
        std::memcpy(s.logits+row*263,join.output[0].data,1052);
      }
      int64_t search_start=kws_us();
      aipet_kws_hit hit{};ret=aipet_kws_advance(s.search,s.logits,rows,&hit);
      s.search_us+=kws_us()-search_start;if(ret)return ret;
      if(hit.word>=0) {
        if(static_cast<unsigned>(hit.word)>=s.word_count)return -EPROTO;
        if(s.dynamic) {
          if(s.hits>=32)return -E2BIG;
          s.candidates[s.hits]=hit;
        }
        ++s.hits;
        if(!s.dynamic)std::printf("npukws hit: word=%ld name=%s frame=%lu probability=%.9f relative=%.9f\n",
                    static_cast<long>(hit.word),s.words[hit.word].name,static_cast<unsigned long>(hit.frame),
                    static_cast<double>(hit.mean_probability),static_cast<double>(hit.relative_score));
      }
    }
    s.chunk_us[chunk]=kws_us()-chunk_start;
    ++s.completed;
    if(!s.quiet)std::printf("npukws: chunk=%u/%u calls=%lu hits=%lu chain=%08lx\n",chunk+1,static_cast<unsigned>(chunks),
                static_cast<unsigned long>(s.calls),static_cast<unsigned long>(s.hits),static_cast<unsigned long>(s.chain));
  }
  return 0;
}
}
static int kws_control(int argc,char **argv,bool dynamic,
                        const float *memory=nullptr,unsigned frames=0)
{
  if(argc<2 || argc>6) {
    std::puts("aipet audio npukws <model-dir> <features.f32> [auto|cpu6|cpu7] [quiet] [fullclean] [crclegacy]");return -EINVAL;
  }
  std::unique_ptr<KwsSession> s(new(std::nothrow) KwsSession);if(!s)return -ENOMEM;
  s->dynamic=dynamic;
  if(memory) {
    if(!dynamic || frames<29 || frames>1000)return -EINVAL;
    s->feature_bytes=static_cast<size_t>(frames)*320;
    s->features=std::malloc(s->feature_bytes);if(!s->features)return -ENOMEM;
    std::memcpy(s->features,memory,s->feature_bytes);
  }
  int cpu=6;bool cpu_option=false;
  for(int i=2;i<argc;++i) {
    if(!std::strcmp(argv[i],"quiet") && !s->quiet)s->quiet=true;
    else if(!std::strcmp(argv[i],"fullclean") && !s->full_clean)s->full_clean=true;
    else if(!std::strcmp(argv[i],"crclegacy") && !s->crc_legacy)s->crc_legacy=true;
    else if(!cpu_option && (!std::strcmp(argv[i],"auto") || !std::strcmp(argv[i],"cpu6") || !std::strcmp(argv[i],"cpu7"))) {
      cpu=!std::strcmp(argv[i],"auto")?-1:argv[i][3]-'0';cpu_option=true;
    } else {std::printf("npukws: invalid option '%s'; no hardware run\n",argv[i]);return -EINVAL;}
  }
  int ret=kws_assets(*s,argv[0],argv[1]);
  if(ret){std::printf("npukws: preflight-failed=%d; no hardware run\n",ret);return ret;}
  int saved=0;ret=pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&saved);if(ret)return -ret;
  KwsAffinity affinity;ret=affinity.enter(cpu);
  int64_t started=kws_us();
  if(!ret) {
    if(dynamic)std::puts("npukws-input: EXPERIMENTAL buffered-input; unknown native outputs have NO golden accuracy proof; no Agent/cloud/physical actions");
    std::printf("npukws: cpu-policy=%s clean=%s validation=all-native-CRCs driver-crc=%s app-crc=slice8\n",cpu<0?"auto":cpu==6?"cpu6":"cpu7",s->full_clean?"full":"bounded",s->crc_legacy?"legacy":"slice8");
    ret=kws_run(*s,argv[0]);
  }
  int closed=s->shutdown();if(!ret)ret=closed;
  int restored=affinity.restore();if(!ret)ret=restored;
  if(!ret && dynamic && kws_interrupted())ret=-ECANCELED;
  for(unsigned id=0;id<3;++id) {
    const auto &p=s->profile[id];
    std::printf("npukws profile[%u]: validate/rebuild/map/clean/hw/check/commit/restore/total-us=",id);
    for(unsigned i=0;i<9;++i)std::printf("%s%llu",i?"/":"",static_cast<unsigned long long>(p.us[i]));
    std::printf(" clean-bytes=%llu tables=%llu rebuilt=%llu app-check-us=%llu\n",static_cast<unsigned long long>(p.clean_bytes),
      static_cast<unsigned long long>(p.table_clean_bytes),static_cast<unsigned long long>(p.rebuild_bytes),static_cast<unsigned long long>(s->check_us[id]));
  }
  uint64_t sum=0,sorted[64]{};unsigned chunks=static_cast<unsigned>((s->feature_bytes/320-29)/16+1),over=0;
  for(unsigned i=0;i<s->completed;++i) {
    sum+=s->chunk_us[i];over+=s->chunk_us[i]>160000;unsigned j=i;
    while(j && sorted[j-1]>s->chunk_us[i]){sorted[j]=sorted[j-1];--j;}sorted[j]=s->chunk_us[i];
  }
  uint64_t p50=s->completed?sorted[(s->completed-1)/2]:0;
  uint64_t p95=s->completed?sorted[(s->completed*95+99)/100-1]:0;
  uint64_t maximum=s->completed?sorted[s->completed-1]:0;
  std::printf("npukws timing: chunks=%u completed=%lu sum-us=%llu p50-us=%llu p95-us=%llu max-us=%llu over-160ms=%u search-us=%llu affinity-restored=%d\n",chunks,
    static_cast<unsigned long>(s->completed),static_cast<unsigned long long>(sum),static_cast<unsigned long long>(p50),
    static_cast<unsigned long long>(p95),static_cast<unsigned long long>(maximum),over,static_cast<unsigned long long>(s->search_us),restored==0);
  if(!dynamic)std::printf("npukws: status=%d calls=%lu hits=%lu chain=%08lx elapsed-us=%lld run-us=%lld/%lld/%lld; resident released\n",
              ret,static_cast<unsigned long>(s->calls),static_cast<unsigned long>(s->hits),static_cast<unsigned long>(s->chain),
              static_cast<long long>(kws_us()-started),static_cast<long long>(s->run_us[0]),
              static_cast<long long>(s->run_us[1]),static_cast<long long>(s->run_us[2]));
  if(dynamic) {
    if(!ret)for(unsigned i=0;i<s->hits;++i) {
      const auto &h=s->candidates[i];
      std::printf("npukws-input candidate: word=%ld name=%s frame=%lu probability=%.9f; diagnostic only\n",
        static_cast<long>(h.word),s->words[h.word].name,static_cast<unsigned long>(h.frame),static_cast<double>(h.mean_probability));
    }
    std::printf("npukws-input: status=%d calls=%lu candidates=%lu known=%lu unknown=%lu chain=%08lx feature-crc=%08lx frames=%zu unused-tail=%zu elapsed-us=%lld; resident released; acoustic accuracy NOT accepted\n",
      ret,static_cast<unsigned long>(s->calls),static_cast<unsigned long>(ret?0:s->hits),
      static_cast<unsigned long>(s->known_calls),static_cast<unsigned long>(s->unknown_calls),static_cast<unsigned long>(s->chain),
      static_cast<unsigned long>(crc(s->features,s->feature_bytes)),s->feature_bytes/320,
      s->feature_bytes/320-(29+(chunks-1)*16),static_cast<long long>(kws_us()-started));
  }
  s.reset();pthread_setcancelstate(saved,nullptr);return ret;
}
int voice_npu_kws(int argc,char **argv) { return kws_control(argc,argv,false); }
int voice_npu_kws_input(int argc,char **argv) { return kws_control(argc,argv,true); }
int voice_npu_kws_memory(const char *base,const float *features,unsigned frames)
{
  if(!base || !features || frames<29 || frames>1000)return -EINVAL;
  char ram[]="RAM",cpu[]="cpu6",quiet[]="quiet";
  char *args[]={const_cast<char *>(base),ram,cpu,quiet};
  return kws_control(4,args,true,features,frames);
}
