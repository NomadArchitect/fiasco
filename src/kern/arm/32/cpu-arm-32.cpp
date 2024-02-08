INTERFACE [arm]:

#include "global_data.h"

EXTENSION class Cpu
{
public:
  enum {
    Cp15_c1_cache_enabled  = Cp15_c1_generic | Cp15_c1_cache_bits,
    Cp15_c1_cache_disabled = Cp15_c1_generic,
  };

  static Global_data<Unsigned32> sctlr;
  bool has_generic_timer() const { return (_cpu_id._pfr[1] & 0xf0000) == 0x10000; }
};

//-------------------------------------------------------------------------
IMPLEMENTATION [arm]:

DEFINE_GLOBAL Global_data<Unsigned32> Cpu::sctlr;

PUBLIC static inline
Mword
Cpu::midr()
{
  Mword m;
  asm volatile ("mrc p15, 0, %0, c0, c0, 0" : "=r" (m));
  return m;
}

PUBLIC static inline
Mword
Cpu::mpidr()
{
  Mword mpid;
  asm volatile ("mrc p15, 0, %0, c0, c0, 5" : "=r"(mpid));
  return mpid;
}

IMPLEMENTATION [arm && arm_v8plus && mmu]: //------------------------------

PUBLIC static inline
Mword
Cpu::dfr1()
{ Mword r; asm volatile ("mrc p15, 0, %0, c0, c3, 5": "=r" (r)); return r; }

IMPLEMENT_OVERRIDE inline
bool
Cpu::has_hpmn0() const
{ return ((dfr1() >> 4) & 0xf) == 1; }

//-------------------------------------------------------------------------
IMPLEMENTATION [arm]:

PRIVATE static inline
void
Cpu::check_for_swp_enable()
{
  if (!Config::Cp15_c1_use_swp_enable)
    return;

  if (((midr() >> 16) & 0xf) != 0xf)
    return; // pre ARMv7 has no swap enable / disable

  Mword id_isar0;
  asm volatile ("mrc p15, 0, %0, c0, c2, 0" : "=r"(id_isar0));
  if ((id_isar0 & 0xf) != 1)
    return; // CPU has no swp / swpb

  if (((mpidr() >> 31) & 1) == 0)
    return; // CPU has no MP extensions -> no swp enable

  sctlr |= Cp15_c1_v7_sw;
}

IMPLEMENT
void Cpu::early_init()
{
  sctlr = Config::Cache_enabled
          ? Cp15_c1_cache_enabled : Cp15_c1_cache_disabled;

  check_for_swp_enable();

  // switch to supervisor mode and initialize the memory system
  asm volatile ( " mov  r2, r13             \n"
                 " mov  r3, r14             \n"
                 " msr  cpsr_c, %1          \n"
                 " mov  r13, r2             \n"
                 " mov  r14, r3             \n"

                 " mcr  p15, 0, %0, c1, c0  \n"
                 :
                 : "r" (sctlr.unwrap()),
                   "r" (Proc::Status_mode_supervisor
                        | Proc::Status_interrupts_disabled)
                 : "r2", "r3");

  early_init_platform();

  Mem_unit::flush_cache();
}

PUBLIC static inline
void
Cpu::enable_dcache()
{
  asm volatile("mrc     p15, 0, %0, c1, c0, 0 \n"
               "orr     %0, %1                \n"
               "mcr     p15, 0, %0, c1, c0, 0 \n"
               : : "r" (0), "i" (Cp15_c1_cache));
}

PUBLIC static inline
void
Cpu::disable_dcache()
{
  asm volatile("mrc     p15, 0, %0, c1, c0, 0 \n"
               "bic     %0, %1                \n"
               "mcr     p15, 0, %0, c1, c0, 0 \n"
               : : "r" (0), "i" (Cp15_c1_cache));
}

//---------------------------------------------------------------------------
IMPLEMENTATION [arm && !cpu_virt && mmu]:

#include "kmem.h"
#include "kmem_space.h"

IMPLEMENT_OVERRIDE
void
Cpu::init_supervisor_mode(bool is_boot_cpu)
{
  if (!is_boot_cpu)
    return;

  extern char ivt_start; // physical address!

  // map the interrupt vector table to 0xffff0000
  auto pte = Kmem::kdir->walk(Virt_addr(Kmem_space::Ivt_base),
                              Kpdir::Depth, true,
                              Kmem_alloc::q_allocator(Ram_quota::root.unwrap()));

  Address va = reinterpret_cast<Address>(&ivt_start)
                 - Mem_layout::Sdram_phys_base + Mem_layout::Map_base;
  pte.set_page(Phys_mem_addr(Kmem::kdir->virt_to_phys(va)),
               Page::Attr::kern_global(Page::Rights::RWX()));
  pte.write_back_if(true);
  Mem_unit::tlb_flush_kernel(Kmem_space::Ivt_base);
}

//---------------------------------------------------------------------------
IMPLEMENTATION [arm && !cpu_virt && mpu]:

