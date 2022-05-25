INTERFACE:

#include "types.h"

class Platform_control
{
public:
  static void init(Cpu_number);
  static bool cpu_shutdown_available();
  static int cpu_allow_shutdown(Cpu_number cpu, bool allow);
  static int system_suspend(Mword extra);
  static void system_off();
  static void system_reboot();

  /**
   * Get node-id of current CPU.
   *
   * *Must* be an inline function that is completely inlineable. It's used
   * by Platform_control::amp_boot_info_idx() which is called without a
   * stack!
   */
  static inline unsigned node_id();
};

// ------------------------------------------------------------------------
IMPLEMENTATION:

#include "l4_types.h"
#include "reset.h"

IMPLEMENT_DEFAULT inline
void
Platform_control::init(Cpu_number)
{}

IMPLEMENT_DEFAULT inline NEEDS["l4_types.h"]
int
Platform_control::system_suspend(Mword)
{ return -L4_err::EBusy; }

IMPLEMENT_DEFAULT inline
bool
Platform_control::cpu_shutdown_available()
{ return false; }

IMPLEMENT_DEFAULT inline NEEDS["l4_types.h"]
int
Platform_control::cpu_allow_shutdown(Cpu_number, bool)
{ return -L4_err::ENodev; }

IMPLEMENT_DEFAULT inline
void
Platform_control::system_off()
{}

IMPLEMENT_DEFAULT inline NEEDS["reset.h"]
void
Platform_control::system_reboot()
{
  platform_reset();
}

IMPLEMENT_DEFAULT inline
unsigned
Platform_control::node_id()
{ return 0; }
