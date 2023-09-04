INTERFACE [ia32 || amd64 || ux]:

EXTENSION class Mem_layout
{
public:
  enum { Io_port_max = (1UL << 16) };

  static Address _io_map_ptr;
};

//---------------------------------------------------------------------------
IMPLEMENTATION [ia32 || amd64 || ux]:

#include "paging_bits.h"

Address Mem_layout::_io_map_ptr = Mem_layout::Registers_map_end;

PUBLIC static inline NEEDS["paging_bits.h"]
Address
Mem_layout::alloc_io_vmem(unsigned long bytes)
{
  bytes = Pg::round(bytes);
  if (_io_map_ptr - bytes < Registers_map_start)
    return 0;

  _io_map_ptr -= bytes;
  return _io_map_ptr;
}

//---------------------------------------------------------------------------
IMPLEMENTATION [(ia32 || amd64 || ux) && virt_obj_space]:

PUBLIC static inline
template< typename V >
bool
Mem_layout::read_special_safe(V const *address, V &v)
{
  // Counterpart: Thread::pagein_tcb_request()
  static_assert(sizeof(v) <= sizeof(Mword), "wrong sized argument");
  Mword value;
  bool res;
  asm volatile ("clc; mov (%[adr]), %[val]; setnc %b[ex] \n"
      : [val] "=acd" (value), [ex] "=r" (res)
      : [adr] "acdbSD" (address)
      : "cc");
  v = V(value);
  return res;
}

PUBLIC static inline
template< typename T >
T
Mem_layout::read_special_safe(T const *a)
{
  // Counterpart: Thread::pagein_tcb_request()
  static_assert(sizeof(T) <= sizeof(Mword), "wrong sized return type");
  Mword res;
  asm volatile ("mov (%1), %0 \n\t"
      : "=acd" (res) : "acdbSD" (a) : "cc");
  return T(res);
}
