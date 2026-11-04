/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>
#include <type_traits>
namespace knf {
enum Level { TRACE,DEBUG,INFO,WARNING,ERROR,FATAL };
/* Fixed-profile port: diagnostics require no locale, streams or exceptions.
 * Fatal invariants abort, not a supported recoverable user input path. */
class RtosLogger {
 public:
  RtosLogger(const char *file,unsigned line,Level level):level_(level) {
    std::fprintf(stderr,"knf %s:%u: ",file,line);
  }
  ~RtosLogger() noexcept {
    std::fputc('\n',stderr);
    if (level_==FATAL) std::abort();
  }
  const RtosLogger &operator<<(const char *v) const {
    std::fputs(v?v:"(null)",stderr); return *this;
  }
  const RtosLogger &operator<<(const std::string &v) const { return *this<<v.c_str(); }
  template<class T> const RtosLogger &operator<<(const T &v) const {
    if constexpr(std::is_integral<T>::value) std::fprintf(stderr,"%lld",static_cast<long long>(v));
    else if constexpr(std::is_floating_point<T>::value) std::fprintf(stderr,"%.8g",static_cast<double>(v));
    else if constexpr(std::is_convertible<const T &,const char *>::value) std::fputs(v,stderr);
    else { (void)v; std::fputs("[value]",stderr); }
    return *this;
  }
 private:
  Level level_;
};
struct RtosVoidifier { void operator&(const RtosLogger &) const {} };
}
#define KNF_STATIC_ASSERT(x) static_assert(x, "")
#define KNF_LOG(x) ::knf::RtosLogger(__FILE__,__LINE__,::knf::x)
#define KNF_CHECK(x) (x) ? (void)0 : ::knf::RtosVoidifier() & KNF_LOG(FATAL) << "Check failed: " #x " "
#define KNF_CHECK_EQ(x,y) KNF_CHECK((x)==(y))
#define KNF_CHECK_NE(x,y) KNF_CHECK((x)!=(y))
#define KNF_CHECK_LT(x,y) KNF_CHECK((x)<(y))
#define KNF_CHECK_LE(x,y) KNF_CHECK((x)<=(y))
#define KNF_CHECK_GT(x,y) KNF_CHECK((x)>(y))
#define KNF_CHECK_GE(x,y) KNF_CHECK((x)>=(y))
#define KNF_DCHECK(x) KNF_CHECK(x)
#define KNF_DCHECK_EQ(x,y) KNF_CHECK_EQ(x,y)
#define KNF_DCHECK_NE(x,y) KNF_CHECK_NE(x,y)
#define KNF_DCHECK_LT(x,y) KNF_CHECK_LT(x,y)
#define KNF_DCHECK_LE(x,y) KNF_CHECK_LE(x,y)
#define KNF_DCHECK_GT(x,y) KNF_CHECK_GT(x,y)
#define KNF_DCHECK_GE(x,y) KNF_CHECK_GE(x,y)
#define KNF_DLOG(x) KNF_LOG(x)
