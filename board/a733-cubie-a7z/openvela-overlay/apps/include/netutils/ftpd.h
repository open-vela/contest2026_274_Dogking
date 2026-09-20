/****************************************************************************
 * apps/include/netutils/ftpd.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __APPS_INCLUDE_NETUTILS_FTPD_H
#define __APPS_INCLUDE_NETUTILS_FTPD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Configuration ************************************************************/

/**
 * @cond
 * Required configuration settings:  Of course TCP networking support is
 * required.  But here are a couple that are less obvious:
 *
 *   CONFIG_DISABLE_PTHREAD - pthread support is required
 *
 * Other FTPD configuration options that may be of interest:
 *
 *   CONFIG_FTPD_VENDORID - The vendor name to use in FTP communications.
 *     Default: "NuttX"
 *   CONFIG_FTPD_SERVERID - The server name to use in FTP communications.
 *     Default: "NuttX FTP Server"
 *   CONFIG_FTPD_CMDBUFFERSIZE - The maximum size of one command.  Default:
 *     128 bytes.
 *   CONFIG_FTPD_DATABUFFERSIZE - The size of the I/O buffer for data
 *     transfers.  Default: 512 bytes.
 *   CONFIG_FTPD_WORKERSTACKSIZE - The stacksize to allocate for each
 *     FTP daemon worker thread.  Default:  2048 bytes.
 */

#ifdef CONFIG_DISABLE_PTHREAD
#  error "pthread support is required (CONFIG_DISABLE_PTHREAD=n)"
#endif

#ifndef CONFIG_FTPD_VENDORID
#  define CONFIG_FTPD_VENDORID "NuttX"
#endif

#ifndef CONFIG_FTPD_SERVERID
#  define CONFIG_FTPD_SERVERID "NuttX FTP Server"
#endif

#ifndef CONFIG_FTPD_CMDBUFFERSIZE
#  define CONFIG_FTPD_CMDBUFFERSIZE 128
#endif

#ifndef CONFIG_FTPD_DATABUFFERSIZE
#  define CONFIG_FTPD_DATABUFFERSIZE 512
#endif

#ifndef CONFIG_FTPD_WORKERSTACKSIZE
#  define CONFIG_FTPD_WORKERSTACKSIZE 2048
#endif

/* Interface definitions ****************************************************/

#define FTPD_ACCOUNTFLAG_NONE    (0)
#define FTPD_ACCOUNTFLAG_ADMIN   (1 << 0)
#define FTPD_ACCOUNTFLAG_SYSTEM  (1 << 1)
#define FTPD_ACCOUNTFLAG_GUEST   (1 << 2)

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* This "handle" describes the FTP session */

typedef FAR void *FTPD_SESSION;
typedef bool (*ftpd_auth_callback_t)(FAR const char *user,
                                    FAR const char *password, FAR void *arg);

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Functions Prototypes
 ****************************************************************************/

/**
 * @endcond
 * @brief
 *   Create an instance of the FTPD server and return a handle that can be
 *   used to run the server.
 *
 * @param   port   The port that the server will listen to.
 * @param   family The type of INET family to use when opening the socket.
 *   AF_INET and AF_INET6 are supported.
 *
 * @return
 *   On success, a non-NULL handle is returned that can be used to reference
 *   the server instance.
 */

FTPD_SESSION ftpd_open(int port, sa_family_t family);

/**
 * @brief
 *   Add one FTP user.
 *
 * @param   handle       A handle previously returned by ftpd_open
 * @param   accountflags The characteristics of this user
 *   (see FTPD_ACCOUNTFLAGS_* definitions above).
 * @param   user         The user login name. May be NULL indicating
 *   that no login is required.
 * @param   passwd       The user password.  May be NULL indicating
 *   that no password is required.
 * @param   home         The user home directory. May be NULL.
 *
 * @return
 *   Zero is returned on success.  A negated errno value is return on
 *   failure.
 */

int ftpd_adduser(FTPD_SESSION handle, uint8_t accountflags,
                 FAR const char *user, FAR const char *passwd,
                 FAR const char *home);

/* Optional password verifier for registered accounts. Set before accepting
 * clients; arg must remain valid until ftpd_close finishes draining workers.
 */
int ftpd_set_auth_callback(FTPD_SESSION handle, ftpd_auth_callback_t verify,
                           FAR void *arg);

/**
 * @brief
 *   Execute the FTPD server.  This thread does not return until either (1)
 *   the timeout expires with no connection, (2) some other error occurs, or
 *   (2) a connection was accepted and an FTP worker thread was started to
 *   service the session.  Each call to ftpd_session creates on session.
 *
 * @param  handle  A handle previously returned by ftpd_open
 * @param  timeout A time in milliseconds to wait for a connection. If
 *   this time elapses with no connected, the -ETIMEDOUT error will be
 *   returned.
 *
 * @return
 *   Zero is returned if the FTP worker was started.  On failure, a negated
 *   errno value is returned to indicate why the server terminated.
 *   -ETIMEDOUT indicates that the user-provided timeout elapsed with no
 *   connection.
 */

int ftpd_session(FTPD_SESSION handle, int timeout);

/**
 * @brief
 *   Close and destroy the handle created by ftpd_open.
 *
 * @param  handle A handle previously returned by ftpd_open
 *
 * @return
 *   None
 */

void ftpd_close(FTPD_SESSION handle);

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif /* __APPS_INCLUDE_NETUTILS_FTPD_H */
