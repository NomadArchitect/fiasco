//------------------------------------------------------------------------
INTERFACE [arm_v8 && fpu]:

EXTENSION class Fpu
{
public:
  struct Exception_state_user
  {
  };

  struct Fpu_regs
  {
    Unsigned32 fpcr, fpsr;
    Unsigned64 state[64]; // 32 128bit regs
  };
};

class Fpu_state : public Fpu::Fpu_regs {};

// ------------------------------------------------------------------------
INTERFACE [arm && !fpu]:

EXTENSION class Fpu
{
public:
  struct Exception_state_user
  {
  };
};

// ------------------------------------------------------------------------
IMPLEMENTATION [arm && !fpu]:

#include "trap_state.h"

PUBLIC static inline NEEDS["trap_state.h"]
void
Fpu::save_user_exception_state(bool, Fpu_state *, Trap_state *,
                               Exception_state_user *)
{}

// ------------------------------------------------------------------------
IMPLEMENTATION [arm && fpu]:

#include <cassert>
#include <cstdio>
#include <cstring>

#include "cpu.h"
#include "mem.h"
#include "processor.h"
#include "static_assert.h"
#include "trap_state.h"

IMPLEMENT
void
Fpu::init(Cpu_number cpu, bool resume)
{
  if (Config::Jdb && !resume && cpu == Cpu_number::boot_cpu())
    printf("FPU: Initialize\n");

  Fpu &f = fpu.cpu(cpu);
  if (!resume)
    show(cpu);

  // make sure that in HYP case CPACR is loaded and enabled.
  // without HYP the disable below will disable it, so this does not hurt
  __asm__ __volatile__ (
      "msr  CPACR_EL1, %[cpacr_on]"
      : : [cpacr_on]"r"(Cpu::Cpacr_el1_generic_hyp));

  f.finish_init();
}

PRIVATE static inline
void
Fpu::save_fpu_regs(Fpu_regs *r)
{
  Mword fpcr;
  Mword fpsr;
  asm volatile("stp     q0, q1,   [%[s], #16 *  0]        \n"
               "stp     q2, q3,   [%[s], #16 *  2]        \n"
               "stp     q4, q5,   [%[s], #16 *  4]        \n"
               "stp     q6, q7,   [%[s], #16 *  6]        \n"
               "stp     q8, q9,   [%[s], #16 *  8]        \n"
               "stp     q10, q11, [%[s], #16 * 10]        \n"
               "stp     q12, q13, [%[s], #16 * 12]        \n"
               "stp     q14, q15, [%[s], #16 * 14]        \n"
               "stp     q16, q17, [%[s], #16 * 16]        \n"
               "stp     q18, q19, [%[s], #16 * 18]        \n"
               "stp     q20, q21, [%[s], #16 * 20]        \n"
               "stp     q22, q23, [%[s], #16 * 22]        \n"
               "stp     q24, q25, [%[s], #16 * 24]        \n"
               "stp     q26, q27, [%[s], #16 * 26]        \n"
               "stp     q28, q29, [%[s], #16 * 28]        \n"
               "stp     q30, q31, [%[s], #16 * 30]        \n"
               "mrs     %[fpcr], fpcr                     \n"
               "mrs     %[fpsr], fpsr                     \n"
               : [fpcr] "=r" (fpcr),
                 [fpsr] "=r" (fpsr),
                 "=m" (r->state)
               : [s] "r" (r->state));
  r->fpcr = fpcr;
  r->fpsr = fpsr;
}

PRIVATE static inline
void
Fpu::restore_fpu_regs(Fpu_regs const *r)
{
  asm volatile("ldp     q0, q1,   [%[s], #16 *  0]        \n"
               "ldp     q2, q3,   [%[s], #16 *  2]        \n"
               "ldp     q4, q5,   [%[s], #16 *  4]        \n"
               "ldp     q6, q7,   [%[s], #16 *  6]        \n"
               "ldp     q8, q9,   [%[s], #16 *  8]        \n"
               "ldp     q10, q11, [%[s], #16 * 10]        \n"
               "ldp     q12, q13, [%[s], #16 * 12]        \n"
               "ldp     q14, q15, [%[s], #16 * 14]        \n"
               "ldp     q16, q17, [%[s], #16 * 16]        \n"
               "ldp     q18, q19, [%[s], #16 * 18]        \n"
               "ldp     q20, q21, [%[s], #16 * 20]        \n"
               "ldp     q22, q23, [%[s], #16 * 22]        \n"
               "ldp     q24, q25, [%[s], #16 * 24]        \n"
               "ldp     q26, q27, [%[s], #16 * 26]        \n"
               "ldp     q28, q29, [%[s], #16 * 28]        \n"
               "ldp     q30, q31, [%[s], #16 * 30]        \n"
               "msr     fpcr, %[fpcr]                     \n"
               "msr     fpsr, %[fpsr]                     \n"
               : : [fpcr] "r" ((Mword)r->fpcr),
                   [fpsr] "r" ((Mword)r->fpsr),
                   [s] "r" (r->state),
                   "m" (r->state));
}

