IMPLEMENTATION:

#include <cassert>
#include <cstdio>
#include <stdlib.h>

// ------------------------------------------------------------------------
IMPLEMENTATION [debug]:

#include "kernel_console.h"
#include "thread.h"

extern "C"
void
assert_fail(char const *expr_msg, char const *file, unsigned int line,
            void *caller)
{
  // Make sure that GZIP mode is off.
  //
  // We need to use the console_unchecked() method here to avoid potential
  // infinite recursion (calling assert() in the regular console() method).
  Kconsole::console_unchecked()->end_exclusive(Console::GZIP);

  printf("\nAssertion failed at %s:%u:%p: %s\n", file, line, caller, expr_msg);

  Thread::system_abort();
}

// ------------------------------------------------------------------------
IMPLEMENTATION [!debug]:

#include "terminate.h"

extern "C"
void
assert_fail(char const *expr_msg, char const *file, unsigned int line,
            void *caller)
{
  printf("\nAssertion failed at %s:%u:%p: %s\n", file, line, caller, expr_msg);

  terminate(EXIT_FAILURE);
}
