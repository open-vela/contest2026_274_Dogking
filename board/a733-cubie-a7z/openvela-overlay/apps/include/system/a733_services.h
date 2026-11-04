/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APPS_INCLUDE_SYSTEM_A733_SERVICES_H
#define __APPS_INCLUDE_SYSTEM_A733_SERVICES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define A733_AUTH_ITERATIONS 120000
struct a733_ftp_auth
{
  uint32_t version;
  uint32_t port;
  uint32_t iterations;
  unsigned char salt[16];
  unsigned char hash[32];
};
int a733_config_read(const char *name, void *data, size_t size);
int a733_config_write(const char *name, const void *data, size_t size);
int a733_config_remove(const char *name);
bool a733_auto_enabled(const char *service);
int a733_auto_option(const char *service, const char *option);
int a733_ftp_auth_load(struct a733_ftp_auth *auth);
int a733_password_derive(const char *password, const unsigned char salt[16],
                         unsigned char hash[32]);
int a733_spawn_service(const char *program, const char *option);
#endif
