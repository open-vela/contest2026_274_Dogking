/****************************************************************************
 * apps/system/aipetllm/aipetllm_cxx.cxx
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

extern "C" int aipetllm_cxx_checkpoint(void)
{
  std::vector<std::uint32_t> values{1, 2, 3, 5, 8, 13};
  std::string backend("ARM64 C++17/STL");
  std::uint32_t checksum = 0;

  for (const auto value : values)
    {
      checksum = checksum * 33 + value;
    }

  std::printf("cxx-check backend='%s' standard=%ld vector=%lu checksum=%08lx\n",
              backend.c_str(), static_cast<long>(__cplusplus),
              static_cast<unsigned long>(values.size()),
              static_cast<unsigned long>(checksum));

  return checksum == UINT32_C(0x027b1520) ? 0 : 1;
}
