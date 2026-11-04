/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AIPET_VOICE_NPU_CHECK_HXX
#define AIPET_VOICE_NPU_CHECK_HXX
namespace aipet {
int voice_npu_check(int argc,char **argv);
int voice_npu_kws(int argc,char **argv);
/* Explicit bounded diagnostic. Unknown outputs get finite/CRC/guard checks,
 * NOT golden recognition acceptance; never dispatches actions or uploads. */
int voice_npu_kws_input(int argc,char **argv);
int voice_npu_kws_memory(const char *base,const float *features,unsigned frames);
}
#endif
