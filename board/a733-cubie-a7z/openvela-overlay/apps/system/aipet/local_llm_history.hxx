/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "aipetllm_api.h"
#include <cstring>

namespace aipet {
/* One trusted physical-device session, shared by serial/SSH/local fallback.
 * RAM only, no transcript logging/persistence. Caller holds infer_lock.
 * Fixed storage avoids growing STL allocations on the NSH stack. */
struct LocalHistory {
  struct Pair { char user[4097]; char assistant[4097]; } pairs[AIPETLLM_HISTORY_LIMIT]{};
  unsigned count=0;
  void clear() { std::memset(pairs,0,sizeof(pairs));count=0; }
  void retain(unsigned n) {
    if(n>=count)return;
    std::memmove(pairs,pairs+count-n,n*sizeof(Pair));
    std::memset(pairs+n,0,(AIPETLLM_HISTORY_LIMIT-n)*sizeof(Pair));count=n;
  }
  bool append(const char *user,const char *assistant,unsigned limit) {
    if(!limit)return true;
    if(limit>AIPETLLM_HISTORY_LIMIT || !user || !assistant ||
       !*user || !*assistant || std::strlen(user)>4096 || std::strlen(assistant)>4096)
      return false;
    retain(limit-1);
    std::strcpy(pairs[count].user,user);std::strcpy(pairs[count].assistant,assistant);
    ++count;return true;
  }
  void view(aipetllm_turn *out) const {
    for(unsigned i=0;i<count;++i)out[i]={pairs[i].user,pairs[i].assistant};
  }
};
}
