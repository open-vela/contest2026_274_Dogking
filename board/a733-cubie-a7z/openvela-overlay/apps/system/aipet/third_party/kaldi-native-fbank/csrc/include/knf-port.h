/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
/* NuttX libc++ deliberately excludes locale/iostream support. */
#if defined(__NuttX__) && !defined(KNF_NO_IOSTREAM)
#define KNF_NO_IOSTREAM 1
#endif
