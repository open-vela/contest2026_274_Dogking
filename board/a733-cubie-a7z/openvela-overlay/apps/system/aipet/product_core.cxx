/* SPDX-License-Identifier: Apache-2.0 */
#include "product_core.hxx"
#include "mcu_protocol.hxx"
#include <cerrno>
#include <cstdio>
namespace aipet {
int product_capture_outcome(int status,int cause) {
  return status==-EOWNERDEAD?status:cause?cause:status;
}
ProductChoice product_choose(ProductMode mode,const ProductReadiness &r) {
  if(mode==ProductMode::cloud)return {ProductRoute::cloud,r.network&&r.cloud?0:-ENETUNREACH,r.network?(r.cloud?"forced-cloud":"cloud-not-configured"):"network-down"};
  if(mode==ProductMode::local)return {ProductRoute::local,r.local?0:-ENODEV,"forced-local"};
  if(r.network&&r.cloud)return {ProductRoute::cloud,0,"network-and-cloud-configured"};
  return {ProductRoute::local,r.local?0:-ENODEV,r.network?"cloud-unconfigured-or-cooldown":"network-down"};
}
bool product_vision_intent(const std::string &text) {
  std::string s=command_text(text);
  for(auto p:{"小派，","小派","请你","请"})if(s.compare(0,std::char_traits<char>::length(p),p)==0)s.erase(0,std::char_traits<char>::length(p));
  for(bool again=true;again;){again=false;for(auto p:{"。","？","?","！","!"," "}) {
    auto n=std::char_traits<char>::length(p);if(s.size()>=n&&s.compare(s.size()-n,n,p)==0){s.resize(s.size()-n);again=true;}}}
  for(auto p:{"识别物体","识别一下物体","看看这是什么","这是什么","你看到了什么","前面有什么","前面有个什么物体","前面有什么物体","识别眼前的物体"})if(s==p)return true;
  for(auto p:{"识图","识别图片","识别图像","看看前面","看看前面是什么","看看这是什么东西","这是什么东西","前面是什么","前面是什么东西","前面有什么东西","看看面前有什么","面前有什么","识别一下","拍照识别","打开摄像头","打开摄像头识别","用摄像头识别"})if(s==p)return true;
  for(auto verb:{"识别","识别一下","看看","看一下","看一看","拍照识别","打开摄像头识别","用摄像头识别"})
    for(auto object:{"前面物体","前面的物体","眼前物体","眼前的物体","面前的物体","这个物体","这是什么物体","物体","图片","这张图片","图像","画面","摄像头画面","这是什么","周围环境","前面有什么"})
      if(s==std::string(verb)+object)return true;
  return false;
}
bool product_setting_intent(const std::string &text,SettingIntent &out) {
  const auto s=command_text(text);out={};
  for(auto p:{"大声一点","大点声","声音大一点","声音调大","调大声音","把声音调大","把音量调大","调大音量","音量调大","音量增加","提高音量"})if(s==p){out={SettingKind::volume_delta,10};return true;}
  for(auto p:{"小声一点","小点声","声音小一点","声音调小","调小声音","把声音调小","把音量调小","调小音量","音量调小","音量降低","降低音量"})if(s==p){out={SettingKind::volume_delta,-10};return true;}
  for(auto p:{"静音","关闭声音","关闭扬声器","不要出声"})if(s==p){out.kind=SettingKind::mute;return true;}
  for(auto p:{"取消静音","恢复声音","打开声音","打开扬声器"})if(s==p){out.kind=SettingKind::unmute;return true;}
  for(auto p:{"当前音量","音量多少","现在音量多少","查看音量","查询音量"})if(s==p){out.kind=SettingKind::volume_status;return true;}
  for(auto prefix:{"把音量调到","把音量设置为","音量调到","音量调成","音量设置为","设置音量为","设置音量","调节音量","调音量到","音量","麦克风增益调到","麦克风增益"}) {
    std::string p=prefix;if(s.compare(0,p.size(),p)!=0)continue;
    std::string number=s.substr(p.size());bool gain=p.find("麦克风")!=std::string::npos;
    if(number.compare(0,9,"百分之")==0)number.erase(0,9);
    if(!number.empty()&&number.back()=='%')number.pop_back();
    unsigned n=0;if(!command_uint(number,gain?16:100,n)||(gain&&!n))return false;
    out={gain?SettingKind::gain:SettingKind::volume,int(n)};return true;
  }
  return false;
}
bool product_direct_rule(const Rules &rules,const std::string &text,std::string &out) {
  std::string s=command_text(text);
  for(unsigned round=0;round<3;round++)for(auto p:{"小派，","小派","请问","请你","请","告诉我","今天","现在"}) {
    auto n=std::char_traits<char>::length(p);if(s.compare(0,n,p)==0)s.erase(0,n);
  }
  for(bool again=true;again;){again=false;for(auto p:{"。","？","?","！","!","，"," ","了","呢","怎么样"}) {
    auto n=std::char_traits<char>::length(p);if(s.size()>=n&&s.compare(s.size()-n,n,p)==0){s.resize(s.size()-n);again=true;}}}
  for(char &c:s)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
  for(const auto &r:rules.direct)for(const auto &word:r.keywords)if((word==s||word==text)&&!r.replies.empty()) {
    out=r.replies.front();return true;
  }
  return false; // No substring matching inside emotional/conversational text.
}
int product_turn(ProductRoute route,const char *text,ProductPorts &ports) {
  Reply input;if(!text || !parse_reply(text,input) || input.text.empty())return -EINVAL;
  int cancelled=ports.cancelled();if(cancelled)return cancelled;
  Reply out;std::string raw;int action=0,rc=0;bool spoken=false;
  if(ports.motor(text,raw,action)) {
    std::printf("product-route-v1: intent=mcu ack-status=%d; never retry motor\n",action);
    // The factual rejection is itself a valid response; do not stop listening.
  } else if(ports.setting(text,raw,action)) {
    std::printf("product-route-v225: intent=setting result=%d\n",action);
  } else if(product_vision_intent(text)) {
    rc=ports.vision(raw);
    if(rc==-EOWNERDEAD||rc==-ECANCELED)return rc;
    if(rc){raw="这次没有拿到有效的摄像头识别结果，请检查摄像头和模型。";std::printf("product-route-v1: vision error=%d; no fabricated detections\n",rc);}
  } else if(ports.rule(text,raw)) {
    std::puts("product-route-v1: intent=direct-rule");
  } else {
    rc=ports.chat(route,text,raw,spoken);if(rc)return rc;
  }
  if(spoken)return 0;
  cancelled=ports.cancelled();if(cancelled)return cancelled;
  if(!parse_reply(raw,out) || !out.actions.empty())return -EILSEQ;
  return ports.speak(route,out);
}
}
