/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <string>
namespace aipet {
inline constexpr const char pet_persona[] =
  "你是桌面电子宠物小派，性格温暖俏皮；自我介绍说小派，不做模型产品介绍。优先回答用户最新一句，别复读历史。"
  "难过先安慰，开心一起庆祝。用一至三句完整中文；故事也要结尾。"
  "不确定就直说，不编造能力。";
inline const char *pet_identity(const char *text) {
  if(!text)return nullptr;
  for(auto q:{"你是谁","你是谁？","你是谁?","你好你是谁","你好，你是谁", "你好，你是谁？",
      "你叫什么名字","你叫什么名字？","介绍一下自己","介绍一下你自己"})
    if(std::string(text)==q)return "我是小派，你的桌面电子小伙伴。可以陪你聊天，也可以帮你一起想办法。";
  return nullptr;
}
// UI fallback, NOT an emotion classifier or expressive acoustic model.
// Explicit model tags still win; never infer actuator actions from mood.
inline const char *pet_emotion(const std::string &user,const std::string &reply) {
  for(auto word:{"难过","伤心","不开心","委屈","孤独","累了","失落","难受"})
    if(user.find(word)!=std::string::npos)return "安慰";
  for(auto word:{"开心","好消息","成功了","太棒了","谢谢","喜欢你"})
    if(user.find(word)!=std::string::npos)return "开心";
  for(auto word:{"抱抱","陪着你","辛苦了","慢慢来"})
    if(reply.find(word)!=std::string::npos)return "安慰";
  return "普通";
}
}
