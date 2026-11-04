/* SPDX-License-Identifier: Apache-2.0 */
/* Bounded diagnostic WAV playback, not a standard audio lower-half.
 * Reuses the board-tested S16 half-word TX path. No filesystem I/O while TX
 * is active. Source 8/16/24/48k is repeated to 48k (no pitch relabeling).
 */

#define A733_PLAY_MAX_BYTES (48000u * 2u * 2u * 10u)
#define A733_TX_ERRORS      ((1u << 6) | (1u << 5))

struct a733_wav_s
{
  uint8_t *pcm;
  uint32_t bytes;
  uint32_t rate;
  uint32_t channels;
  uint32_t frames;
};

static uint32_t a733_play_le32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static unsigned int a733_play_le16(const uint8_t *p)
{
  return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static int a733_play_read_all(struct file *filep, void *buffer, size_t size)
{
  uint8_t *cursor = buffer;

  while (size > 0)
    {
      ssize_t count = file_read(filep, cursor, size);
      if (count < 0)
        {
          return (int)count;
        }

      if (count == 0)
        {
          return -EIO;
        }

      cursor += count;
      size -= count;
    }

  return OK;
}

static int a733_play_parse_volume(const char *text, unsigned int *volume)
{
  unsigned int value = 0;

  if (*text < '0' || *text > '9')
    {
      return -EINVAL;
    }

  while (*text >= '0' && *text <= '9')
    {
      value = value * 10u + (unsigned int)(*text++ - '0');
      if (value > 100)
        {
          return -ERANGE;
        }
    }

  if (*text != '\0')
    {
      return -EINVAL;
    }

  *volume = value;
  return OK;
}

static int a733_play_load(const char *path, struct a733_wav_s *wav)
{
  struct file source;
  struct stat info;
  uint8_t header[18];
  uint32_t end;
  uint32_t position = 12;
  bool have_format = false;
  int ret;
  int close_ret;

  memset(wav, 0, sizeof(*wav));
  if (strncmp(path, "/data/", 6) != 0)
    {
      return -EINVAL;
    }

  memset(&source, 0, sizeof(source));
  ret = file_open(&source, path, O_RDONLY, 0);
  if (ret < 0)
    {
      return ret;
    }

  ret = file_fstat(&source, &info);
  if (ret >= 0 && (!S_ISREG(info.st_mode) || info.st_size < 12 ||
                  info.st_size > A733_PLAY_MAX_BYTES + 65536u))
    {
      ret = -EFBIG;
    }

  if (ret >= 0)
    {
      ret = a733_play_read_all(&source, header, 12);
    }

  if (ret >= 0 && (memcmp(header, "RIFF", 4) != 0 ||
                  memcmp(header + 8, "WAVE", 4) != 0 ||
                  a733_play_le32(header + 4) < 4 ||
                  a733_play_le32(header + 4) > (uint64_t)info.st_size - 8u))
    {
      ret = -EINVAL;
    }

  end = ret >= 0 ? a733_play_le32(header + 4) + 8u : 0;
  while (ret >= 0 && position + 8u <= end)
    {
      uint32_t size;
      uint32_t payload = position + 8u;

      if (file_seek(&source, position, SEEK_SET) != position)
        {
          ret = -EIO;
          break;
        }

      ret = a733_play_read_all(&source, header, 8);
      if (ret < 0)
        {
          break;
        }

      size = a733_play_le32(header + 4);
      if (size > end - payload)
        {
          ret = -EINVAL;
          break;
        }

      if (memcmp(header, "fmt ", 4) == 0)
        {
          if (have_format || (size != 16 && size != 18))
            {
              ret = -ENOTSUP;
              break;
            }

          ret = a733_play_read_all(&source, header, size);
          if (ret < 0)
            {
              break;
            }

          wav->channels = a733_play_le16(header + 2);
          wav->rate = a733_play_le32(header + 4);
          if (a733_play_le16(header) != 1 ||
              a733_play_le16(header + 14) != 16 ||
              (size == 18 && a733_play_le16(header + 16) != 0) ||
              (wav->channels != 1 && wav->channels != 2) ||
              (wav->rate != 8000 && wav->rate != 16000 &&
               wav->rate != 24000 && wav->rate != 48000) ||
              a733_play_le16(header + 12) != wav->channels * 2u ||
              a733_play_le32(header + 8) != wav->rate * wav->channels * 2u)
            {
              ret = -ENOTSUP;
              break;
            }

          have_format = true;
        }
      else if (memcmp(header, "data", 4) == 0)
        {
          if (!have_format || size == 0 || size > A733_PLAY_MAX_BYTES ||
              size % (wav->channels * 2u) != 0 ||
              size / (wav->channels * 2u) > wav->rate * 10u)
            {
              ret = -EINVAL;
              break;
            }

          wav->bytes = size;
          wav->frames = size / (wav->channels * 2u);
          wav->pcm = kmm_malloc(size);
          ret = wav->pcm == NULL ? -ENOMEM :
                a733_play_read_all(&source, wav->pcm, size);
          break;
        }

      position = payload + size + (size & 1u);
      if (position > end)
        {
          ret = -EINVAL;
        }
    }

  if (ret >= 0 && wav->pcm == NULL)
    {
      ret = -EINVAL;
    }

  close_ret = file_close(&source);
  if (ret >= 0 && close_ret < 0)
    {
      ret = close_ret;
    }

  if (ret < 0)
    {
      kmm_free(wav->pcm);
      wav->pcm = NULL;
    }

  return ret;
}

static int a733_i2s0_play_pcm(const struct a733_wav_s *input)
{
  const struct a733_wav_s wav = *input;
  uint32_t output_frames;
  uint32_t repeat;
  uint32_t sent = 0;
  uint32_t duration;
  clock_t start;
  int ret;

  strlcpy(g_i2s0.last_operation, "play", sizeof(g_i2s0.last_operation));
  g_i2s0.play_status = -EINPROGRESS;
  g_i2s0.play_sent = g_i2s0.play_frames = g_i2s0.play_elapsed = 0;
  g_i2s0.play_rate = g_i2s0.play_channels = g_i2s0.play_intsta = 0;
  ret = a733_i2s0_hw_initialize();
  if (ret < 0)
    {
      g_i2s0.play_status = ret;
      return ret;
    }

  repeat = A733_AUDIO_RATE / wav.rate;
  output_frames = wav.frames * repeat;
  duration = output_frames * 1000u / A733_AUDIO_RATE;
  g_i2s0.play_rate = wav.rate;
  g_i2s0.play_channels = wav.channels;
  g_i2s0.play_frames = output_frames;
  ret=a733_i2s0_tx_prepare(true);if(ret<0)return ret;
  putreg32(0, A733_I2S0_BASE + I2S_TXCNT);
  putreg32(A733_TX_ERRORS, A733_I2S0_BASE + I2S_INTSTA);
  strlcpy(g_i2s0.stage, "play", sizeof(g_i2s0.stage));
  start = clock_systime_ticks();

  /* Prefill 64 stereo frames before enabling clocks. Mono is sent to BOTH
   * slots, independent of the amplifier's channel-selection strap.
   */

  while (sent < output_frames)
    {
      uint32_t index = (sent / repeat) * wav.channels * 2u;
      int16_t left = (int16_t)a733_play_le16(wav.pcm + index);
      int16_t right = wav.channels == 1 ? left :
        (int16_t)a733_play_le16(wav.pcm + index + 2u);

      ret = a733_i2s0_wait_tx_space();
      if (ret < 0)
        {
          break;
        }

      if ((sent & 255u) == 0 &&
          (TICK2MSEC(clock_systime_ticks() - start) > duration + 200u ||
           (getreg32(A733_I2S0_BASE + I2S_INTSTA) & A733_TX_ERRORS) != 0))
        {
          ret = -EPIPE;
          break;
        }

      left = (int16_t)((int32_t)left * (int32_t)g_i2s0.playback_volume / 100);
      right = (int16_t)((int32_t)right * (int32_t)g_i2s0.playback_volume / 100);
      putreg16((uint16_t)left, A733_I2S0_BASE + I2S_TXFIFO);
      putreg16((uint16_t)right, A733_I2S0_BASE + I2S_TXFIFO);
      sent++;
      a733_i2s0_tx_sample(left);a733_i2s0_tx_sample(right);
      if (sent == 64 || (sent == output_frames && sent < 64))
        {
          /* FIFO already contains valid PCM. Return PB7 from capture/idle
           * GPIO-low to DOUT0 before enabling the TX serializer/clocks.
           */

          ret=a733_i2s0_tx_begin();if(ret<0)break;
        }
    }

  if (ret >= 0 &&
      (getreg32(A733_I2S0_BASE + I2S_INTSTA) & A733_TX_ERRORS) != 0)
    {
      ret = -EPIPE;
    }

  if (ret >= 0)
    {
      clock_t drain = clock_systime_ticks();
      while (I2S_FIFO_TX_COUNT(getreg32(A733_I2S0_BASE + I2S_FIFOSTA)) < 128)
        {
          if (TICK2MSEC(clock_systime_ticks() - drain) > 100)
            {
              ret = -ETIMEDOUT;
              break;
            }
        }

      /* FIFO empty can precede the last serialized word. */

      up_udelay(50);
    }

  g_i2s0.tx_count = getreg32(A733_I2S0_BASE + I2S_TXCNT);
  g_i2s0.play_intsta = getreg32(A733_I2S0_BASE + I2S_INTSTA);
  g_i2s0.play_elapsed = TICK2MSEC(clock_systime_ticks() - start);
  g_i2s0.play_sent = sent;
  if (ret >= 0 && (sent != output_frames ||
                  (g_i2s0.play_intsta & (1u << 5)) != 0 ||
                  g_i2s0.play_elapsed + 20u < duration ||
                  g_i2s0.play_elapsed > duration + 200u))
    {
      ret = -EIO;
    }

  /* TX underrun after deliberate final drain may latch; it is reported raw,
   * not used as an invented TXCNT scaling rule or an acoustic success flag.
   */

  a733_i2s0_stream_stop();
  g_i2s0.play_status = ret;
  g_i2s0.last_status = ret;
  g_i2s0.plays += ret >= 0;
  a733_i2s0_probe();
  return ret;
}

static int a733_i2s0_play_stream(struct a733_audio_stream_s *in)
{
  uint8_t pcm[960];
  uint32_t bytes=0,sent=0,index=0;
  bool enabled=false;
  int ret;
  in->frames=0;
  if(in->version!=A733_AUDIO_ABI || !in->read || in->volume>100)return -EINVAL;
  /* No callback needs to run in interrupt context. The initial bounded wait
   * for a producer's prebuffer occurs before clocks/DOUT are enabled. */
  ret=in->read(in->cookie,pcm,sizeof(pcm),&bytes);
  if(ret)return ret<0?ret:-EIO;
  if(!bytes)return -ENODATA;
  if(bytes>sizeof(pcm)||bytes%2)return -EINVAL;
  ret=a733_i2s0_hw_initialize();if(ret<0)return ret;
  g_i2s0.playback_volume=in->volume;
  ret=a733_i2s0_tx_prepare(true);if(ret<0)return ret;
  putreg32(0,A733_I2S0_BASE+I2S_TXCNT);
  putreg32(A733_TX_ERRORS,A733_I2S0_BASE+I2S_INTSTA);
  strlcpy(g_i2s0.last_operation,"stream-play",sizeof(g_i2s0.last_operation));
  strlcpy(g_i2s0.stage,"stream-play",sizeof(g_i2s0.stage));
  clock_t start=clock_systime_ticks();
  for(;;)
    {
      if(index==bytes)
        {
          bytes=0;index=0;
          ret=in->read(in->cookie,pcm,sizeof(pcm),&bytes);
          if(ret){if(ret>0)ret=-EIO;break;}
          if(!bytes)break;
          if(bytes>sizeof(pcm)||bytes%2){ret=-EINVAL;break;}
        }
      if(sent>=480000 || TICK2MSEC(clock_systime_ticks()-start)>10200)
        {ret=-E2BIG;break;}
      if((sent&255u)==0 && (getreg32(A733_I2S0_BASE+I2S_INTSTA)&A733_TX_ERRORS))
        {ret=-EPIPE;break;}
      ret=a733_i2s0_wait_tx_space();if(ret<0)break;
      int16_t sample=(int16_t)a733_play_le16(pcm+index);index+=2;
      sample=(int16_t)((int32_t)sample*(int32_t)in->volume/100);
      putreg16((uint16_t)sample,A733_I2S0_BASE+I2S_TXFIFO);
      putreg16((uint16_t)sample,A733_I2S0_BASE+I2S_TXFIFO);
      ++sent;
      a733_i2s0_tx_sample(sample);
      if(sent==64)
        {
          ret=a733_i2s0_tx_begin();if(ret<0)break;
          enabled=true;
        }
    }
  if(ret>=0 && sent)
    {
      if(!enabled)
        {
          ret=a733_i2s0_tx_begin();
        }
      clock_t drain=clock_systime_ticks();
      while(I2S_FIFO_TX_COUNT(getreg32(A733_I2S0_BASE+I2S_FIFOSTA))<128)
        if(TICK2MSEC(clock_systime_ticks()-drain)>100){ret=-ETIMEDOUT;break;}
      up_udelay(50);
    }
  in->frames=sent;
  g_i2s0.play_rate=48000;g_i2s0.play_channels=1;
  g_i2s0.play_sent=sent;g_i2s0.play_frames=sent;
  g_i2s0.play_elapsed=TICK2MSEC(clock_systime_ticks()-start);
  g_i2s0.tx_count=getreg32(A733_I2S0_BASE+I2S_TXCNT);
  g_i2s0.play_intsta=getreg32(A733_I2S0_BASE+I2S_INTSTA);
  /* A deliberate final drain may latch TX-underrun (bit 6); overflow or
   * impossible pacing still fails, as on the established whole-PCM path. */
  if(ret>=0 && (!sent || (g_i2s0.play_intsta&(1u<<5)) ||
                 g_i2s0.play_elapsed+20u<sent/48u ||
                 g_i2s0.play_elapsed>sent/48u+200u))ret=-EIO;
  a733_i2s0_stream_stop();
  g_i2s0.play_status=ret;g_i2s0.last_status=ret;g_i2s0.plays+=ret>=0;
  return ret;
}

static int a733_i2s0_play_wav(const char *path)
{
  struct a733_wav_s wav;
  int ret = a733_play_load(path, &wav);
  if (ret < 0)
    {
      strlcpy(g_i2s0.last_operation, "play", sizeof(g_i2s0.last_operation));
      g_i2s0.play_status = ret;
      g_i2s0.play_sent = g_i2s0.play_frames = g_i2s0.play_elapsed = 0;
      g_i2s0.play_rate = g_i2s0.play_channels = g_i2s0.play_intsta = 0;
      return ret;
    }

  ret = a733_i2s0_play_pcm(&wav);
  kmm_free(wav.pcm);
  return ret;
}
