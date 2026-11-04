/* SPDX-License-Identifier: Apache-2.0 */
/* Link-only root; never installed as a command or called on a board. */
#include "third_party/onnxruntime/include/onnxruntime_c_api.h"
extern "C" const void *aipet_ort_link_probe() { return OrtGetApiBase(); }