PUBLIC static
void
Cpu::init_sctlr()
{
  unsigned control = Config::Cache_enabled
                     ? Cp15_c1_cache_enabled : Cp15_c1_cache_disabled;

  Mem::dsb();
  asm volatile("mcr p15, 0, %[control], c1, c0, 0" // SCTLR
      : : [control] "r" (control));
  Mem::isb();
}

IMPLEMENT_OVERRIDE
void
Cpu::init_supervisor_mode(bool)
{
  // set VBAR system register to exception vector address
  extern char exception_vector;
  asm volatile("mcr p15, 0, %0, c12, c0, 0 \n\t"  // VBAR
               :  : "r" (&exception_vector));

  // make sure vectors are executed in A32 state
  unsigned long r;
  asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r" (r) : : "memory");  // SCTLR
  r &= ~(1UL << 30);
  asm volatile("mcr p15, 0, %0, c1, c0, 0" : : "r" (r) : "memory");   // SCTLR
}

//---------------------------------------------------------------------------
IMPLEMENTATION [arm && cpu_virt && mpu]:

PUBLIC static
void
Cpu::init_sctlr()
{
  Mem::dsb();
  asm volatile("mcr p15, 4, %[control], c1, c0, 0" // HSCTLR
      : : [control] "r" (Hsctlr));
  Mem::isb();
}

//---------------------------------------------------------------------------
IMPLEMENTATION [arm && arm_v6plus]:

PRIVATE static inline
void
Cpu::modify_actrl(Mword set_mask, Mword clear_mask)
{
  Mword t;
  asm volatile("mrc p15, 0, %[reg], c1, c0, 1 \n\t"
               "bic %[reg], %[reg], %[clr]    \n\t"
               "orr %[reg], %[reg], %[set]    \n\t"
               "mcr p15, 0, %[reg], c1, c0, 1 \n\t"
               : [reg] "=r" (t)
               : [set] "r" (set_mask), [clr] "r" (clear_mask));
}

PRIVATE static inline NEEDS[Cpu::modify_actrl]
void
Cpu::set_actrl(Mword bit_mask)
{ modify_actrl(bit_mask, 0); }

PRIVATE static inline NEEDS[Cpu::modify_actrl]
void
Cpu::clear_actrl(Mword bit_mask)
{ modify_actrl(0, bit_mask); }

IMPLEMENT
void
Cpu::id_init()
{
  __asm__("mrc p15, 0, %0, c0, c1, 0": "=r" (_cpu_id._pfr[0]));
  __asm__("mrc p15, 0, %0, c0, c1, 1": "=r" (_cpu_id._pfr[1]));
  __asm__("mrc p15, 0, %0, c0, c1, 2": "=r" (_cpu_id._dfr0));
  __asm__("mrc p15, 0, %0, c0, c1, 3": "=r" (_cpu_id._afr0));
  __asm__("mrc p15, 0, %0, c0, c1, 4": "=r" (_cpu_id._mmfr[0]));
  __asm__("mrc p15, 0, %0, c0, c1, 5": "=r" (_cpu_id._mmfr[1]));
  __asm__("mrc p15, 0, %0, c0, c1, 6": "=r" (_cpu_id._mmfr[2]));
  __asm__("mrc p15, 0, %0, c0, c1, 7": "=r" (_cpu_id._mmfr[3]));
}

//---------------------------------------------------------------------------
IMPLEMENTATION [arm && arm_v7]:

PUBLIC static inline
Unsigned32
Cpu::hcr()
{
  Unsigned32 r;
  asm volatile ("mrc p15, 4, %0, c1, c1, 0" : "=r"(r));
  return r;
}

PUBLIC static inline
void
Cpu::hcr(Unsigned32 hcr)
{
  asm volatile ("mcr p15, 4, %0, c1, c1, 0" : : "r"(hcr));
}

//---------------------------------------------------------------------------
IMPLEMENTATION [arm && arm_v8]:

PUBLIC static inline
Unsigned64
Cpu::hcr()
{
  Unsigned32 l, h;
  asm volatile ("mrc p15, 4, %0, c1, c1, 0" : "=r"(l));
  asm volatile ("mrc p15, 4, %0, c1, c1, 4" : "=r"(h));
  return Unsigned64{h} << 32 | l;
}

PUBLIC static inline
void
Cpu::hcr(Unsigned64 hcr)
{
  asm volatile ("mcr p15, 4, %0, c1, c1, 0" : : "r"(hcr & 0xffffffff));
  asm volatile ("mcr p15, 4, %0, c1, c1, 4" : : "r"(hcr >> 32));
}

//---------------------------------------------------------------------------
IMPLEMENTATION [arm && !(mmu && arm_lpae)]:

PUBLIC static inline unsigned Cpu::phys_bits() { return 32; }

//---------------------------------------------------------------------------
IMPLEMENTATION [arm && mmu && arm_lpae]:

PUBLIC static inline unsigned Cpu::phys_bits() { return 40; }
