/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "product_core.hxx"
#include "voice_local.hxx"
namespace aipet {
bool product_network();
bool product_cloud_configured(); // Ready credentials/Agent/time, independent of AUTO cooldown.
bool product_cloud_ready();
void product_cloud_failed();
void product_cloud_succeeded();
bool product_cloud_cooling();
int product_cloud_recover(VoiceLocalCancel,void *);
ProductMode product_mode();
int product_control(int,char **);
bool product_vision_busy();
int product_capture_objects(std::string &,VoiceLocalCancel,void *);
int product_request(ProductRoute,const char *,VoiceLocalCancel,void *);
int product_text_ask(const char *);
int product_pending_vision(const char *); // explicit fresh-camera consent
int audio_i2s_cloud_speak(const char *);
int audio_product_prepare_local();
const char *audio_product_backend();
int audio_product_manual_begin();
void audio_product_manual_end();
void product_screen_resume();
void product_screen_open();
int product_screen_pause();
int product_screen_close();
}
