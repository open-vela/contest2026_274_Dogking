/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_CPU_QUALIFY_H
#define A733_CPU_QUALIFY_H
#include <stdint.h>
#include <errno.h>
#include <string.h>
/* Testable migration protocol, not a replacement scheduler. A successful
 * affinity syscall is NOT evidence that this instruction runs on that CPU.
 * All callbacks return negative errno. No locks may be held across wait(). */
struct a733_cpu_ops
{
  int (*get)(void *, uint32_t *);
  int (*set)(void *, uint32_t);
  int (*identity)(void *, uint64_t *, uint64_t *);
  uint64_t (*time)(void *);
  void (*wait)(void *);
  int (*stop)(void *);
};
struct a733_cpu_proof
{
  uint32_t before, requested, readback, restored;
  uint64_t midr, mpidr, elapsed;
  unsigned attempts, confirmations;
  int error, restore_error;
};
static inline int a733_cpu_qualify(const struct a733_cpu_ops *op, void *ctx,
  unsigned target, unsigned part, struct a733_cpu_proof *p)
{
  memset(p, 0, sizeof(*p));
  if (target >= 8) return p->error=-EINVAL;
  p->requested=UINT32_C(1)<<target;
  uint64_t start=op->time(ctx);
  int r=op->get(ctx, &p->before);
  if (r) return p->error=r;
  r=op->set(ctx, p->requested);
  if (r) return p->error=r;
  /* The iteration cap also bounds a stalled/unqualified monotonic timer.
   * Two matching snapshots separated by a real blocking yield are required.
   * Snapshot's implementation brackets MIDR by two MPIDR reads. */
  for (unsigned n=0; n<251; n++)
    {
      p->attempts++;
      if (op->stop(ctx)) {r=-ECANCELED;break;}
      r=op->get(ctx, &p->readback);
      if (r) break;
      if (p->readback!=p->requested) {r=-EIO;break;}
      r=op->identity(ctx, &p->midr, &p->mpidr);
      if (r && r!=-EAGAIN) break;
      if (!r && ((p->mpidr>>8)&255)==target &&
          ((p->midr>>4)&4095)==part)
        {
          if (++p->confirmations==2) {p->elapsed=op->time(ctx)-start;return 0;}
        }
      else p->confirmations=0;
      if (op->time(ctx)-start>=250000) {r=-ETIMEDOUT;break;}
      op->wait(ctx);
      r=-ETIMEDOUT;
    }
  p->error=r ? r : -ETIMEDOUT;
  p->elapsed=op->time(ctx)-start;
  /* A fallback is only safe when restoring the exact original mask succeeds.
   * Never hide a failed restore, nor enable a CPU-specific fast path here. */
  p->restore_error=op->set(ctx,p->before);
  if (!p->restore_error)
    {
      p->restore_error=op->get(ctx,&p->restored);
      if (!p->restore_error && p->restored!=p->before) p->restore_error=-EIO;
    }
  return p->restore_error ? p->restore_error : p->error;
}
#endif