IMPLEMENT
void
Fpu::save_state(Fpu_state *fpu_regs)
{
  assert(fpu_regs);
  save_fpu_regs(fpu_regs);
}

IMPLEMENT_DEFAULT
void
Fpu::restore_state(Fpu_state const *fpu_regs)
{
  assert(fpu_regs);
  restore_fpu_regs(fpu_regs);
}

IMPLEMENT inline
unsigned
Fpu::state_size()
{ return sizeof (Fpu_regs); }

IMPLEMENT inline
unsigned
Fpu::state_align()
{ return 16; }

PUBLIC static inline NEEDS["trap_state.h", <cassert>]
void
Fpu::save_user_exception_state(bool, Fpu_state *, Trap_state *, Exception_state_user *)
{
}

// ------------------------------------------------------------------------
IMPLEMENTATION [arm && fpu && lazy_fpu]:

PRIVATE inline
void
Fpu::finish_init()
{
  disable();
  set_owner(0);
}

//-------------------------------------------------------------------------
IMPLEMENTATION [arm && fpu && !lazy_fpu]:

PRIVATE inline
void
Fpu::finish_init()
{}

// ------------------------------------------------------------------------
IMPLEMENTATION [arm && fpu]:

PRIVATE static
void
Fpu::show(Cpu_number)
{}

//-------------------------------------------------------------------------
IMPLEMENTATION [arm && fpu && !cpu_virt]:

IMPLEMENT inline NEEDS ["mem.h", "static_assert.h", <cstring>]
void
Fpu::init_state(Fpu_state *fpu_regs)
{
  static_assert(!(sizeof (*fpu_regs) % sizeof(Mword)),
                "Non-mword size of Fpu_regs");
  Mem::memset_mwords(fpu_regs, 0, sizeof (*fpu_regs) / sizeof(Mword));
}

PUBLIC static inline NEEDS ["cpu.h"]
bool
Fpu::is_enabled()
{
  Mword x;
  asm volatile ("mrs %0, CPACR_EL1" : "=r"(x));
  return x & Cpu::Cpacr_el1_fpen_full;
}


PUBLIC static inline NEEDS ["cpu.h"]
void
Fpu::enable()
{
  Mword t;
  asm volatile("mrs  %0, CPACR_EL1  \n"
               "orr  %0, %0, %1     \n"
               "msr  CPACR_EL1, %0  \n"
               : "=r"(t) : "I" (Cpu::Cpacr_el1_fpen_full));
  Mem::isb();
}

PUBLIC static inline NEEDS ["cpu.h"]
void
Fpu::disable()
{
  Mword t;
  asm volatile("mrs  %0, CPACR_EL1  \n"
               "bic  %0, %0, %1     \n"
               "msr  CPACR_EL1, %0  \n"
               : "=r"(t) : "I" (Cpu::Cpacr_el1_fpen_full));
  Mem::isb();
}

//-------------------------------------------------------------------------
IMPLEMENTATION [arm && fpu && cpu_virt]:

IMPLEMENT inline NEEDS ["mem.h", "static_assert.h", <cstring>]
void
Fpu::init_state(Fpu_state *fpu_regs)
{
  static_assert(!(sizeof (*fpu_regs) % sizeof(Mword)),
                "Non-mword size of Fpu_regs");
  Mem::memset_mwords(fpu_regs, 0, sizeof (*fpu_regs) / sizeof(Mword));
  //fpu_regs->fpexc |= FPEXC_EN;
}

PUBLIC static inline NEEDS ["cpu.h"]
bool
Fpu::is_enabled()
{
  Mword dummy;
  __asm__ __volatile__ ("mrs %0, CPTR_EL2" : "=r"(dummy));
  return !(dummy & Cpu::Cptr_el2_tfp);
}

PUBLIC inline NEEDS ["cpu.h"]
void
Fpu::enable()
{
  Mword dummy;
  __asm__ __volatile__ (
      "mrs %0, CPTR_EL2  \n"
      "bic %0, %0, %1    \n"
      "msr CPTR_EL2, %0  \n"
      : "=&r" (dummy) : "I" (Cpu::Cptr_el2_tfp));
  Mem::isb();
}

PUBLIC inline NEEDS ["cpu.h"]
void
Fpu::disable()
{
  Mword dummy;
  __asm__ __volatile__ (
      "mrs  %0, CPTR_EL2  \n"
      "orr  %0, %0, %1    \n"
      "msr  CPTR_EL2, %0  \n"
      : "=&r" (dummy) : "I" (Cpu::Cptr_el2_tfp));
  Mem::isb();
}
