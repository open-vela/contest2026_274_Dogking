/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <system/a733_camera_live.h>
#include <string>
#include <cstdio>
namespace aipet {
inline const char *product_coco_name(unsigned id) {
  static const char *const names[]={"人","自行车","汽车","摩托车","飞机","公交车","火车","卡车","船","红绿灯","消防栓","停车标志","停车计时器","长椅","鸟","猫","狗","马","羊","牛","大象","熊","斑马","长颈鹿","背包","雨伞","手提包","领带","行李箱","飞盘","滑雪板","单板滑雪板","球","风筝","棒球棒","棒球手套","滑板","冲浪板","网球拍","瓶子","红酒杯","杯子","叉子","刀","勺子","碗","香蕉","苹果","三明治","橙子","西兰花","胡萝卜","热狗","披萨","甜甜圈","蛋糕","椅子","沙发","盆栽","床","餐桌","马桶","电视","笔记本电脑","鼠标","遥控器","键盘","手机","微波炉","烤箱","烤面包机","水槽","冰箱","书","时钟","花瓶","剪刀","泰迪熊","吹风机","牙刷"};
  static_assert(sizeof(names)/sizeof(*names)==80,"frozen COCO80 model classes");
  return id<80?names[id]:nullptr;
}
inline int product_describe(const a733_live_result &r,std::string &text) {
  if(r.kind!=A733_LIVE_RESULT || a733_live_result_valid(&r))return -1;
  unsigned counts[80]{};unsigned selected=0;
  for(unsigned i=0;i<r.count;i++) {
    if(!product_coco_name(r.boxes[i].class_id))return -1;
    if(r.boxes[i].score_milli>=500){++counts[r.boxes[i].class_id];++selected;}
  }
  text.clear();
  if(!selected){text="这一帧没有检测到足够明确的常见物体，不代表画面里没有东西。";return 0;}
  text="这一帧，模型检测到";unsigned listed=0;
  for(unsigned i=0;i<80;i++)if(counts[i]) {
    if(listed&&listed%2==0)text+="。还有";
    else if(listed)text+="、";
    char n[16];std::snprintf(n,sizeof(n),"%u",counts[i]);text+=n;
    const char *unit=i>=14&&i<=23?"只":i==1||i==2||i==3||i==5||i==6||i==7?"辆":i==4?"架":i==8?"艘":i==56?"把":i==73?"本":i==67?"部":"个";
    text+=unit;text+=product_coco_name(i);++listed;
  }
  text+="。这是模型判断，可能有误。";return 0;
}
}
