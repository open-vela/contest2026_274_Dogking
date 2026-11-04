/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Cloud only. Never switches to a local implementation or reuses LLM keys. */
int aipet_cloud_asr(const unsigned char *,size_t,char *,size_t);
int aipet_cloud_tts(const char *,unsigned char *,size_t,size_t *);
typedef int (*aipet_cloud_cancel_fn)(void *);
int aipet_cloud_asr_cancel(const unsigned char *,size_t,char *,size_t,aipet_cloud_cancel_fn,void *);
int aipet_cloud_tts_cancel(const char *,unsigned char *,size_t,size_t *,aipet_cloud_cancel_fn,void *);
int aipet_cloud_ready(void);
void aipet_cloud_status(void);
int aipet_cloud_provider_valid(const char *kind,const char *name);
int aipet_voice_provider_setup(const char *provider);
int aipet_voice_provider_select(const char *kind,const char *provider);
/* C-only config compatibility adapter; decrypts only explicit voice secrets. */
int aipet_cloud_get(const char *key,char *out,size_t cap,int secret);
#ifdef __cplusplus
}
#endif
