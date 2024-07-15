INTERFACE:

#include "mem.h"
#include "cpu_mask.h"
#include "queue.h"
#include "queue_item.h"
#include "per_cpu_data.h"
#include "processor.h"
#include <cxx/function>

class Cpu_call_queue;

class Cpu_call : private Queue_item
{
  friend class Cpu_call_queue;
  friend class Jdb_cpu_call_module;

private:
  cxx::functor<bool (Cpu_number cpu)> _func;
  Mword _wait;

public:
  template< typename Functor >
  Cpu_call(Functor &&f)
  : _func(f), _wait(false) {}

  Cpu_call(cxx::functor<bool (Cpu_number)> &&f)
  : _func(f), _wait(false) {}

  Cpu_call() : _func(), _wait(false) {}

  void set(cxx::functor<bool (Cpu_number)> &f)
  { _func = f; }

  void set_queued()
  { _wait = true; }

  void done()
  {
    Mem::mp_mb();
    write_now(&_wait, Mword{false});
  }

  bool run(Cpu_number cpu, bool done = true)
  {
    bool res = _func(cpu);
    if (done)
      this->done();
    return res;
  }

  /**
   * Test if this Cpu_call has finished execution.
   *
   * \param async  On `false`, synchronous execution was requested. On `true`,
   *               asynchronous execution was requested.
   * \retval false Execution did not finish yet, Cpu_call may *not* be re-used.
   * \retval true  Execution finished, this Cpu_call may be re-used.
   */
  bool is_done(bool async) const
  {
    if (!async)
      return !access_once(&_wait);

    Mem::mp_mb();
    return !queued();
  }

  bool remote_call(Cpu_number cpu, bool async);
};

template<unsigned MAX>
class Cpu_calls
{
  Cpu_calls(Cpu_calls const &) = delete;
  Cpu_calls &operator = (Cpu_calls const &) = delete;

public:
  enum { Max = Config::Max_num_cpus < MAX ?  Config::Max_num_cpus : MAX };
  Cpu_calls() : _used(0) {}

  Cpu_call *next()
  {
    if (_used < Max)
      return &_cs[_used++];
    return 0;
  }

  Cpu_call *find_done(bool async)
  {
    for (unsigned i = 0; i < _used; ++i)
      if (_cs[i].is_done(async))
        return &_cs[i];

    return 0;
  }

  void wait_all(bool async)
  {
    for (unsigned i = 0; i < _used; ++i)
      while (!_cs[i].is_done(async))
        Proc::pause();
  }

private:
  Cpu_call _cs[Max];
  unsigned char _used;
};

class Cpu_call_queue : public Queue
{
public:
  void enq(Cpu_call *rq);
  bool dequeue(Cpu_call *drq);
  bool handle_requests();
  bool execute_request(Cpu_call *r);
};


IMPLEMENTATION:

#include "assert.h"
#include "globals.h"
#include "lock_guard.h"
#include "mem.h"

IMPLEMENT inline NEEDS["lock_guard.h", "assert.h"]
void
Cpu_call_queue::enq(Cpu_call *rq)
{
  assert(cpu_lock.test());
  auto guard = lock_guard(q_lock());
  enqueue(rq);
}

IMPLEMENT inline
bool
Cpu_call_queue::execute_request(Cpu_call *r)
{
  return r->run(current_cpu(), true);
}

IMPLEMENT inline NEEDS["lock_guard.h"]
bool
Cpu_call_queue::dequeue(Cpu_call *r)
{
  auto guard = lock_guard(q_lock());
  if (!r->queued())
    return false;
  return Queue::dequeue(r);
}

IMPLEMENT inline NEEDS["mem.h", "lock_guard.h", "globals.h"]
bool
Cpu_call_queue::handle_requests()
{
  bool need_resched = false;
  while (1)
    {
      Queue_item *qi;
        {
          auto guard = lock_guard(q_lock());
          qi = first();
          if (!qi)
            return need_resched;

          check (Queue::dequeue(qi));
        }

      Cpu_call *r = static_cast<Cpu_call*>(qi);
      need_resched |= execute_request(r);
    }
}

/**
 * Execute code on a number of CPUs synchronously.
 *
 * \pre CPU lock must not be held (to prevent deadlocks).
 *
 * The `func` is executed synchronously on the set of target CPUs, i.e.
 * cpu_call_many waits until `func` has finished execution on all CPUs.
 *
 * \param cpus  The set of CPUs to execute `func` on.
 * \param func  The code to execute on the target CPUs. The return value
 *              indicates whether a reschedule is necessary on the target CPU.
 *
 * \note The `func` might be executed on several CPUs of the target CPU in
 *       parallel.
 *
 * \note If the CPU set is empty, this function returns immediately.
 */
