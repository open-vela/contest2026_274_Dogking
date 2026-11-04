/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <cJSON.h>
#include <mbedtls/base64.h>
#include "aipet_tts_validate.h"
static int check(const char *text,size_t cap,size_t *bytes)
{
  char response[1024]; unsigned char pcm[64]={0};
  assert(strlen(text)<sizeof(response)); strcpy(response,text);
  int result=aipet_tts_validate(response,pcm,cap,bytes);
  assert(!strcmp(response,text)); /* validator restores NDJSON separators */
  if(!result) assert(pcm[0]==0 && pcm[1]==1 && *bytes==4);
  return result;
}
int main(void)
{
  size_t bytes;
  assert(check("{\"code\":0,\"data\":\"AAECAw==\"}\n{\"code\":20000000}\n",64,&bytes)==0);
  assert(check("{\"code\":20000000,\"data\":\"AAECAw==\"}",64,&bytes)==0);
  assert(check("{\"code\":20000000,\"data\":\"AAECAw==\"}",4,&bytes)==0);
  assert(check("{\"code\":0,\"data\":\"AAECAw==\"}\n{\"code\":20000000,\"data\":null}",4,&bytes)==0);
  assert(check("{\"code\":0,\"data\":\"AAECAw==\"}",64,&bytes)<0 && bytes==0);
  assert(check("{\"code\":0,\"data\":\"AAECAw==\"}\n{",64,&bytes)<0 && bytes==0);
  assert(check("{\"code\":20000000,\"data\":\"AAECAw==\"}",3,&bytes)==-EMSGSIZE);
  assert(check("{\"code\":20000000,\"data\":\"AAE=\"}",1,&bytes)==-EMSGSIZE);
  assert(check("{\"code\":20000000,\"data\":\"AA==\"}",64,&bytes)<0);
  assert(check("{\"code\":20000000,\"data\":\"!!!!\"}",64,&bytes)<0);
  assert(check("{\"code\":20000000,\"data\":1}",64,&bytes)<0);
  assert(check("{\"code\":1,\"data\":\"AAECAw==\"}",64,&bytes)<0);
  assert(check("{\"code\":20000000,\"data\":\"AAECAw==\"}\n{\"code\":0}",64,&bytes)<0);
  assert(check("{\"code\":20000000}",64,&bytes)<0);
  puts("PASS: complete TTS response, terminal mandatory, no malformed/overflow/partial prefix, binary length and base64 validation");
}
