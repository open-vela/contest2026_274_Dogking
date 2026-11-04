/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "voice_local.hxx"
#include "audio_capture_live.hxx"
namespace aipet {
/* Explicit experimental configuration, RAM only. No inference/recording or
 * NPU fd at install; no cloud or keyword fallback. */
int voice_general_configure(const char *directory); // "off" uninstalls
/* Explicit full candidate: loads immutable CPU assets, no NPU execution or
 * capture at install. Busy/failed replacement retains the previous provider. */
int voice_general_configure_full(const char *asr_directory,const char *tts_directory);
/* Read-only 1/0; a prepared resident lease is readable ONLY by its exact
 * owner thread/task. Foreign access remains EBUSY; retained owner is fatal.
 * Never infer busy as cloud mode or reconfigure an owner's live engines. */
int voice_general_tts_selected();
struct VoiceGeneralSession;
/* Open/listen/close on the SAME owner thread and task group. Bounded NSH
 * commands close before exit; resident mode owns a separately spawned live
 * task, never borrowed NSH fds. Replacement is rejected throughout a lease,
 * including idle and the interval between ASR and TTS. */
int voice_general_session_open(VoiceGeneralSession **,VoiceLocalCancel,void *);
int voice_general_session_listen(VoiceGeneralSession *,unsigned ms,unsigned gain,
    unsigned endpoint,char *,std::size_t,VoiceLocalCancel,void *);
int voice_general_session_close(VoiceGeneralSession *);
/* Same dedicated owner task remains alive for continuous idle/multiple turns.
 * Returns EMSGSIZE for a transport-qualified but overlong, discarded turn. */
int voice_general_session_wait(VoiceGeneralSession *,unsigned gain,unsigned floor,
    char *,std::size_t,VoiceLocalCancel,void *,LiveVoiceState,void *);
void voice_general_session_retain(VoiceGeneralSession *); // downstream join failed: pin until reboot
int voice_general_live(unsigned ms,unsigned gain,unsigned endpoint_rms,
                        char *text,std::size_t capacity,VoiceLocalCancel,void *);
}