PUBLIC static inline inline NEEDS[Cpu_call::_cpu_call_many]
void
Cpu_call::cpu_call_many(Cpu_mask const &cpus,
                        cxx::functor<bool (Cpu_number)> &&func)
{ _cpu_call_many(cpus, cxx::move(func), false); }

/**
 * Execute code on a number of CPUs asynchronously.
 *
 * \pre CPU lock must not be held (to prevent deadlocks).
 *
 * The `func` is executed asynchronously on the set of target CPUs, i.e.
 * cpu_call_many_async does not wait until `func` has finished execution on all
 * CPUs.
 *
 * This means it is allowed to pass a `func` that does not return or returns
 * late. But the `func` must not be a capturing lambda with state associated, it
 * has to be a regular function or non-capturing lambda.
 *
 * \param cpus  The set of CPUs to execute `func` on.
 * \param func  The code to execute on the target CPUs. The return value
 *              indicates whether a reschedule is necessary on the target CPU.
 *
 * \note If the CPU set is empty, this function returns immediately.
 */
PUBLIC static inline NEEDS[Cpu_call::_cpu_call_many]
void
Cpu_call::cpu_call_many_async(Cpu_mask const &cpus,
                              bool (*func)(Cpu_number))
{ _cpu_call_many(cpus, func, true); }

// ----------------------------------------------------------------------
IMPLEMENTATION [!mp]:

PUBLIC static inline
bool
Cpu_call::_cpu_call_many(Cpu_mask const &m,
                         cxx::functor<bool (Cpu_number)> &&func,
                         bool)
{
  auto guard = lock_guard(cpu_lock);
  if (m.get(current_cpu()))
    func(current_cpu());
  return true;
}

PUBLIC static bool Cpu_call::handle_global_requests() { return false; }

// -----------------------------------------------------------------------
IMPLEMENTATION [mp]:

#include "cpu.h"
#include "ipi.h"
#include "processor.h"

EXTENSION class Cpu_call
{
  static Per_cpu<Cpu_call_queue> _glbl_q;
};

DEFINE_PER_CPU Per_cpu<Cpu_call_queue> Cpu_call::_glbl_q;

/**
 * Execute this Cpu_call on another CPU.
 *
 * \param cpu    CPU where to execute this Cpu_call.
 * \param async  On false, synchronous handling is requested. On true,
 *               asynchronous handling is requested.
 * \retval false Cpu_call handled, no need to wait.
 * \retval true  Cpu_call not yet executed, need to call Cpu_call::is_done()
 *               to detect when this call was finally executed.
 *
 * \note If this function returns false then this Cpu_call object can be re-used
 *       for another operation.
 * \note A request to execute the function on the current CPU will be executed
 *       directly synchronous and the Cpu_call can be re-used when the function
 *       returns.
 */
IMPLEMENT inline NEEDS["cpu.h", "ipi.h"]
bool
Cpu_call::remote_call(Cpu_number cpu, bool async)
{
  auto guard = lock_guard(cpu_lock);
  if (current_cpu() == cpu)
    {
      assert (is_done(async));
      run(cpu, false);
      return false;
    }

  Cpu_call_queue &q = _glbl_q.cpu(cpu);

  if (!async)
    set_queued();

  Mem::mp_mb();
  q.enq(this);

  Mem::mp_mb();
  if (EXPECT_FALSE(!Cpu::online(cpu)))
    {
      Mem::mp_mb();
      if (q.dequeue(this) && !async)
        done();
      assert (is_done(async));
      return false;
    }

  if (queued())
    {
      Ipi::send(Ipi::Global_request, current_cpu(), cpu);
      return true;
    }

  // async: may re-use the object as we are already done.
  // !async: it depends on `_wait` if we are already done or not.
  return !is_done(async);
}

PRIVATE static inline NEEDS["processor.h"]
void
Cpu_call::_cpu_call_many(Cpu_mask const &cpus,
                         cxx::functor<bool (Cpu_number)> &&func,
                         bool async)
{
  assert (!cpu_lock.test());

  if (cpus.empty())
    return;

  Cpu_calls<8> cs;
  Cpu_number n;
  Cpu_call *c = cs.next();
  for (n = Cpu_number::first(); n < Config::max_num_cpus() && c; ++n)
    {
      if (!cpus.get(n))
        continue;

      c->set(func);
      if (c->remote_call(n, async))
        c = cs.next();
    }

  for (; n < Config::max_num_cpus(); ++n)
    {
      if (!cpus.get(n))
        continue;

      while (!(c = cs.find_done(async)))
        Proc::pause();

      c->remote_call(n, async);
    }

  cs.wait_all(async);
  return;
}

PUBLIC
static bool
Cpu_call::handle_global_requests()
{
  return _glbl_q.current().handle_requests();
}


