/* SPDX-License-Identifier: Apache-2.0 */
/* Correct exception count for the explicitly exception-disabled NuttX ABI.
 * Never compile this adapter with exception support; it is not a fake unwind
 * runtime. No exception can be active in this profile. */
#include <nuttx/config.h>
#include <exception>
#if defined(CONFIG_CXX_EXCEPTION) || defined(__cpp_exceptions)
#error "No-exception count adapter requires the qualified no-exception ABI"
#endif
namespace std {
int uncaught_exceptions() noexcept { return 0; }
}
