/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "voice_local.hxx"
namespace aipet {
int audio_control(int argc, char **argv);
int audio_voice_service_main();
bool audio_voice_service_active();
bool audio_i2s_selected();
bool audio_muted();
bool audio_product_setting(const char *,std::string &,int &);
int audio_i2s_speak(const char *text);
/* Explicit local-only synthesis. Missing backend is an error, never cloud. */
int audio_i2s_local_speak(const char *text);
int audio_i2s_local_stream_speak(const char *text);
int audio_i2s_local_stream_speak_cancel(const char *,int (*cancel)(void *),void *);
int audio_i2s_local_feed_cancel(VoiceTextNext,void *,VoiceLocalCancel,void *);
/* Offline 48k ->16k mono S16LE FIR decimator, no rate relabeling. */
int audio_decimate(const unsigned char *source, std::size_t bytes,
                   unsigned char *out, std::size_t capacity, std::size_t *used);
}
