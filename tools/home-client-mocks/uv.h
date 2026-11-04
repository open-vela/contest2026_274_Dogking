/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HOME_UV_MOCK_H
#define HOME_UV_MOCK_H
typedef struct { int live; } uv_loop_t;
#define UV_RUN_NOWAIT 2
int uv_loop_init(uv_loop_t *);
int uv_loop_close(uv_loop_t *);
int uv_run(uv_loop_t *,int);
#endif
