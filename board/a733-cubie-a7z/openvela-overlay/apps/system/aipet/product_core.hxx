/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "pet_core.hxx"
#include <cerrno>
namespace aipet {
enum class ProductMode { automatic,local,cloud };
enum class ProductRoute { local,cloud };
inline bool product_cloud_transient(int rc) {
  return rc==-EIO||rc==-ETIMEDOUT||rc==-EHOSTUNREACH||rc==-ENETUNREACH||
    rc==-ENETDOWN||rc==-ECONNRESET||rc==-ECONNREFUSED||rc==-EPIPE||rc==-EAGAIN;
}
struct ProductReadiness {bool network=false,cloud=false,local=false;};
struct ProductChoice {ProductRoute route=ProductRoute::local;int error=0;const char *reason="offline";};
ProductChoice product_choose(ProductMode,const ProductReadiness &);
bool product_vision_intent(const std::string &);
bool product_direct_rule(const Rules &,const std::string &,std::string &);
enum class SettingKind { none,volume,volume_delta,mute,unmute,volume_status,gain };
struct SettingIntent {SettingKind kind=SettingKind::none;int value=0;};
bool product_setting_intent(const std::string &,SettingIntent &);
// A cancellation/deadline must NEVER hide a failed ownership handoff.
int product_capture_outcome(int status,int cause);
struct ProductPorts {
  virtual ~ProductPorts()=default;
  virtual int cancelled(){return 0;}
  virtual int vision(std::string &)=0;
  virtual bool motor(const char *,std::string &,int &)=0;
  virtual bool rule(const char *,std::string &){return false;}
  virtual bool setting(const char *,std::string &,int &){return false;}
  virtual int chat(ProductRoute,const char *,std::string &,bool &spoken)=0;
  virtual int speak(ProductRoute,const Reply &)=0;
};
// No automatic replay/fallback after semantic dispatch: a command or spoken
// prefix may already exist. ASR fallback is allowed BEFORE entering this API.
int product_turn(ProductRoute,const char *,ProductPorts &);
}
