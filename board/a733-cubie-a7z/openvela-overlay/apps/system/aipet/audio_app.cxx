/* SPDX-License-Identifier: Apache-2.0 */
#include "audio_app.hxx"
#include "audio_resample.hxx"
#include "audio_capture_live.hxx"
#include "voice_fbank.hxx"
#include "voice_npu_check.hxx"
#include "voice_local.hxx"
#include "voice_general_provider.hxx"
#include "cloud_voice.h"
#include "voice_load_log.hxx"
#include "voice_model_storage.hxx"
#ifdef __NuttX__
#include <nuttx/config.h>
#ifdef CONFIG_SYSTEM_A733STORAGE
#include <system/a733_storage.h>
#endif
#endif
#include "voice_play_stream.hxx"
#include "local_llm.hxx"
#include "agent_bridge.h"
#include "agent_secret.h"
#include "pet_routes.hxx"
#include "pet_core.hxx"
#ifdef CONFIG_A733_PRODUCT_V1
#include "product_runtime.hxx"
#include "mcu_link.hxx"
#endif
#include "speech_output.hxx"
#include "cloud_prefetch.hxx"
#include <system/a733_pcm.h>
#include "voice/voice_asr.h"
#include "voice/voice_tts.h"
#include <cerrno>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <new>
#include <pthread.h>
#include <signal.h>
#if defined(__NuttX__) || defined(AIPET_TEST_RESIDENT)
#include <spawn.h>
#endif
#include <sys/stat.h>
#include <unistd.h>

#ifndef AIPET_AUDIO_DIR
#define AIPET_AUDIO_DIR "/data/ai_agent"
#endif
#ifndef AIPET_AUDIO_RECORD
#define AIPET_AUDIO_RECORD "/data/aipet-record.wav"
#endif

