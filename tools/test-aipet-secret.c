#include "agent_secret.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(void)
{
  char enc[316], again[316], out[128];
  assert(!aipet_secret_seal("test-only-not-a-real-key",enc,sizeof(enc)));
  assert(!strstr(enc,"test-only"));
  assert(!aipet_secret_open(enc,out,sizeof(out)));
  assert(!strcmp(out,"test-only-not-a-real-key"));
  assert(!aipet_secret_seal(out,again,sizeof(again)));
  assert(strcmp(enc,again));
  enc[12]=enc[12]=='0'?'1':'0';
  assert(aipet_secret_open(enc,out,sizeof(out)) && !out[0]);
  assert(aipet_secret_open("a7g1:00",out,sizeof(out)));
  assert(!aipet_secret_open("legacy-key",out,sizeof(out)));
  assert(aipet_secret_open("REPLACE_ME",out,sizeof(out)));
  assert(!unlink(AIPET_KEY_FILE));
  assert(aipet_secret_open(again,out,sizeof(out)));
  puts("secret roundtrip, random nonce, tamper, missing-key and legacy tests passed");
  return 0;
}
