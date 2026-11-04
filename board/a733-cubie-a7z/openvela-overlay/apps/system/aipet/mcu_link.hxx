/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "mcu_protocol.hxx"
namespace aipet {
int mcu_control(int argc,char **argv);
int mcu_worker_main();
void mcu_desktop_session(bool enabled);
int mcu_request(const McuCommand &,std::string &result);
int mcu_desktop_request(const McuCommand &,std::string &result);
bool mcu_route(const char *text,bool inspect,std::string &reply,int *status=nullptr);
void mcu_turn_begin(const char *);
void mcu_turn_end();
bool mcu_turn_used();
bool mcu_turn_report(std::string &);
}
extern "C" void aipet_mcu_tools_register(void);