namespace aipet {
struct AudioConfig { unsigned output=1, volume=50, gain=5; };
/* v177 retires UART TTS. Legacy saved mode 0 maps to I2S, without file edits. */
static pthread_mutex_t config_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t operation_lock=PTHREAD_MUTEX_INITIALIZER;
static std::atomic<int> audio_fault{0}; // uncertain worker ownership: reboot, never reuse I2S
enum { VS_OFF,VS_PREPARING,VS_CALIBRATING,VS_IDLE,VS_SPEECH,VS_FINALIZING,VS_REPLY,VS_QUARANTINE,VS_WAIT_CLOUD };
static std::atomic<bool> service_active{false},service_stop{false};
static std::atomic<int> service_pid{0},service_stage{VS_OFF},service_error{0};
static std::atomic<unsigned> service_turns{0},service_empty{0},service_long{0},service_floor{400};
static std::atomic<unsigned> service_load_stage{0};
static std::atomic<long long> service_started_us{0},service_load_since_us{0};
#if defined(__NuttX__) || defined(AIPET_TEST_RESIDENT)
static pthread_mutex_t service_lock=PTHREAD_MUTEX_INITIALIZER;
#endif
bool audio_voice_service_active(){return service_active.load();}
#ifdef CONFIG_A733_PRODUCT_V1
static pthread_mutex_t product_start_lock=PTHREAD_MUTEX_INITIALIZER;
static std::atomic<bool> product_boot_greeting{false};
static std::atomic<int> product_manual_owner{0};
extern "C" int a733_product_start_lock(){int rc=pthread_mutex_lock(&product_start_lock);return rc?-rc:0;}
extern "C" void a733_product_start_unlock(){pthread_mutex_unlock(&product_start_lock);}
extern "C" int a733_product_voice_reserved(){return audio_fault.load() || (service_active.load()&&service_pid.load()!=getpid()) || (product_manual_owner.load()&&product_manual_owner.load()!=getpid());}
extern "C" int a733_product_camera_begin(){int rc=a733_product_start_lock();if(rc)return rc;if(a733_product_voice_reserved()){a733_product_start_unlock();return -EBUSY;}return 0;}
int audio_product_manual_begin(){int rc=a733_product_start_lock();if(rc)return rc;
  if(audio_fault.load()||service_active.load()||product_manual_owner.load()||product_vision_busy())rc=-EBUSY;
  else product_manual_owner=getpid();
  a733_product_start_unlock();return rc;}
void audio_product_manual_end(){int rc=a733_product_start_lock();if(rc)return;
  if(product_manual_owner.load()==getpid())product_manual_owner=0;
  a733_product_start_unlock();}
#endif
#ifdef AIPET_TEST_RESIDENT
int audio_voice_service_phase_for_test(){return service_stage.load();}
#endif
static const char *const config_path=AIPET_AUDIO_DIR "/config/audio.conf";

static bool interrupted()
{
  sigset_t pending;
  return sigpending(&pending)==0 && sigismember(&pending,SIGINT)==1;
}

/* Defer Ctrl+C until a synchronous owner can release its board/PCM mutex.
 * Discard captured PCM before upload if SIGINT arrived during capture.
 * SIGKILL/task_delete and asynchronous network cancellation are unsupported.
 */
class AudioInterruptGuard
{
public:
  AudioInterruptGuard() {
    sigset_t set; sigemptyset(&set); sigaddset(&set,SIGINT);
    error=pthread_sigmask(SIG_BLOCK,&set,&saved_);
  }
  ~AudioInterruptGuard() {
    if (error) return;
    if (sigismember(&saved_,SIGINT)!=1) {
      sigset_t set; sigemptyset(&set); sigaddset(&set,SIGINT);
      struct timespec zero={0,0};
      while (interrupted()) {
        if (sigtimedwait(&set,nullptr,&zero)<0) break;
      }
    }
    pthread_sigmask(SIG_SETMASK,&saved_,nullptr);
  }
  int error=0;
private:
  sigset_t saved_;
};

static int load_config(AudioConfig &out)
{
  FILE *f=std::fopen(config_path,"rb");
  if (!f) return errno==ENOENT ? 0 : -errno;
  unsigned version=0, mode=0, volume=0, gain=0;
  int parsed=std::fscanf(f,"%u %u %u %u",&version,&mode,&volume,&gain);
  int ch;
  do { ch=std::fgetc(f); } while (ch==' ' || ch=='\r' || ch=='\n' || ch=='\t');
  bool bad=parsed!=4 || ch!=EOF || std::ferror(f) || version!=151 ||
           mode>2 || volume>100 || gain<1 || gain>16;
  std::fclose(f);
  if (bad) return -EINVAL; /* Never silently replace a damaged config. */
  out.output=mode==0?1:mode; out.volume=volume; out.gain=gain;
  return 0;
}

static int config(AudioConfig &out)
{
  pthread_mutex_lock(&config_lock);
  int result=load_config(out);
  pthread_mutex_unlock(&config_lock);
  return result;
}

static int exact_write(int fd,const unsigned char *data,std::size_t bytes)
{
  while (bytes) {
    ssize_t n=write(fd,data,bytes);
    if (n<0 && errno==EINTR) continue;
    if (n<=0) return n<0 ? -errno : -EIO;
    data+=n; bytes-=n;
  }
  return 0;
}

static int save_config(const AudioConfig &in)
{
  if (mkdir(AIPET_AUDIO_DIR,0700)<0 && errno!=EEXIST) return -errno;
  if (mkdir(AIPET_AUDIO_DIR "/config",0700)<0 && errno!=EEXIST) return -errno;
  const char *tmp=AIPET_AUDIO_DIR "/config/audio.conf.new";
  int fd=open(tmp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
  if (fd<0) return -errno;
  char line[48];
  int size=std::snprintf(line,sizeof(line),"151 %u %u %u\n",in.output,in.volume,in.gain);
  int result=exact_write(fd,reinterpret_cast<unsigned char *>(line),size);
  if (!result && fsync(fd)<0) result=-errno;
  if (close(fd)<0 && !result) result=-errno;
  if (!result && rename(tmp,config_path)<0) result=-errno;
  if (result) unlink(tmp);
  return result;
}

static bool number(const char *text,unsigned min,unsigned max,unsigned &value)
{
  if (!text || !*text) return false;
  unsigned n=0;
  for (;*text;text++) {
    if (*text<'0' || *text>'9') return false;
    unsigned digit=static_cast<unsigned>(*text-'0');
    if (n>max/10 || (n==max/10 && digit>max%10)) return false;
    n=n*10+digit;
  }
  if (n<min) return false;
  value=n; return true;
}

bool audio_i2s_selected()
{
  AudioConfig c;
  return !config(c) && c.output==1;
}
bool audio_muted()
{
  AudioConfig c;
  return config(c)!=0 || c.output==2 || !c.volume;
}

int audio_decimate(const unsigned char *source,std::size_t bytes,
                   unsigned char *out,std::size_t capacity,std::size_t *used)
{
  if (!used) return -EINVAL;
  *used=0;
  if (!source || !out || !bytes || bytes%6 || bytes>960000 || capacity<bytes/3)
    return -EINVAL;
  /* 63-tap symmetric Hamming-windowed low-pass, cutoff 6.5 kHz at 48 kHz.
   * Convolution runs AFTER RX stops; zero-pad edges, preserve sign/DC gain.
   * Kept here behind an adapter until official Media resampling is enabled.
   */
  double taps[63], sum=0;
  constexpr double pi=3.14159265358979323846, cutoff=6500.0/48000.0;
  for (int i=0;i<63;i++) {
    int k=i-31;
    double sinc=k ? std::sin(2*pi*cutoff*k)/(pi*k) : 2*cutoff;
    taps[i]=sinc*(0.54-0.46*std::cos(2*pi*i/62)); sum+=taps[i];
  }
  const std::size_t samples=bytes/2;
  for (std::size_t frame=0;frame<samples/3;frame++) {
    double value=0;
    for (int tap=0;tap<63;tap++) {
      const long index=static_cast<long>(frame*3)+tap-31;
      if (index>=0 && static_cast<std::size_t>(index)<samples) {
        unsigned raw=source[index*2] | (unsigned(source[index*2+1])<<8);
        int sample=raw>=32768 ? int(raw)-65536 : int(raw);
        value+=sample*taps[tap]/sum;
      }
    }
    long sample=std::lround(value);
    if (sample>32767) sample=32767;
    if (sample< -32768) sample=-32768;
    uint16_t raw=static_cast<uint16_t>(static_cast<int16_t>(sample));
    out[frame*2]=raw&255; out[frame*2+1]=raw>>8;
  }
  *used=bytes/3; return 0;
}

static bool engine_ready()
{
  int phase,pid,error;
  aipet_agent_status(&phase,&pid,&error);
  if (phase==2) return true;
  std::puts("aipet audio: official Agent not ready; run aipet start/status first.");
  return false;
}

static int synth(void *,const char *text,unsigned char *pcm,
                  std::size_t capacity,std::size_t *bytes)
{
  if (interrupted()) return -ECANCELED;
  if (!engine_ready()) return -ENODEV;
  /* Official batch interface is explicitly 16k/mono/S16. Its WS interface
   * currently requests 24k, so do not mislabel the latter as 16k PCM. */
  return aipet_cloud_tts(text,pcm,capacity,bytes);
}
static int play(void *context,const unsigned char *pcm,std::size_t bytes,
                 unsigned rate,unsigned channels,unsigned bits)
{
  if (interrupted()) return -ECANCELED;
  if (bits!=16) return -ENOTSUP;
  return a733_pcm_play(pcm,bytes,rate,channels,
                       static_cast<AudioConfig *>(context)->volume);
}

int audio_i2s_speak(const char *text)
{
  if(service_active.load() && service_pid.load()!=getpid())return -EBUSY;
  if(audio_fault.load())return -EOWNERDEAD;
  const int local=voice_general_tts_selected();
  if(local<0)return local;
  if(local)return audio_i2s_local_stream_speak(text); // explicit local selection, no cloud retry
  AudioInterruptGuard signals;
  if (signals.error) return -signals.error;
  if (!text) return -EINVAL;
  Reply checked;
  if (!parse_reply(text,checked)) return -EILSEQ;
  if (pthread_mutex_trylock(&operation_lock)) return -EBUSY;
  AudioConfig c;
  int result=config(c);
  if (!result && c.volume==0) {
    std::puts("aipet audio: I2S muted; no TTS cloud request.");
  } else if (!result) {
    PcmSpeechOutput output({&c,synth,play,nullptr});
    std::puts("aipet audio: selected cloud TTS -> I2S (bounded 10s).");
    result=output.speak(text);
  }
  pthread_mutex_unlock(&operation_lock);
  return result;
}

static void le16(unsigned char *out,unsigned value)
{ out[0]=value&255; out[1]=(value>>8)&255; }
static void le32(unsigned char *out,uint32_t value)
{ le16(out,value&65535); le16(out+2,value>>16); }

static int local_cancel(void *) {
  return interrupted() || (service_active.load() && service_pid.load()==getpid() &&
                           service_stop.load())?-ECANCELED:0;
}
struct LocalPcm { unsigned char *data;std::size_t bytes=0; };
static int collect_local_pcm(void *cookie,const unsigned char *pcm,std::size_t bytes)
{
  auto &out=*static_cast<LocalPcm *>(cookie);
  if(bytes>441000-out.bytes)return -EMSGSIZE;
  std::memcpy(out.data+44+out.bytes,pcm,bytes);out.bytes+=bytes;return 0;
}
int audio_i2s_local_speak(const char *text)
{
  if(service_active.load() && service_pid.load()!=getpid())return -EBUSY;
  if(audio_fault.load())return -EOWNERDEAD;
  AudioInterruptGuard signals;if(signals.error)return -signals.error;
  Reply checked;if(!text || !parse_reply(text,checked))return -EILSEQ;
  if(pthread_mutex_trylock(&operation_lock))return -EBUSY;
  AudioConfig c;int rc=config(c);
  if(!rc && c.output!=1)rc=-EACCES;
  if(!rc && c.volume) {
    std::unique_ptr<unsigned char[]> wav(new(std::nothrow) unsigned char[441044]());
    std::unique_ptr<unsigned char[]> pcm(new(std::nothrow) unsigned char[960000]);
    if(!wav || !pcm)rc=-ENOMEM;
    LocalPcm collected{wav.get()};
    if(!rc)rc=voice_local_speak(text,collect_local_pcm,&collected,local_cancel,nullptr);
    /* Publish no PCM until synthesis completed successfully. Keep the rational
     * resampler's phase across the whole utterance, not per vocoder chunk. */
    if(!rc) {
      auto *p=wav.get();std::memcpy(p,"RIFF",4);le32(p+4,36+collected.bytes);
      std::memcpy(p+8,"WAVEfmt ",8);le32(p+16,16);le16(p+20,1);le16(p+22,1);
      le32(p+24,22050);le32(p+28,44100);le16(p+32,2);le16(p+34,16);
      std::memcpy(p+36,"data",4);le32(p+40,collected.bytes);
      std::size_t used=0;
      rc=matcha_wav_to_48k(p,44+collected.bytes,pcm.get(),960000,&used);
      if(!rc)rc=local_cancel(nullptr);
      if(!rc)rc=a733_pcm_play(pcm.get(),used,48000,1,c.volume);
    }
    if(wav)aipet_wipe(wav.get(),441044);
    if(pcm)aipet_wipe(pcm.get(),960000);
  }
  pthread_mutex_unlock(&operation_lock);return rc;
}

int audio_i2s_local_stream_speak(const char *text)
{ return audio_i2s_local_stream_speak_cancel(text,nullptr,nullptr); }

static int local_play(const char *text,VoiceTextNext next,void *source,int (*cancel)(void *),void *cookie)
{
  if(service_active.load() && service_pid.load()!=getpid())return -EBUSY;
  if(audio_fault.load())return -EOWNERDEAD;
  AudioInterruptGuard signals;if(signals.error)return -signals.error;
  Reply checked;if(!next && (!text||!parse_reply(text,checked)))return -EILSEQ;
  if(pthread_mutex_trylock(&operation_lock))return -EBUSY;
  AudioConfig c;int rc=config(c);
  if(!rc && c.output!=1)rc=-EACCES;
  VoicePlayMetrics metrics{};
  if(!rc && next && !c.volume)rc=-EACCES;
  struct Control {int (*cancel)(void *);void *cookie;};
  std::unique_ptr<Control> control(new(std::nothrow) Control{cancel,cookie});
  if(!control)rc=-ENOMEM;
  auto poll=[](void *p){auto &ctl=*static_cast<Control *>(p);int cancelled=local_cancel(nullptr);
    return cancelled?cancelled:ctl.cancel?ctl.cancel(ctl.cookie):0;};
  if(!rc && c.volume)rc=next?voice_play_feed(next,source,c.volume,poll,control.get(),&metrics):
    voice_play_stream(text,c.volume,poll,control.get(),&metrics);
  if(metrics.worker_retained){audio_fault.store(-EOWNERDEAD);control.release();}
  pthread_mutex_unlock(&operation_lock);
  if(!cancel)std::printf("local-tts-stream: status=%d first-pcm=%lld us first-driver-pull=%lld us frames=%u; audible latency NOT measured\n",
              rc,metrics.first_pcm_us,metrics.first_pull_us,metrics.frames);
  std::printf("local-tts-buffer-v189: segments=%u rebuffers=%u rebuffer-wait-us=%lld; no padding/replay; actual device errors are fatal\n",
              metrics.device_segments,metrics.rebuffers,metrics.rebuffer_wait_us);
  std::printf("voice-stage: tts-start-mono-us=%lld first-driver-pull-mono-us=%lld status=%d; driver pull is NOT physical audible onset\n",
              metrics.start_mono_us,metrics.first_pull_mono_us,rc);
  return rc;
}
int audio_i2s_local_stream_speak_cancel(const char *text,VoiceLocalCancel cancel,void *cookie)
{return local_play(text,nullptr,nullptr,cancel,cookie);}
int audio_i2s_local_feed_cancel(VoiceTextNext next,void *source,VoiceLocalCancel cancel,void *cookie)
{return local_play(nullptr,next,source,cancel,cookie);}

static int save_recording(const unsigned char *pcm,std::size_t bytes)
{
  /* Only an explicit record command creates this fixed exportable WAV.
   * O_EXCL + same-filesystem rename preserves the previous file on failure. */
  const char *tmp=AIPET_AUDIO_RECORD ".new", *path=AIPET_AUDIO_RECORD;
  int fd=open(tmp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
  if (fd<0) return -errno;
  unsigned char header[44]={0};
  std::memcpy(header,"RIFF",4); le32(header+4,36+bytes);
  std::memcpy(header+8,"WAVEfmt ",8); le32(header+16,16);
  le16(header+20,1); le16(header+22,1); le32(header+24,48000);
  le32(header+28,96000); le16(header+32,2); le16(header+34,16);
  std::memcpy(header+36,"data",4); le32(header+40,bytes);
  int result=exact_write(fd,header,sizeof(header));
  if (!result) result=exact_write(fd,pcm,bytes);
  if (!result && fsync(fd)<0) result=-errno;
  if (close(fd)<0 && !result) result=-errno;
  if (!result && rename(tmp,path)<0) result=-errno;
  if (result) unlink(tmp);
  return result;
}

static int inspect_features(const unsigned char *pcm,std::size_t bytes)
{
  std::unique_ptr<unsigned char[]> down(new(std::nothrow) unsigned char[bytes/3]);
  if (!down) return -ENOMEM;
  std::size_t used=0;
  int result=audio_decimate(pcm,bytes,down.get(),bytes/3,&used);
  VoiceFbank *raw=nullptr;
  if (!result) result=voice_fbank_create(&raw);
  std::unique_ptr<VoiceFbank,decltype(&voice_fbank_destroy)> bank(raw,voice_fbank_destroy);
  if (!result) result=voice_fbank_accept(bank.get(),down.get(),used);
  if (!result) result=voice_fbank_finish(bank.get());
  int frames=result ? 0 : voice_fbank_frames(bank.get());
  float row[voice_fbank_bins], low=0,high=0;
  double sum=0;
  for (int i=0;!result && i<frames;i++) {
    result=voice_fbank_copy(bank.get(),i,1,row,voice_fbank_bins);
    for (unsigned j=0;!result && j<voice_fbank_bins;j++) {
      if (!std::isfinite(row[j])) { result=-EDOM; break; }
      if ((!i && !j) || row[j]<low) low=row[j];
      if ((!i && !j) || row[j]>high) high=row[j];
      sum+=row[j];
    }
  }
  if (!result) std::printf("local-frontend: rate=16000 frames=%d bins=80 min=%.6g max=%.6g sum=%.9g\n"
    "CPU Kaldi fbank only; no KWS/ASR/NPU inference, no upload, no saved audio.\n",
    frames,static_cast<double>(low),static_cast<double>(high),sum);
  aipet_wipe(down.get(),bytes/3);
  return result;
}

struct LiveProbe {
  VoiceFbank *bank=nullptr;
  unsigned next=0;
  double sum=0;
  float low=0,high=0;
  unsigned char *echo=nullptr;
  std::size_t capacity=0,used=0,played=0;
};
static int live_probe_rows(LiveProbe *s)
{
  int frames=voice_fbank_frames(s->bank);if(frames<0)return frames;
  float row[80];
  for(;s->next<static_cast<unsigned>(frames);s->next++) {
    int rc=voice_fbank_copy(s->bank,s->next,1,row,80);if(rc)return rc;
    for(unsigned i=0;i<80;i++) {
      if(!std::isfinite(row[i]))return -ERANGE;
      if((!s->next&&!i)||row[i]<s->low)s->low=row[i];
      if((!s->next&&!i)||row[i]>s->high)s->high=row[i];
      s->sum+=row[i];
    }
  }
  return 0;
}
static int live_probe16(void *cookie,const unsigned char *pcm,std::size_t bytes)
{
  auto *s=static_cast<LiveProbe *>(cookie);
  int rc=voice_fbank_accept(s->bank,pcm,bytes);return rc?rc:live_probe_rows(s);
}
static int live_probe48(void *cookie,const unsigned char *pcm,std::size_t bytes)
{
  auto *s=static_cast<LiveProbe *>(cookie);
  if(!s->echo||bytes>s->capacity-s->used)return -ENOSPC;
  std::memcpy(s->echo+s->used,pcm,bytes);s->used+=bytes;return 0;
}
static int live_probe_read(void *cookie,unsigned char *pcm,uint32_t cap,uint32_t *bytes)
{
  auto *s=static_cast<LiveProbe *>(cookie);
  if(interrupted())return -ECANCELED;
  std::size_t n=s->used-s->played;if(n>cap)n=cap;
  if(n)std::memcpy(pcm,s->echo+s->played,n);
  s->played+=n;*bytes=n;return 0;
}
static int live_probe(unsigned ms,unsigned threshold,bool echo,const AudioConfig &c)
{
  if(echo&&(c.output!=1||!c.volume))return -EACCES;
  if(pthread_mutex_trylock(&operation_lock))return -EBUSY;
  struct Unlock {~Unlock(){pthread_mutex_unlock(&operation_lock);}} unlock;
  std::unique_ptr<LiveProbe> owner(new(std::nothrow) LiveProbe);
  if(!owner)return -ENOMEM;
  LiveProbe &s=*owner;
  int rc=voice_fbank_create(&s.bank);
  std::unique_ptr<VoiceFbank,decltype(&voice_fbank_destroy)> bank(s.bank,voice_fbank_destroy);
  std::unique_ptr<unsigned char[]> saved;
  if(!rc&&echo){s.capacity=ms*96u;saved.reset(new(std::nothrow) unsigned char[s.capacity]);
    if(!saved)rc=-ENOMEM;else s.echo=saved.get();}
  LiveCaptureMetrics metrics{};
  std::printf("MIC ON: live CPU frontend %u ms, gain=x%u endpoint-rms=%u. No upload/ASR/NPU/actions.\n",
              ms,c.gain,threshold);
  if(!rc)rc=audio_capture_live(ms,c.gain,threshold,echo?live_probe48:nullptr,
                              live_probe16,&s,local_cancel,nullptr,&metrics);
  if(metrics.consumer_retained) {
    audio_fault.store(-EOWNERDEAD);bank.release();saved.release();owner.release();
    std::puts("live-audio: complete callback owner retained; REBOOT REQUIRED");
    return -EOWNERDEAD;
  }
  if(!rc)rc=voice_fbank_finish(s.bank);
  if(!rc)rc=live_probe_rows(&s);
  if(!rc&&s.next!=(metrics.samples16+80u)/160u)rc=-EIO;
  std::printf("MIC OFF: live status=%d input48=%u output16=%u fbank=%u bins=80 queue-peak=%u "
              "first-pcm16=%lld us elapsed=%lld us endpoint=%u speech=%u\n",
              rc,metrics.samples48,metrics.samples16,s.next,metrics.queue_peak,
              metrics.first_pcm16_us,metrics.elapsed_us,metrics.ended,metrics.speech);
  if(!rc)std::printf("live-features: min=%.6g max=%.6g sum=%.9g; CPU DSP only, NOT transcription\n",
                     double(s.low),double(s.high),s.sum);
  if(!rc&&echo) {
    std::puts("RX transport qualified and stopped; RAM echo playback, NOT TTS.");
    uint32_t frames=0;rc=a733_pcm_stream(live_probe_read,&s,c.volume,&frames);
    if(!rc&&(frames!=metrics.samples48||s.played!=s.used))rc=-EIO;
    std::printf("live-echo: status=%d frames=%u volume=%u%%\n",rc,frames,c.volume);
  }
  if(saved)aipet_wipe(saved.get(),s.capacity);
  return rc;
}

static int capture(unsigned ms,const AudioConfig &c,bool recognize,
                    bool converse,bool features=false,bool local=false)
{
  if(local && ((voice_local_capabilities()&(converse?3u:1u))!=(converse?3u:1u))) {
    std::puts("local-voice: target backend not installed; no recording, upload or cloud fallback.");return -ENODEV;
  }
  if (recognize && !local && !engine_ready()) return -ENODEV;
  if (pthread_mutex_trylock(&operation_lock)) return -EBUSY;
  std::unique_ptr<unsigned char[]> pcm(new(std::nothrow) unsigned char[ms*96u]);
  std::unique_ptr<char[]> text(new(std::nothrow) char[4097]());
  std::size_t bytes=0;
  int result=pcm && text ? 0 : -ENOMEM;
  std::printf("aipet audio: recording=%u ms gain=x%u; %s\n",ms,c.gain,
    recognize?(local?"local ASR only; cloud fallback disabled":"this command uploads PCM to official cloud ASR"):
      "local only, no upload");
  if (!result) result=a733_pcm_capture(ms,c.gain,pcm.get(),ms*96u,&bytes);
  if (!result && interrupted()) result=-ECANCELED;
  if (!result && bytes!=ms*96u) result=-EIO;
  if (!result && features) result=inspect_features(pcm.get(),bytes);
  if (!result && !recognize && !features) result=save_recording(pcm.get(),bytes);
  if (!result && recognize) {
    std::unique_ptr<unsigned char[]> down(new(std::nothrow) unsigned char[bytes/3]);
    std::size_t length=0;
    result=down ? audio_decimate(pcm.get(),bytes,down.get(),bytes/3,&length) : -ENOMEM;
    if (!result) {
      std::printf("aipet audio: ASR input=16000 Hz mono S16, bytes=%zu\n",length);
      result=local?voice_local_recognize(down.get(),length,text.get(),4097,local_cancel,nullptr):
        aipet_cloud_asr(down.get(),length,text.get(),4097);
      if (!result && interrupted()) result=-ECANCELED;
      Reply checked;
      if (!result && (!std::memchr(text.get(),0,4097) ||
                      !parse_reply(text.get(),checked))) result=-EILSEQ;
      if (!result) std::printf("[asr] %s\n",text.get());
    }
    if (down) aipet_wipe(down.get(),bytes/3);
  }
  if (pcm) aipet_wipe(pcm.get(),ms*96u);
  pthread_mutex_unlock(&operation_lock);
  /* Release capture/ASR ownership before the route presents an I2S reply. */
  if (!result && converse) result=local?routed_offline_ask(text.get()):
    (routed_ask(text.get(),false,false)==0 ? 0 : -EIO);
  if (text) aipet_wipe(text.get(),4097);
  if (!result && !recognize && !features) std::puts("record saved: " AIPET_AUDIO_RECORD " (48k mono S16)");
  return result;
}

static int play_matcha_wav(const char *path,const AudioConfig &c)
{
  int fd=open(path,O_RDONLY|O_NOFOLLOW);
  if (fd<0) return -errno;
  struct stat st;
  int result=fstat(fd,&st)<0 ? -errno : 0;
  if (!result && (!S_ISREG(st.st_mode) || st.st_size<44 || st.st_size>1048576))
    result=-EINVAL;
  std::size_t size=result ? 0 : static_cast<std::size_t>(st.st_size), offset=0;
  std::unique_ptr<unsigned char[]> source,pcm;
  if (!result) {
    source.reset(new(std::nothrow) unsigned char[size]);
    pcm.reset(new(std::nothrow) unsigned char[960000]);
    if (!source || !pcm) result=-ENOMEM;
  }
  while (!result && offset<size) {
    ssize_t n=read(fd,source.get()+offset,size-offset);
    if (n<0 && errno==EINTR) continue;
    if (n<=0) result=n<0 ? -errno : -EIO;
    else offset+=static_cast<std::size_t>(n);
  }
  if (close(fd)<0 && !result) result=-errno;
  std::size_t used=0;
  if (!result) result=matcha_wav_to_48k(source.get(),size,pcm.get(),960000,&used);
  if (!result && interrupted()) result=-ECANCELED;
  if (!result) {
    std::printf("aipet audio: local WAV 22050 -> 48000 Hz, frames=%zu; no inference/upload\n",used/2);
    result=a733_pcm_play(pcm.get(),used,48000,1,c.volume);
  }
  if (source) aipet_wipe(source.get(),size);
  if (pcm) aipet_wipe(pcm.get(),960000);
  return result;
}

#include "voice_kws_input.inc"
#include "voice_frontend_audit.inc"

static int general_wav(const char *path)
{
  const char *phase="input";FrontendFile file;int rc=frontend_read(path,44,1025536,file,&phase);
  const unsigned char *pcm=nullptr;size_t bytes=0;unsigned rate=0;
  if(!rc){phase="wav-format";rc=kws_wave(file.data.get(),file.bytes,&pcm,&bytes,&rate);}
  if(!rc && bytes>rate*16u)rc=-EMSGSIZE; // <=8 seconds, never silently trim
  std::unique_ptr<unsigned char[]> down;
  if(!rc && rate==48000) {
    down.reset(new(std::nothrow) unsigned char[bytes/3]);size_t used=0;
    rc=down?audio_decimate(pcm,bytes,down.get(),bytes/3,&used):-ENOMEM;
    pcm=down.get();bytes=used;
  }
  char text[2048]{};
  if(!rc) {
    std::printf("general-wav-v210: input-ready path=%s bytes=%zu rate=%u; NPU not called yet\n",path,bytes,rate);
    phase="asr-model-or-inference";
    rc=voice_local_recognize(pcm,bytes,text,sizeof(text),local_cancel,nullptr);
  }
  if(rc)std::printf("general-wav-v210: failed-stage=%s error=%d detail=%s path=%s; no transcript/actions/cloud\n",
                    phase,rc,std::strerror(-rc),path);
  if(rc==-ENOTDIR || rc==-ENOENT)
    std::puts("general-wav-v210: check parent directories and exact files; missing intermediate FAT directory also returns ENOTDIR; models are not created/faked");
  std::printf("general-wav: result=%d transcript=%s; explicit FILE input, not microphone/closed-loop acceptance; no actions/cloud\n",rc,rc?"[NONE]":text);
  if(down)aipet_wipe(down.get(),bytes);
  return rc;
}

static int offline_chat(unsigned turns,unsigned ms,unsigned endpoint,const AudioConfig &c)
{
  if(c.output!=1 || !c.volume)return -EACCES;
  int selected=voice_general_tts_selected();if(selected<0)return selected;
  if(!selected) {
    struct stat st{};
    const bool ufs=stat("/ufs/npu/general-asr/asr-encoder.a7tv",&st)==0 && S_ISREG(st.st_mode);
    int rc=voice_general_configure_full(ufs?"/ufs/npu/general-asr":"/data/npu/general-asr",
                                      ufs?"/ufs/npu/general-tts":"/data/npu/general-tts");
    if(rc)return rc; // Missing/incomplete selected assets: no implicit model fallback
  }
  // Real LLM preload before MIC ON. Missing/disabled/invalid models fail here,
  // never record a user who cannot receive a local response. No canned turn.
  std::puts("offline-v190: preflight/warmup; microphone OFF; local only, no actions/cloud");
  if(local_llm_control("preload")) {
    std::puts("offline preflight: LLM preload failed; microphone was NOT opened. Check local status/model and llm-pool-v212; this is NOT a missing-microphone diagnosis.");
    return -EIO;
  }
  VoiceGeneralSession *session=nullptr;
  int rc=voice_general_session_open(&session,local_cancel,nullptr);
  for(unsigned turn=0;!rc && turn<turns;++turn) {
    char transcript[2048]{};
    rc=local_cancel(nullptr);if(rc)break;
    std::printf("offline-v190: turn=%u/%u wait for MIC ON; max=%ums endpoint=%u\n",turn+1,turns,ms,endpoint);
    if(pthread_mutex_trylock(&operation_lock)){rc=-EBUSY;break;}
    rc=voice_general_session_listen(session,ms,c.gain,endpoint,transcript,sizeof(transcript),local_cancel,nullptr);
    pthread_mutex_unlock(&operation_lock);
    if(rc==-EOWNERDEAD){audio_fault.store(rc);break;}
    if(!rc && !*transcript) {
      std::puts("offline-v190: no transcript; no LLM/TTS/actions; next explicit bounded turn");
      continue;
    }
    if(rc==-ENODATA) {std::puts("offline-v190: no speech; session ended without reply");break;}
    if(!rc)rc=local_cancel(nullptr);
    if(!rc) {
      std::printf("[asr] %s\n",transcript);
      // Capture and consumer have joined before synthesis/playback starts.
      // Preserve real local Agent routing, context and phrase-streaming path.
      rc=routed_offline_stream_ask(transcript);
      if(rc==-EOWNERDEAD){audio_fault.store(rc);voice_general_session_retain(session);}
    }
    aipet_wipe(transcript,sizeof(transcript));
  }
  int closed=voice_general_session_close(session);if(!rc)rc=closed;
  std::printf("offline-v190: MIC OFF; session result=%d; runtime fds released unless ownership quarantined\n",rc);
  return rc;
}
#if defined(__NuttX__) || defined(AIPET_TEST_RESIDENT)
static void voice_service_load(unsigned stage)
{
  service_load_since_us=voice_load_now();service_load_stage=stage;
}
static int voice_storage_probe(const char *path)
{
  int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -errno;
  struct stat st{};int rc=0;
  if(fstat(fd,&st)<0)rc=-errno;
  else if(!S_ISREG(st.st_mode) || st.st_size<=0 || st.st_size>512*1024*1024)rc=-EBADMSG;
  else if(std::strstr(path,".ort") && st.st_size!=65033312)rc=-EBADMSG;
  if(close(fd)<0 && !rc)rc=-errno;
  return rc;
}
#endif
static void voice_service_status()
{
  const char *stages[]={"off","preparing-MIC-OFF","calibrating","READY-idle-MIC-ON",
                        "speech-MIC-ON","asr-finalizing-MIC-OFF","reply-MIC-OFF","quarantined-reboot-required","waiting-cloud-MIC-OFF"};
  int stage=service_stage.load();
  std::printf(
#ifdef CONFIG_A733_PRODUCT_V1
#ifdef CONFIG_A733_PRODUCT_V12
    "voice-service-v3.0pre-fix2(v229): "
#else
    "voice-service-v1.1(v219): "
#endif
#else
    "voice-service-v216: "
#endif
    "active=%u pid=%d stage=%s stop=%u error=%d turns=%u empty=%u overlong=%u floor=%u\n",
    service_active.load(),service_pid.load(),stages[stage],service_stop.load(),service_error.load(),
    service_turns.load(),service_empty.load(),service_long.load(),service_floor.load());
#ifdef CONFIG_A733_PRODUCT_V1
  std::printf("product-backend=%s; cloud and local histories are separate RAM sessions\n",audio_product_backend());
  std::printf("product-policy-v228: mode=%s automatic-local-fallback=%u; cloud failures never select local ASR/LLM/TTS\n",product_mode()==ProductMode::cloud?"cloud":product_mode()==ProductMode::local?"local":"auto",product_mode()==ProductMode::automatic);
  std::puts("product-v225: routed cloud/local; half-duplex; explicit run or saved boot consent; desktop bounded commands, no AEC/barge-in/fault bypass; status != acoustic acceptance");
#else
  std::puts("local-only half-duplex; explicit start; no boot recording/AEC/barge-in/cloud/MCU actions; status is not acoustic acceptance");
#endif
  const char *loads[]={"none","spawn-pending","settings","storage-and-TTS",
                       "LLM-preload","ASR-vocoder-prepare","engines-prepared","closing"};
  long long at=voice_load_now();unsigned load=service_load_stage.load();
  std::printf("voice-load-v215: load-stage=%s stage-age-ms=%lld since-start-ms=%lld; elapsed is NOT disk throughput\n",
    loads[load],service_load_since_us.load()?(at-service_load_since_us.load())/1000:0,
    service_started_us.load()?(at-service_started_us.load())/1000:0);
  std::fflush(stdout);
}
static int voice_service_control(int argc,char **argv)
{
  if(argc==1 && !std::strcmp(argv[0],"status")){voice_service_status();return 0;}
  if(argc==1 && !std::strcmp(argv[0],"stop")) {
    service_stop.store(true);
    if(service_active.load() && service_stage.load()==VS_REPLY)(void)local_llm_control("stop");
    std::puts("voice-service: cooperative stop requested; wait for active=0; never task_delete/kill an NPU owner");
    voice_service_status();return 0;
  }
  unsigned floor=400;
  const bool boot=argc==1&&!std::strcmp(argv[0],"start-boot");
  if(argc<1 || argc>2 || (!boot&&std::strcmp(argv[0],"start")) ||
     (argc==2 && !number(argv[1],100,5000,floor))) {
    std::puts("aipet audio service start [rms-floor:100..5000] | stop | status");return 1;
  }
#if defined(__NuttX__) || defined(AIPET_TEST_RESIDENT)
#ifdef CONFIG_A733_PRODUCT_V1
  if(a733_product_start_lock())return 1;
  struct ProductStartUnlock {~ProductStartUnlock(){a733_product_start_unlock();}} start_unlock;
#endif
  pthread_mutex_lock(&service_lock);
  if(service_active.load() || audio_fault.load()
#ifdef CONFIG_A733_PRODUCT_V1
     || product_manual_owner.load() || product_vision_busy()
#endif
     ) {
    pthread_mutex_unlock(&service_lock);voice_service_status();return 1;
  }
  int preflight=
#ifdef CONFIG_A733_PRODUCT_V1
    !boot&&product_mode()==ProductMode::local?local_llm_preflight():0;
#else
    local_llm_preflight();
#endif
  if(preflight) {
    service_error=preflight;service_stage=VS_OFF;service_load_stage=0;
    pthread_mutex_unlock(&service_lock);
    std::printf("voice-service: start rejected before spawning/TTS/NPU; LLM preflight=%d\n",preflight);
    voice_service_status();return 1;
  }
  service_stop=false;service_stage=VS_PREPARING;service_error=0;service_floor=floor;
#ifdef CONFIG_A733_PRODUCT_V1
  product_boot_greeting=boot;
#endif
  service_turns=0;service_empty=0;service_long=0;service_pid=0;service_active=true;
  service_started_us=voice_load_now();voice_service_load(1);
  char name[]="aipet",arg[]="--voice-worker";char *args[]={name,arg,nullptr};pid_t pid=-1;
  // Do not inherit an SSH socket as the long-lived LLM/TTS stdout: closing
  // that session could deliver SIGPIPE or turn streaming printf into a failure.
  // Child stdio is opened BEFORE entering any model/NPU owner code.
  posix_spawn_file_actions_t files;
  int rc=posix_spawn_file_actions_init(&files);bool files_ready=!rc;
  if(!rc)rc=posix_spawn_file_actions_addopen(&files,0,"/dev/null",O_RDONLY,0);
  // Match NSH's proven polling node, NOT the unqualified 16550 IRQ console.
  // Its TX ring can wait indefinitely after polling RX disables UART IER.
  if(!rc)rc=posix_spawn_file_actions_addopen(&files,1,"/dev/a7zconsole",O_WRONLY,0);
  if(!rc)rc=posix_spawn_file_actions_addopen(&files,2,"/dev/a7zconsole",O_WRONLY,0);
  if(!rc)rc=posix_spawn(&pid,name,&files,nullptr,args,nullptr);
  if(files_ready)(void)posix_spawn_file_actions_destroy(&files);
  if(rc){service_error=-rc;service_stage=VS_OFF;service_active=false;}
  // Child publishes its own PID before acquiring any engine. Never overwrite
  // an already-exited child's state from this short-lived NSH parent.
  pthread_mutex_unlock(&service_lock);
  if(!rc)std::puts("voice-service: worker logs on board serial console; use service status from SSH; safe to disconnect this starting shell");
  voice_service_status();return rc?1:0;
#else
  return 1;
#endif
}
#ifdef CONFIG_A733_PRODUCT_V1
#include "audio_product.inc"
#endif
int audio_voice_service_main()
{
#ifdef CONFIG_A733_PRODUCT_V1
  return product_voice_main();
#else
#if defined(__NuttX__) || defined(AIPET_TEST_RESIDENT)
  pthread_mutex_lock(&service_lock);
  bool owner=service_active.load() && service_pid.load()==0;
  if(owner)service_pid=getpid();
  pthread_mutex_unlock(&service_lock);if(!owner)return 1;
  voice_service_load(2);VoiceLoadLog settings("service-settings");
  AudioInterruptGuard signals;AudioConfig c;VoiceGeneralSession *session=nullptr;
  int rc=signals.error?-signals.error:config(c);
  if(!rc && (c.output!=1 || !c.volume))rc=-EACCES;
  if(!rc)rc=local_cancel(nullptr);
  if(!rc)rc=local_llm_preflight(); // recheck inside actual owner before costly ORT init
  settings.finish(rc);
  voice_service_load(3);VoiceLoadLog tts_load("service-storage-TTS");
  int selected=rc?rc:voice_general_tts_selected();if(selected<0)rc=selected;
  if(!rc && !selected) {
    bool mounted=false;
#if defined(__NuttX__) && defined(CONFIG_SYSTEM_A733STORAGE)
    mounted=a733_storage_ready();
#endif
    VoiceModelRoots roots;rc=voice_model_roots(mounted,voice_storage_probe,roots);
    std::printf("voice-load-v215: storage=%s ASR=%s TTS=%s status=%d\n",roots.reason,roots.asr,roots.tts,rc);
    std::fflush(stdout);
    if(!rc)rc=voice_general_configure_full(roots.asr,roots.tts);
  } else if(!rc) {
    std::puts("voice-load-v215: keeping explicitly selected RAM provider; no storage auto-switch");
  }
  tts_load.finish(rc);
  if(!rc)rc=local_cancel(nullptr);
  // One preload per service lifetime. Per-turn preloads would destroy the
  // useful exact-prefix KV cache, even when weights were already resident.
  if(!rc) {
    voice_service_load(4);VoiceLoadLog load("service-LLM-preload");
    if(local_llm_control("preload"))rc=-EIO;
    load.finish(rc);
  }
  if(!rc)rc=local_cancel(nullptr);
  if(!rc) {
    voice_service_load(5);VoiceLoadLog load("service-ASR-vocoder-prepare");
    rc=voice_general_session_open(&session,local_cancel,nullptr);load.finish(rc);
  }
  auto quarantine=[&]() {
    audio_fault=-EOWNERDEAD;voice_general_session_retain(session);
    service_stage=VS_QUARANTINE;service_error=-EOWNERDEAD;
    std::puts("voice-service: uncertain worker ownership; retaining SAME task and engines; REBOOT REQUIRED");
    for(;;)usleep(100000); // No retry/reset/free/descriptor reuse.
  };
  if(rc==-EOWNERDEAD)quarantine();
  if(!rc)voice_service_load(6);
  auto state=[](void *,unsigned event) {
    // Both reporters are outside the hardware FIFO loop. Serialize state+log
    // so a delayed consumer cannot publish MIC ON after RX has already stopped.
    pthread_mutex_lock(&service_lock);
    int stage=service_stage.load();bool announce=false;
    if(event==3){service_stage=VS_FINALIZING;announce=true;}
    else if(event==1 && stage==VS_CALIBRATING){service_stage=VS_IDLE;announce=true;}
    else if(event==2 && (stage==VS_CALIBRATING||stage==VS_IDLE)){service_stage=VS_SPEECH;announce=true;}
    if(announce)std::puts(event==1?"voice-service: READY MIC ON; continuously waiting, up to 400ms RAM pre-roll":
      event==2?"voice-service: speech detected; retaining opening audio":
      "voice-service: MIC OFF; RX stopped, draining/finalizing ASR");
    pthread_mutex_unlock(&service_lock);
  };
  while(!rc && !service_stop.load()) {
    char text[2048]{};service_stage=VS_CALIBRATING;
    if(pthread_mutex_trylock(&operation_lock)){rc=-EBUSY;break;}
    rc=voice_general_session_wait(session,c.gain,service_floor.load(),text,sizeof(text),local_cancel,nullptr,state,nullptr);
    pthread_mutex_unlock(&operation_lock);
    if(rc==-EMSGSIZE){++service_long;rc=0;std::puts("voice-service: utterance exceeds 8s including pre-roll; discarded, no partial LLM turn");}
    else if(!rc && !*text)++service_empty;
    if(!rc && *text && !service_stop.load()) {
      service_stage=VS_REPLY;std::printf("voice-service: MIC OFF (half-duplex); [asr] %s\n",text);
      rc=routed_offline_stream_ask(text);if(!rc)++service_turns;
    }
    aipet_wipe(text,sizeof(text));
    if(rc==-EOWNERDEAD)quarantine();
  }
  voice_service_load(7);VoiceLoadLog cleanup("service-close");
  int closed=voice_general_session_close(session);if(!rc)rc=closed;
  cleanup.finish(closed);
  if(closed==-EOWNERDEAD)quarantine();
  pthread_mutex_lock(&service_lock);
  service_error=rc==-ECANCELED?0:rc;service_stage=VS_OFF;service_active=false;
  pthread_mutex_unlock(&service_lock);
  std::printf("voice-service: MIC OFF; stopped status=%d; owner engines closed\n",rc);
  return rc && rc!=-ECANCELED?1:0;
#else
  return 1;
#endif
#endif
}
int audio_control(int argc,char **argv)
{
#ifdef CONFIG_A733_PRODUCT_V1
  if((product_vision_busy()||a733_product_voice_reserved()) && !(argc==1&&(!std::strcmp(argv[0],"status")||!std::strcmp(argv[0],"local-status"))) &&
      !(argc==2&&!std::strcmp(argv[0],"service")&&(!std::strcmp(argv[1],"status")||!std::strcmp(argv[1],"stop")))) {
    std::puts("product-v1: camera owns NPU/SPI; wait for cleanup=0 before audio; no forced reset");return 1;
  }
#endif
  if(argc && !std::strcmp(argv[0],"service"))return voice_service_control(argc-1,argv+1);
  if(service_active.load() && !(argc==1 && (!std::strcmp(argv[0],"status") || !std::strcmp(argv[0],"local-status")))) {
    std::puts("audio: resident service owns engines; use service stop/status before changing audio");return 1;
  }
  if(audio_fault.load() && !(argc==1 && (!std::strcmp(argv[0],"status") || !std::strcmp(argv[0],"local-status")))) {
    std::puts("audio: worker ownership uncertain; no further audio operations; REBOOT REQUIRED");return 1;
  }
  AudioInterruptGuard signals;
  if (signals.error) return 1;
  if(argc==1 && !std::strcmp(argv[0],"local-status")){voice_local_status();return 0;}
  if(argc==2 && !std::strcmp(argv[0],"general-asr")) {
    int rc=voice_general_configure(argv[1]);
    std::printf("general-asr: configure result=%d\n",rc);return rc?1:0;
  }
  if(argc==3 && !std::strcmp(argv[0],"general-voice")) {
    int rc=voice_general_configure_full(argv[1],argv[2]);
    std::printf("general-voice: configure result=%d\n",rc);return rc?1:0;
  }
  if (argc>0 && !std::strcmp(argv[0],"npuref"))
    return voice_npu_check(argc-1,argv+1)==0?0:1;
  if (argc>0 && !std::strcmp(argv[0],"npukws"))
    return voice_npu_kws(argc-1,argv+1)==0?0:1;
  if (argc>0 && !std::strcmp(argv[0],"kwsinput"))
    return voice_npu_kws_input(argc-1,argv+1)==0?0:1;
  /* Read-only diagnostics/export do not depend on Agent or audio.conf. */
  if ((argc==2 && !std::strcmp(argv[0],"wavinfo")) ||
      (argc==3 && (!std::strcmp(argv[0],"fbankcheck") || !std::strcmp(argv[0],"fbankexport")))) {
    int rc=-EBUSY;
    if(!pthread_mutex_trylock(&operation_lock)) {
      rc=frontend_audit(argv[0],argv[1],argc==3?argv[2]:nullptr);
      pthread_mutex_unlock(&operation_lock);
    }
    if(!rc && interrupted())rc=-ECANCELED;
    std::printf("aipet audio: %s result=%d\n",argv[0],rc);
    return rc==0?0:1;
  }
  AudioConfig c;
  int result=config(c);
  if (result) { std::printf("aipet audio: config error=%d; file unchanged\n",result); return 1; }
  if (argc==0 || (argc==1 && !std::strcmp(argv[0],"status"))) {
    std::printf("audio: output=%s volume=%u%% mic-gain=x%u node=%s config=%s\n",
      c.output==0?"uart":c.output==1?"i2s":"off",c.volume,c.gain,
      a733_pcm_available()==0?"present":"missing",config_path);
    aipet_cloud_status();
    voice_local_status();
    voice_service_status();
    return 0;
  }
  if(argc>=2 && argc<=4 && !std::strcmp(argv[0],"offline")) {
    unsigned turns=0,ms=8000,endpoint=400;
    if(!number(argv[1],1,10,turns) || (argc>2&&!number(argv[2],100,8000,ms)) ||
       (argc>3&&!number(argv[3],0,5000,endpoint)))result=-EINVAL;
    else result=offline_chat(turns,ms,endpoint,c);
  } else if(argc==2 && !std::strcmp(argv[0],"general-wav")) {
    if(pthread_mutex_trylock(&operation_lock))result=-EBUSY;
    else {result=general_wav(argv[1]);pthread_mutex_unlock(&operation_lock);}
  } else if (argc==2 && (!std::strcmp(argv[0],"output") ||
                 !std::strcmp(argv[0],"volume") || !std::strcmp(argv[0],"gain"))) {
    pthread_mutex_lock(&config_lock);
    result=load_config(c);
    if (!result && !std::strcmp(argv[0],"output")) {
      if (!std::strcmp(argv[1],"uart")) result=-ENOTSUP;
      else if (!std::strcmp(argv[1],"i2s")) c.output=1;
      else if (!std::strcmp(argv[1],"off")) c.output=2;
      else result=-EINVAL;
    } else if (!result && !std::strcmp(argv[0],"volume")) {
      if (!number(argv[1],0,100,c.volume)) result=-EINVAL;
    } else if (!result && !number(argv[1],1,16,c.gain)) result=-EINVAL;
    if (!result) result=save_config(c);
    pthread_mutex_unlock(&config_lock);
    if (!result) std::puts("audio: saved; applies next operation and survives reboot.");
  } else if (argc>=1 && argc<=2 && !std::strcmp(argv[0],"setup")) {
    if (!engine_ready()) result=-ENODEV;
    else if (pthread_mutex_trylock(&operation_lock)) result=-EBUSY;
    else { result=aipet_voice_provider_setup(argc==2?argv[1]:"volc"); pthread_mutex_unlock(&operation_lock); }
  } else if((argc==1||argc==3)&&!std::strcmp(argv[0],"providers")) {
    if(argc==3) {
      if(pthread_mutex_trylock(&operation_lock))result=-EBUSY;
      else {result=aipet_voice_provider_select(argv[1],argv[2]);pthread_mutex_unlock(&operation_lock);}
    }
    aipet_cloud_status();
#ifdef CONFIG_A733_PRODUCT_V1
  } else if(argc==2&&!std::strcmp(argv[0],"cloud-speak")) {
    result=audio_i2s_cloud_speak(argv[1]);
#endif
  } else if (argc==2 && (!std::strcmp(argv[0],"play") || !std::strcmp(argv[0],"play22k"))) {
    if (pthread_mutex_trylock(&operation_lock)) result=-EBUSY;
    else {
      result=!std::strcmp(argv[0],"play22k") ? play_matcha_wav(argv[1],c) :
             a733_pcm_play_file(argv[1],c.volume);
      pthread_mutex_unlock(&operation_lock);
    }
  } else if(argc==3&&!std::strcmp(argv[0],"loop")) {
    unsigned ms=0,turns=0;
    if(!number(argv[1],1000,10000,ms)||!number(argv[2],1,20,turns))result=-EINVAL;
    else {
      std::printf("MIC ON: %u explicit cloud-ASR turns of %u ms; Ctrl+C cancels; no auto-arm.\n",turns,ms);
      for(unsigned i=0;i<turns&&!result;i++) {
        if(interrupted()){result=-ECANCELED;break;}
        std::printf("voice turn %u/%u\n",i+1,turns);
        result=capture(ms,c,true,true);
      }
      std::puts("MIC OFF: bounded voice loop ended.");
    }
  } else if ((argc==2||argc==3)&&(!std::strcmp(argv[0],"stream-features")||
              !std::strcmp(argv[0],"stream-endpoint")||!std::strcmp(argv[0],"stream-echo"))) {
    unsigned ms=0,threshold=!std::strcmp(argv[0],"stream-endpoint")?400:0;
    if(!number(argv[1],100,8000,ms)||(argc==3&&
        (std::strcmp(argv[0],"stream-endpoint")||!number(argv[2],100,5000,threshold))))result=-EINVAL;
    else result=live_probe(ms,threshold,!std::strcmp(argv[0],"stream-echo"),c);
  } else if ((argc==2 || argc==3) && (!std::strcmp(argv[0],"general-stream") ||
                                     !std::strcmp(argv[0],"general-listen"))) {
    bool converse=!std::strcmp(argv[0],"general-listen");
    unsigned ms=0,endpoint=400;
    if(!number(argv[1],100,8000,ms) || (argc==3&&!number(argv[2],0,5000,endpoint)))result=-EINVAL;
    else if(converse && (c.output!=1 || !c.volume))result=-EACCES;
    else if(converse && voice_general_tts_selected()!=1)result=-ENODEV;
    else if(pthread_mutex_trylock(&operation_lock))result=-EBUSY;
    else {
      char transcript[2048]{};
      result=voice_general_live(ms,c.gain,endpoint,transcript,sizeof(transcript),local_cancel,nullptr);
      if(result==-EOWNERDEAD)audio_fault.store(result);
      if(!result)std::printf("general-asr transcript: %s\n",transcript);
      pthread_mutex_unlock(&operation_lock);
      // ASR clients, capture thread and I2S ownership are released BEFORE
      // starting LLM/streaming TTS. Provisional tokens never execute actions.
      if(!result && converse)result=*transcript?routed_offline_stream_ask(transcript):-ENODATA;
    }
  } else if ((argc==1 || argc==2) &&
             (!std::strcmp(argv[0],"local-asr") || !std::strcmp(argv[0],"local-listen"))) {
    unsigned ms=5000;bool converse=!std::strcmp(argv[0],"local-listen");
    if(argc==2 && !number(argv[1],100,8000,ms))result=-EINVAL;
    else if(converse && (c.output!=1 || !c.volume))result=-EACCES;
    else result=capture(ms,c,true,converse,false,true);
  } else if (argc==2 && !std::strcmp(argv[0],"local-speak-stream")) {
    result=audio_i2s_local_stream_speak(argv[1]);
  } else if (argc==2 && !std::strcmp(argv[0],"local-dialog-stream")) {
    result=routed_offline_stream_ask(argv[1]);
  } else if (argc==2 && !std::strcmp(argv[0],"local-speak")) {
    result=audio_i2s_local_speak(argv[1]);
  } else if (argc==2 && !std::strcmp(argv[0],"speak")) {
    result=c.output==1 ? audio_i2s_speak(argv[1]) : -EACCES;
  } else if (argc==3 && !std::strcmp(argv[0],"kwswav")) {
    if(pthread_mutex_trylock(&operation_lock))result=-EBUSY;
    else {result=kws_wav_file(argv[1],argv[2]);pthread_mutex_unlock(&operation_lock);}
  } else if ((argc==2 || argc==3) && !std::strcmp(argv[0],"kwsmic")) {
    unsigned ms=5000;
    if(argc==3 && !number(argv[2],500,10000,ms))result=-EINVAL;
    else if(pthread_mutex_trylock(&operation_lock))result=-EBUSY;
    else {result=kws_mic(argv[1],ms,c.gain);pthread_mutex_unlock(&operation_lock);}
  } else if ((argc==1 || argc==2) &&
             (!std::strcmp(argv[0],"record") || !std::strcmp(argv[0],"asr") ||
              !std::strcmp(argv[0],"listen") || !std::strcmp(argv[0],"features"))) {
    unsigned ms=5000;
    if (argc==2 && !number(argv[1],100,10000,ms)) result=-EINVAL;
    else result=capture(ms,c,!std::strcmp(argv[0],"asr") || !std::strcmp(argv[0],"listen"),
                         !std::strcmp(argv[0],"listen"),!std::strcmp(argv[0],"features"));
  } else {
    std::puts("aipet audio status | setup [volc|xfyun|tencent|edge] (voice credentials hidden/encrypted)\n"
              "aipet audio providers [asr|tts <volc|xfyun|tencent|edge>] (edge=TTS companion only)\n"
              "aipet audio cloud-speak \"text\" (product build: selected cloud TTS, even if local TTS is configured)\n"
              "aipet audio stream-features|stream-echo 100..8000 (CPU frontend / RAM echo, NOT ASR/TTS)\n"
              "aipet audio stream-endpoint 100..8000 [rms:100..5000] (diagnostic energy detector only)\n"
              "aipet audio local-status | local-asr [100..8000 ms] | local-listen [ms]\n"
              "aipet audio general-asr /data/npu/general-asr|/ufs/npu/general-asr|off (RAM-only candidate)\n"
              "aipet audio general-voice <asr-dir> <tts-dir> (RAM-only full candidate; no auto capture)\n"
              "aipet audio offline <1..10 turns> [100..8000 ms] [endpoint:0..5000] (preload + local ASR/LLM/TTS; no actions/cloud)\n"
              "aipet audio general-wav /data/file.wav (explicit file ASR, <=8s mono S16 16/48k; no capture/actions)\n"
              "aipet audio general-stream 100..8000 [endpoint-rms:0..5000] (live NPU ASR candidate, no actions)\n"
              "aipet audio general-listen 100..8000 [endpoint-rms:0..5000] (NPU ASR->local LLM->CPU acoustic/NPU vocoder; no actions/cloud)\n"
              "aipet audio local-speak|local-speak-stream \"text\" (requires qualified local provider; NEVER cloud)\n"
              "aipet audio local-dialog-stream \"text\" (plain-text LLM->TTS overlap; actions disabled)\n"
              "aipet audio output i2s|off | volume 0..100 | gain 1..16 (UART4 reserved for MCU)\n"
              "aipet audio record [ms] (local WAV only) | play /data/file.wav\n"
              "aipet audio features [ms] (local CPU fbank only; no ASR/upload/file)\n"
              "aipet audio npuref model /data/model.a7tv /data/input-dir [dynamic] (explicit NPU checkpoint)\n"
              "aipet audio npukws <model-dir> <features.f32> [auto|cpu6|cpu7] [quiet] [fullclean] [crclegacy] (default cpu6; no mic)\n"
              "aipet audio kwsinput <model-dir> <features.f32> [same options] (experimental unknown-input diagnostic)\n"
              "aipet audio kwswav <model-dir> </data/mono-S16.wav> (16k/48k; buffered diagnostic)\n"
              "aipet audio kwsmic <model-dir> [500..10000 ms] (record THEN infer; RAM only, no cloud/actions)\n"
              "aipet audio wavinfo </data/mono-S16.wav> (16k/48k; levels/rail-risk, CPU/file only)\n"
              "aipet audio fbankcheck <wav> </data/reference.f32> (strict bytes + maxabs/RMSE; no NPU)\n"
              "aipet audio fbankexport <wav> </data/NEW.f32> (new direct child only; never overwrite)\n"
              "aipet audio play22k /data/matcha.wav (local 22.05k mono S16 -> 48k)\n"
              "aipet audio asr [ms] (cloud upload, text only)\n"
              "aipet audio listen [ms] (selected cloud ASR -> existing Agent route -> selected output)\n"
              "aipet audio loop <1000..10000 ms> <1..20 turns> (explicit bounded cloud voice control)\n"
              "aipet audio speak \"text\" (explicit general-voice uses local TTS; otherwise legacy cloud)");
    return 1;
  }
  if (!result && interrupted()) result=-ECANCELED;
  std::printf("aipet audio: %s result=%d\n",argv[0],result);
  return result==0 ? 0 : 1;
}
}
