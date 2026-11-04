/* SPDX-License-Identifier: Apache-2.0 */
/* POSIX facade compiled with the product, not MMIO in an application. */
#include <system/a733_pcm.h>
#include <sys/a733_audio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

static int transfer(int command, void *request)
{
  int fd = open("/dev/a733-audio", O_RDWR);
  if (fd < 0) return -errno;
  int result = ioctl(fd, command, (unsigned long)(uintptr_t)request);
  if (result < 0) result = -errno;
  if (close(fd) < 0 && result == 0) result = -errno;
  return result;
}

int a733_pcm_available(void)
{
  return access("/dev/a733-audio", F_OK) == 0 ? 0 : -errno;
}

int a733_pcm_monitor(unsigned gain,a733_pcm_capture_sink consume,void *cookie,
                     uint64_t *frames,unsigned *ended)
{
  if(!frames || !ended)return -EINVAL;
  *frames=0;*ended=0;
  if(!consume || gain<1 || gain>16)return -EINVAL;
  struct a733_audio_monitor_s r={A733_AUDIO_ABI,gain,consume,cookie,0,0};
  int rc=transfer(A733_AUDIO_MONITOR,&r);
  *frames=r.frames;*ended=r.ended;return rc;
}

int a733_pcm_capture(unsigned ms, unsigned gain, unsigned char *pcm,
                     size_t capacity, size_t *bytes)
{
  if (!bytes) return -EINVAL;
  *bytes = 0;
  if (!pcm || capacity > UINT32_MAX) return -EINVAL;
  struct a733_audio_capture_s request =
    {A733_AUDIO_ABI, ms, gain, (uint32_t)capacity, pcm, 0};
  int result = transfer(A733_AUDIO_CAPTURE, &request);
  if (!result) *bytes = request.bytes;
  return result;
}

int a733_pcm_play(const unsigned char *pcm, size_t bytes, unsigned rate,
                  unsigned channels, unsigned volume)
{
  if (!pcm || bytes > UINT32_MAX) return -EINVAL;
  struct a733_audio_play_s request =
    {A733_AUDIO_ABI, rate, channels, (uint32_t)bytes, pcm, volume};
  return transfer(A733_AUDIO_PLAY, &request);
}

int a733_pcm_play_file(const char *path, unsigned volume)
{
  struct a733_audio_file_s request = {A733_AUDIO_ABI, volume, path};
  return transfer(A733_AUDIO_FILE, &request);
}

int a733_pcm_stream(a733_pcm_reader read,void *cookie,unsigned volume,uint32_t *frames)
{
  if(!frames)return -EINVAL;
  *frames=0;
  if(!read || volume>100)return -EINVAL;
  struct a733_audio_stream_s request={A733_AUDIO_ABI,volume,read,cookie,0};
  int rc=transfer(A733_AUDIO_STREAM,&request);
  *frames=request.frames;return rc;
}

int a733_pcm_capture_stream(unsigned ms,unsigned gain,a733_pcm_capture_sink consume,
                            void *cookie,uint32_t *frames,unsigned *ended)
{
  if(!frames||!ended)return -EINVAL;
  *frames=*ended=0;
  if(!consume||ms<100||ms>8000||gain<1||gain>16)return -EINVAL;
  struct a733_audio_live_s request={A733_AUDIO_ABI,ms,gain,consume,cookie,0,0};
  int rc=transfer(A733_AUDIO_LIVE_CAPTURE,&request);
  *frames=request.frames;*ended=request.ended;return rc;
}
