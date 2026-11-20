

#ifdef _DEBUG
#define _STLP_DEBUG 1
#endif

// TnzCore includes
#include "tthreadmessage.h"
#include "tsystem.h"
#include "tatomicvar.h"

#include "tthread.h"

// STL includes
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>  // memset (it came with Qt's headers on Linux)
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

// tcg includes
#include "tcg/tcg_pool.h"


//==============================================================================

//==================================================
//    Paradigms of the Executor tasks management
//--------------------------------------------------

// Basics:
//  * Tasks added by Executors are always stored in a global multimap first -
//    ordering primarily being the schedulingPriority(), and insertion instant
//    (implicit) when they have the same scheduling priority.
//  * The multimap needs to be reverse-iterated to do that, since values with
//    the same key are ordered from the most recent to the oldest one: each is
//    inserted before the others of its key, as Qt's QMultiMap did before step
//    9c of doc/nexttoonz_core_noqt.md.
//  * Worker threads are stored in a global set.
//  * When a task is added or a task has been performed, the workers list is
//    refreshed, possibly adding new Workers for some executable tasks.
//  * When a worker ends a task, it automatically takes a new one before
//  refreshing
//    the workers list. If no task can be taken, by default the thread exits and
//    invokes its own destruction.
//  * The thread may instead be put to rest if explicitly told by the user with
//    the appropriate method.

// Default execution conditions:
//  * A task is executable if, by default, its task load added to the sum of
//  that of
//    all other active tasks does not exceed the available resources of the
//    system
//    (i.e.: 100 * # of cores of the machine).
//    In other words, in every instant of execution, the sum of all active
//    task's loads
//    never exceeds the available machine resources.
//  * When such default execution condition is not met when attempting to take
//  the
//    task, no other task is taken instead - we wait until enough resources have
//    been
//    freed before attempting to take the same task again.
//    In other words, the default execution condition is *BLOCKING*.

// Custom execution conditions:
//  * The user may decide to impose more tight conditions for tasks added by a
//  certain
//    Executor. Let's call such conditions 'custom' conditions.
//  * Custom conditions are always tested *AFTER* the default ones (on the same
//  task).
//    This is necessary to enforce the global scheduling priorities mechanism.
//  * When no task of a certain executor is active, custom conditions are always
//  considered
//    satisfied.
//  * If custom conditions are not met, we enqueue the task in an
//  Executor-private queue
//    for later execution and remove it from the global tasks queue - making it
//    possible
//    to perform other tasks before our custom-failed one (such operation will
//    be called
//    "ACCUMULATION").
//    In other words, custom conditions are *NOT BLOCKING* among different
//    Executors.
//  * Tasks in the last task's Executor-private queue are always polled before
//  those in
//    the global queue by Workers which ended a task, and this is done in a
//    *BLOCKING* way
//    inside the same Executor.
//  * When an Executor-private tasks queue is not empty, all the other tasks
//  added by the
//    same executor are scheduled in the queue.
//    In other words, the order of execution is always that of global insertion,
//    *inside
//    the same executor*.
//  * Tasks polled from an Executor-private queue (which therefore satisfied
//  custom conditions)
//    may still fail with default execution conditions. If that happens, we put
//    the task
//    back into the global queue with highest possible priority (timedOut) and
//    the worker dies.
//    Tasks with this special priority are polled *before every other task*. So,
//    again, default
//    conditions are *BLOCKING*.

// Thread-safety:
//  * Most of the following code is mutex-protected, although it may not seem
//    like it - indeed,
//    only *one* mutex is locked and unlocked all of the time. This 'transition
//    mutex' is
//    the key to thread-safety: we're considered to lie in a 'transition state'
//    if we are
//    operating outside the run() of some task - which covers almost the
//    totality of the code.
//  * The transition mutex is *not* recursive. The reason is that threads con
//  not wait on
//    QWaitConditions if the mutex is recursive. That makes it necessary (and
//    welcome) to put
//    mutex lockers in strategic points of the code - in many low-level
//    functions no mutex
//    is locked *because some caller already did it before*. If you're modifying
//    the code
//    always trace back the callers of a function before inserting misleading
//    mutex lockers.

//==============================================================================

//==================
//    TODO list
//------------------

//  * Improve dedicated threads support: make sure that tasks added to a
//  dedicated
//    executor are directly moved to the accumulation queue. The
//    setDedicatedThreads()
//    method must therefore react accordingly.

//  * It could be possible to implement a dependency-based mechanism...

//  * Should the hosting thread wait for worker ones upon ExecutorImp
//  destruction??
//    It could be a problem on some forever-waiting tasks...

//  * Ricontrolla che con le ultime modifiche gli ExecutorId rimangano quando si
//  passano
//    i puntatori in takeTask e refreshAss..

//==============================================================================

using namespace TThread;

DEFINE_CLASS_CODE(TThread::Runnable, 21)

//==============================================================================

//==========================================
//    Global init() initializer function
//------------------------------------------

#ifdef QT_CORE_LIB
// tthread_qt.cpp: the Qt dispatcher, and RunnableP for queued signals
void initThreadQt();
#endif

void TThread::init() {
  Executor::init();
#ifdef QT_CORE_LIB
  initThreadQt();
#endif
}

//---------------------------------------------------------------------

void TThread::shutdown() { Executor::shutdown(); }

//==============================================================================

namespace TThread {

//==============================================================================

//===========================
//    Worker Thread class
//---------------------------

//! A Worker is a thread that continuously polls Runnable tasks from a global
//! execution queue to make them work. Since step 9c of
//! doc/nexttoonz_core_noqt.md it is a detached std::thread, which takes
//! itself out of the workers list and deletes itself when it ends (it was a
//! QThread, deleted in the main thread through a queued signal).
class Worker final {
public:
  RunnableP m_task;

  TSmartPointerT<ExecutorId> m_master;

  bool m_exit;
  ExecutorId *m_sleepingIn;  // whose m_sleepings holds it, while it sleeps
  std::condition_variable m_waitCondition;  // waited on with the transition
                                            // mutex

  Worker();
  ~Worker();

  void start();
  void run();

  inline void takeTask();
  inline bool canAdopt(const RunnableP &task);
  inline void adoptTask(RunnableP &task);

  inline void rest();

  inline void updateCountsOnTake();
  inline void updateCountsOnRelease();

  inline void onFinish();
};

//=====================================================================

//========================
//    ExecutorId class
//------------------------

//! Contains all the executor data that need to persist until all tasks
//! added by the executor have been processed. Upon creation of an Executor
//! object, it instantiates one ExecutorId for both storing the Executor's data
//! sensible to the underlying code of the Executor manager, and to identify all
//! tasks added through it - by copying the smart pointer to the id into each
//! added task.
//! \sa Executor and Runnable class.
class ExecutorId final : public TSmartObject {
public:
  size_t m_id;

  int m_activeTasks;
  int m_maxActiveTasks;

  int m_activeLoad;
  int m_maxActiveLoad;

  bool m_dedicatedThreads;
  bool m_persistentThreads;
  std::deque<Worker *> m_sleepings;

  ExecutorId();
  ~ExecutorId();

  inline void accumulate(const RunnableP &task);

  void newWorker(RunnableP &task);
  void refreshDedicatedList();
};

//=====================================================================

//===========================
//    Executor::Imp class
//---------------------------

//! ExecutorImp both manages the allocation of worker threads for the
//! execution of Runnable tasks and centralizes the tasks collection.
//! One process only hosts one instance of the ExecutorImp class as a
//! a global variable that needs to be allocated in an application-lasting
//! and event-looped thread - typically the main thread in GUI applications.
class ExecutorImp {
public:
  std::multimap<int, RunnableP> m_tasks;
  std::set<Worker *> m_workers;  // Used just for debugging purposes

  tcg::indices_pool<> m_executorIdPool;
  std::vector<UCHAR> m_waitingFlagsPool;

  int m_activeLoad;
  int m_maxLoad;

  int m_liveWorkers;  // started and not yet deleted (under the mutex)

  std::mutex m_transitionMutex;  // Workers' transition mutex

  ExecutorImp();
  ~ExecutorImp();

  inline void insertTask(int schedulingPriority, RunnableP &task);
  inline bool removeTask(int schedulingPriority, const RunnableP &task);

  void refreshAssignments();

  inline bool isExecutable(RunnableP &task);
};

//=====================================================================

}  // namespace TThread

//=====================================================================

//===========================
//     Global variables
//---------------------------

namespace {
ExecutorImp *globalImp = 0;
bool shutdownVar       = false;

//! Gives the calling thread the scheduling QThread::setPriority() gives a
//! thread, called in that thread as the workers did (decision Q9-C of step 9
//! of doc/nexttoonz_core_noqt.md; Qt 5.15, qthread_unix.cpp and
//! qthread_win.cpp). On macOS and Linux the thread keeps its policy and its
//! priority is scaled from the enum onto the policy's range:
//!
//!   prio = (priority - lowest) * (max - min) / TimeCriticalPriority + min,
//!          clamped to [min, max]
//!
//! where lowest is LowestPriority if the system has SCHED_IDLE (Linux:
//! IdlePriority then is SCHED_IDLE with priority 0) and IdlePriority if not
//! (macOS). On macOS, SCHED_OTHER spans 15 to 47: Idle 15, Lowest 20, Low 25,
//! Normal 31, High 36, Highest 41, TimeCritical 47. On Windows the levels are
//! THREAD_PRIORITY_IDLE, _LOWEST, _BELOW_NORMAL, _NORMAL, _ABOVE_NORMAL,
//! _HIGHEST and _TIME_CRITICAL. InheritPriority changes nothing, as
//! setPriority() refuses it.
void setCurrentThreadPriority(Priority priority) {
  if (priority == InheritPriority) return;
#ifdef _WIN32
  int prio;
  switch (priority) {
  case IdlePriority:
    prio = THREAD_PRIORITY_IDLE;
    break;
  case LowestPriority:
    prio = THREAD_PRIORITY_LOWEST;
    break;
  case LowPriority:
    prio = THREAD_PRIORITY_BELOW_NORMAL;
    break;
  case NormalPriority:
    prio = THREAD_PRIORITY_NORMAL;
    break;
  case HighPriority:
    prio = THREAD_PRIORITY_ABOVE_NORMAL;
    break;
  case HighestPriority:
    prio = THREAD_PRIORITY_HIGHEST;
    break;
  case TimeCriticalPriority:
    prio = THREAD_PRIORITY_TIME_CRITICAL;
    break;
  default:
    return;
  }
  SetThreadPriority(GetCurrentThread(), prio);
#else
  pthread_t self = pthread_self();
  int policy;
  sched_param param;
  if (pthread_getschedparam(self, &policy, &param) != 0) return;
  int prio;
#ifdef SCHED_IDLE
  if (priority == IdlePriority) {
    policy = SCHED_IDLE;
    prio   = 0;
  } else
#endif
  {
#ifdef SCHED_IDLE
    const int lowest = LowestPriority;
#else
    const int lowest = IdlePriority;
#endif
    const int highest = TimeCriticalPriority;
    const int prioMin = sched_get_priority_min(policy);
    const int prioMax = sched_get_priority_max(policy);
    if (prioMin == -1 || prioMax == -1) return;
    prio = ((priority - lowest) * (prioMax - prioMin) / highest) + prioMin;
    prio = std::max(prioMin, std::min(prioMax, prio));
  }
  param.sched_priority = prio;
  int status           = pthread_setschedparam(self, policy, &param);
#ifdef SCHED_IDLE
  // were we trying to set to idle priority and failed?
  if (status == -1 && policy == SCHED_IDLE && errno == EINVAL) {
    // reset to lowest priority possible
    pthread_getschedparam(self, &policy, &param);
    param.sched_priority = sched_get_priority_min(policy);
    pthread_setschedparam(self, policy, &param);
  }
#else
  (void)status;
#endif
#endif
}
}

//=====================================================================

//=============================
//     ExecutorImp methods
//-----------------------------

ExecutorImp::ExecutorImp()
    : m_activeLoad(0)
    , m_liveWorkers(0)
    , m_maxLoad(TSystem::getProcessorCount() * 100)
    , m_transitionMutex()  // NOTE: We'll wait on this mutex - so it can't be
                           // recursive
{}

//---------------------------------------------------------------------

ExecutorImp::~ExecutorImp() {}

//---------------------------------------------------------------------

// A task is executable <==> its load allows it. The task load is considered
// fixed until another isExecutable() call is made again - in case it may
// change in time.
inline bool ExecutorImp::isExecutable(RunnableP &task) {
  return m_activeLoad + task->m_load <= m_maxLoad;
}

//---------------------------------------------------------------------

inline void ExecutorImp::insertTask(int schedulingPriority, RunnableP &task) {
  task->m_schedulingPriority = schedulingPriority;
  // before the others of its priority, as QMultiMap::insert() put it
  m_tasks.insert(m_tasks.lower_bound(schedulingPriority),
                 std::make_pair(schedulingPriority, task));
}

//---------------------------------------------------------------------

// As QMultiMap::remove(key, value): every entry with both.
inline bool ExecutorImp::removeTask(int schedulingPriority,
                                    const RunnableP &task) {
  bool removed = false;
  auto range   = m_tasks.equal_range(schedulingPriority);
  for (auto it = range.first; it != range.second;)
    if (it->second == task) {
      it      = m_tasks.erase(it);
      removed = true;
    } else
      ++it;
  return removed;
}

//=====================================================================

//========================
//    Runnable methods
//------------------------

Runnable::Runnable() : TSmartObject(m_classCode), m_id(0) {}

//---------------------------------------------------------------------

Runnable::~Runnable() {
  if (m_id) m_id->release();  // see Executor::addTask()
}

//---------------------------------------------------------------------

//! Returns the predicted CPU load generated by the task, expressed in
//! percentage
//! of core usage (that is, 100 is intended to fully occupy one processing
//! core).
//! Appropriate task load calibration is an important step to take when
//! implementing
//! a new task; for this purpose, remember some rules to follow:
//! <ul>
//! <li>In every moment, the task manager ensures that the overall sum of the
//! active
//!     task's load does not exceed the number of machine's processing cores
//!     multiplied
//!     by 100. This condition is \a blocking with respect to the execution of
//!     any other
//!     task - meaning that when a task is about to be executed the task manager
//!     \a waits
//!     until enough CPU resources are available to make it run.
//!     In particular, observe that a task's load \b never has to exceed the
//!     total CPU
//!     resources - doing so would surely result in a block of your application.
//!     The number
//!     of available cores can be accessed via the \b
//!     TSystem::getProcessorCount() or
//!     \b QThread::idealThreadCount().

//! <li>The task load is considered constant for the duration of the task.
//! Changing its
//!     value does not affect the task manager in any way once the task has been
//!     started.

//! <li>The default task load is 0, representing a very light task. If the task
//! load
//!     is 0 the condition at point 1 always succeeds - so the task is always
//!     executed when
//!     encountered. Observe that a long succession of 0 task loads leads to the
//!     creation of
//!     a proportional number of threads simultaneously running to dispatch it;
//!     if this is
//!     a problem, consider the use of \b Executor::setMaxActiveTasks()
//!     to make only a certain number of tasks being executed at the same time.
//! </ul>
int Runnable::taskLoad() { return 0; }

//---------------------------------------------------------------------

//! Returns the priority value used to schedule a task for execution. Tasks
//! with higher priority start before tasks with lower priority. The default
//! value returned is 5 (halfway from 0 to 10) - but any value other than
//! (std::numeric_limits<int>::max)() is acceptable.
int Runnable::schedulingPriority() { return 5; }

//---------------------------------------------------------------------

//! Returns the Priority used by worker threads when they adopt the task. The
//! default value returned is NormalPriority.
Priority Runnable::runningPriority() { return NormalPriority; }

//---------------------------------------------------------------------

inline bool Runnable::customConditions() {
  return (m_id->m_activeTasks < m_id->m_maxActiveTasks) &&
         (m_id->m_activeLoad + m_load <= m_id->m_maxActiveLoad);
}

/*!
  \fn void Runnable::started(RunnableP sender)

  This signal is emitted from working threads just before the run() code
  is executed. Observe that the passed smart pointer ensures the survival of
  the emitting task for the time required by connected slots execution.

  \warning The started(), finished() and exception() signals are emitted in
  a mutex-protected environment in order to provide the correct sequence of
  emissions (i.e. so that canceled() and terminated() controller-emitted signals
  are either delivered after started() and before finished() or exception(),
  or \a instead of them).
  \warning Thus, setting up blocking connections or \a direct slots that contain
  a
  blocking instruction or even calls to the Executor API (which would definitely
  try to relock the aforementioned mutex) is dangerous and could result in an
  application freeze.
  \warning In case it's necessary to use blocking features, they should be
  enforced
  through custom signals to be invoked manually in the run() method, outside the
  mutex.

  \sa \b finished and \b exception signals.
*/

/*!
  \fn void Runnable::finished(RunnableP sender)

  The \b finished signal is emitted from working threads once the run()
  code is returned without unmanaged exceptions.

  \sa \b started and \b exception signals.
*/

/*!
  \fn void Runnable::exception(RunnableP sender)

  The \b exception signal is emitted from working threads whenever an
  untrapped exception is found within the run() method.

  \sa \b started and \b finished signals.
*/

/*!
  \fn void Runnable::canceled(RunnableP sender)

  The \b canceled signal is emitted from the controller thread whenever
  a task which is currently under the task manager's control is canceled
  by the user (the signal is emitted from the thread invoking the cancel).
  Observe that tasks under execution are not stopped by the task manager
  when they are canceled, but the signal is emitted anyway - helping the
  user to stop the actual execution of the run() code in advance.

  \sa \b Executor::removeTask and \b Executor::cancelAll methods.
*/

/*!
  \fn void Runnable::terminated(RunnableP sender)

  The \b terminated signal is emitted from the controller thread when
  the Executor components are shutting down inside a call to
  Executor::shutdown(). Implementing a slot connected to this signal
  helps the user in controlling the flow of an Executor-multithreaded
  application when it is shutting down - for example, it can be imposed
  that the application must wait for the task to be finished, print logs
  or similar.
  This signal is always preceded by a canceled() signal, informing all
  active tasks that they should begin quitting on their own in a 'soft'
  way before brute termination may occur.

  \sa \b Executor::shutdown static method and \b Runnable::canceled signal.
*/

// (The on*() convenience slots are QtRunnable's, tthread_qt.cpp.)

//=====================================================================

//==========================
//    ExecutorId methods
//--------------------------

ExecutorId::ExecutorId()
    : m_activeTasks(0)
    , m_maxActiveTasks(1)
    , m_activeLoad(0)
    , m_maxActiveLoad((std::numeric_limits<int>::max)())
    , m_dedicatedThreads(false)
    , m_persistentThreads(false) {
  std::lock_guard<std::mutex> transitionLocker(globalImp->m_transitionMutex);

  m_id = globalImp->m_executorIdPool.acquire();
  globalImp->m_waitingFlagsPool.resize(globalImp->m_executorIdPool.size());
}

//---------------------------------------------------------------------

ExecutorId::~ExecutorId() {
  std::lock_guard<std::mutex> transitionLocker(globalImp->m_transitionMutex);

  if (m_dedicatedThreads) {
    m_persistentThreads = 0;
    refreshDedicatedList();
  }

  globalImp->m_executorIdPool.release(m_id);
}

//---------------------------------------------------------------------

// Make sure that sleeping workers are eliminated properly if the permanent
// workers count decreases.
void ExecutorId::refreshDedicatedList() {
  // QMutexLocker transitionLocker(&globalImp->m_transitionMutex);  //Already
  // covered

  if (!m_dedicatedThreads || !m_persistentThreads) {
    // Release all sleeping workers
    // Wake them - they will exit on their own

    unsigned int i, size = m_sleepings.size();
    for (i = 0; i < size; ++i) {
      m_sleepings[i]->m_exit       = true;
      m_sleepings[i]->m_sleepingIn = 0;
      m_sleepings[i]->m_waitCondition.notify_one();
    }

    m_sleepings.clear();
  }
}

//=====================================================================

//===========================
//      Worker methods
//---------------------------

Worker::Worker() : m_task(0), m_master(0), m_exit(true), m_sleepingIn(0) {}

//---------------------------------------------------------------------

Worker::~Worker() {}

//---------------------------------------------------------------------

// Called with the transition mutex locked, as QThread::start() was. When
// run() returns, the worker leaves the workers list (whatever way it
// returned: known issue 45 was one it did not) and is deleted outside the
// mutex, since that may release the last references to a task and to an
// ExecutorId, whose destructor locks the mutex.
//
// The worker counts itself out (m_liveWorkers) only once it is deleted, so
// that when shutdown() sees no live worker, none of them touches anything
// any more.
void Worker::start() {
  ++globalImp->m_liveWorkers;
  std::thread([this] {
    run();
    {
      std::lock_guard<std::mutex> lock(globalImp->m_transitionMutex);
      globalImp->m_workers.erase(this);
    }
    delete this;
    std::lock_guard<std::mutex> lock(globalImp->m_transitionMutex);
    --globalImp->m_liveWorkers;
  }).detach();
}

//---------------------------------------------------------------------

void Worker::run() {
  // Ensure atomicity of worker's state transitions
  std::unique_lock<std::mutex> sl(globalImp->m_transitionMutex);

  if (shutdownVar) return;

  for (;;) {
    // Run the taken task
    setCurrentThreadPriority(m_task->runningPriority());

    try {
      m_task->notifyStarted();
      sl.unlock();

      m_task->run();

      sl.lock();
      m_task->notifyFinished();
    } catch (...) {
      if (!sl.owns_lock()) sl.lock();  // throw must be in the run() block
      m_task->notifyException();
    }

    updateCountsOnRelease();

    if (shutdownVar) return;

    // Get the next task
    takeTask();

    if (!m_task) {
      onFinish();

      if (!m_exit && !shutdownVar) {
        // Put the worker to sleep, until it is given a task (newWorker())
        // or released (refreshDedicatedList()), which also takes it out of
        // the sleeping list; a spurious wake-up sleeps again.
        m_waitCondition.wait(sl, [this] { return m_task || m_exit; });

        // Released: m_task == 0 and we return.
        if (!m_task || shutdownVar) return;
      } else
        return;
    }
  }
}

//---------------------------------------------------------------------

inline void Worker::updateCountsOnTake() {
  globalImp->m_activeLoad += m_task->m_load;
  m_task->m_id->m_activeLoad += m_task->m_load;
  ++m_task->m_id->m_activeTasks;
}

//---------------------------------------------------------------------

inline void Worker::updateCountsOnRelease() {
  globalImp->m_activeLoad -= m_task->m_load;
  m_task->m_id->m_activeLoad -= m_task->m_load;
  --m_task->m_id->m_activeTasks;
}

//---------------------------------------------------------------------

inline void Worker::onFinish() {
  if (m_master && m_master->m_dedicatedThreads &&
      m_master->m_persistentThreads) {
    m_exit = false;
    m_master->m_sleepings.push_back(this);
    m_sleepingIn = m_master.getPointer();

    // Unlock the mutex - since eventually invoked ~ExecutorId will relock it...
    globalImp->m_transitionMutex.unlock();

    m_master =
        0;  // Master may be destroyed here - and m_exit= true for all sleepings
    // in that case

    globalImp->m_transitionMutex.lock();
  } else {
    m_exit = true;
    globalImp->m_workers.erase(this);
  }
}

//=====================================================================

//===========================
//     Executor methods
//---------------------------

Executor::Executor() : m_id(new ExecutorId) { m_id->addRef(); }

//---------------------------------------------------------------------

Executor::~Executor() { m_id->release(); }

//---------------------------------------------------------------------

//! This static method declares the use of the Executor's task manager into
//! the application code. Be sure to use it according to the following rules:
//! <ul>
//! <li> Only QCoreApplications or QApplications may use Executors.
//! <li> This method must be invoked in a thread which performs constant Qt
//! event
//!      processing - like the main loop of interactive GUI applications.
//! <li> No task processing is allowed after event processing stops.
//! </ul>
void Executor::init() {
  // If no global ExecutorImp exists, allocate it now. (The workers no longer
  // need the QCoreApplication since step 9c; the tasks' signals, delivered
  // to the main thread, still do.)
  if (!globalImp) globalImp = new ExecutorImp;
}

//---------------------------------------------------------------------

//! This static method, which \b must be invoked in the controller thread,
//! declares
//! termination of all Executor-based components, forcing the execution of tasks
//! submitted
//! by any Executor to quit as soon as possible in a safe way.
//! When the shutdown method is invoked, the task manager first emits a
//! canceled()
//! signal for all the tasks that were submitted to it, independently from the
//! Executor that
//! performed the submission; then, tasks that are still active once all the
//! cancellation signals
//! were delivered further receive a terminated() signal informing them that
//! they must provide
//! code termination (or at least remain silent in a safe state until the
//! application quits).

//! \b NOTE: Observe that this method does not explicitly wait for all the tasks
//! to terminate - this depends
//! on the code connected to the terminated() signal and is under the user's
//! responsibility (see the
//! remarks specified in started() signal description); if this is the intent
//! and the terminated slot
//! is invoked in the controller thread, you should remember to implement a
//! local event loop in it (so that
//! event processing is still performed) and wait there until the first
//! finished() or catched()
//! slot make it quit.
void Executor::shutdown() {
  {
    // Updating tasks list - lock against state transitions
    std::lock_guard<std::mutex> transitionLocker(globalImp->m_transitionMutex);

    shutdownVar = true;

    // Cancel all tasks  - first the active ones
    std::set<Worker *>::iterator it;
    for (it = globalImp->m_workers.begin(); it != globalImp->m_workers.end();
         ++it) {
      RunnableP task = (*it)->m_task;
      if (task) task->notifyCanceled();
    }

    // Finally, deal with the global queue tasks
    auto &tasks = globalImp->m_tasks;
    for (auto jt = tasks.begin(); jt != tasks.end();) {
      RunnableP task = jt->second;
      task->notifyCanceled();
      jt = tasks.erase(jt);
    }

    // Now, send the terminate() signal to all active tasks
    for (it = globalImp->m_workers.begin(); it != globalImp->m_workers.end();
         ++it) {
      RunnableP task = (*it)->m_task;
      if (task) task->notifyTerminated();
    }

    // Release the dedicated workers that sleep for their executor (they
    // would sleep until it is destroyed): out of its list, and woken.
    for (it = globalImp->m_workers.begin(); it != globalImp->m_workers.end();
         ++it) {
      Worker *worker = *it;
      if (worker->m_task || !worker->m_sleepingIn) continue;
      std::deque<Worker *> &sleepings = worker->m_sleepingIn->m_sleepings;
      sleepings.erase(std::remove(sleepings.begin(), sleepings.end(), worker),
                      sleepings.end());
      worker->m_sleepingIn = 0;
      worker->m_exit       = true;
      worker->m_waitCondition.notify_one();
    }
  }


  // Wait for every worker thread to end (decision of 2026-11-20, step 9 of
  // doc/nexttoonz_core_noqt.md): the running tasks were told to stop, and
  // the program is about to exit. The wait goes through the main thread
  // dispatcher, so that events (queued signals, sendBlocking() messages
  // from the workers) are still processed meanwhile; a helper thread wakes
  // it every 50 milliseconds. After 5 seconds it returns anyway, as it
  // always did, with a line on standard error.
  using Clock                     = std::chrono::steady_clock;
  const Clock::time_point timeout = Clock::now() + std::chrono::seconds(5);
  TMainThreadDispatcher *dispatcher = TThread::mainThreadDispatcher();
  std::atomic<bool> waiting(true);
  std::thread waker([&] {
    while (waiting) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      if (waiting) dispatcher->post([] {});
    }
  });
  // The queued slots of the signals above run here too, without a return to
  // an event loop (it was a QCoreApplication::processEvents()): a marker
  // posted now runs after them, and the wait lasts at least until then.
  std::atomic<bool> drained(false);
  dispatcher->post([&drained] { drained = true; });
  dispatcher->waitUntil([&] {
    return drained && (liveWorkerCount() == 0 || Clock::now() >= timeout);
  });
  waiting = false;
  waker.join();
  if (int left = liveWorkerCount())
    std::fprintf(stderr,
                 "NextToonz: TThread::shutdown() returns after 5 seconds with "
                 "%d worker thread%s still running\n",
                 left, left == 1 ? "" : "s");
}

//---------------------------------------------------------------------

//! The worker threads that have been started and have not ended yet, sleeping
//! ones included.
int Executor::liveWorkerCount() {
  if (!globalImp) return 0;
  std::lock_guard<std::mutex> transitionLocker(globalImp->m_transitionMutex);
  return globalImp->m_liveWorkers;
}

//---------------------------------------------------------------------

//! Specifies the use of dedicated threads for the Executor's task group.

//! By default a worker thread attempts adoption of Runnable tasks
//! without regard to the Executor that performed the submission. This helps
//! in stabilizing the number of threads that are created and destroyed
//! by the task manager - but may be a problem in some cases.

//! Using this method the user can explicitly tell the Executor to seize the
//! ownership of worker threads assigned to its tasks, so that they will not
//! try adoption of external tasks but instead remain focused on Executor's
//! tasks only.

//! An optional \b persistent parameter may be passed, which specifies if
//! dedicated threads should remain sleeping or should rather die when no
//! processable tasks from the Executor are found.

//! This method is especially helpful in two occasions:
//! <ul>
//! <li> The Executor's tasks use thread-specific data such as QThreadStorages,
//!      which may be recycled among different tasks.
//! <li> The Executor receives tasks at a frequent rate, but mostly ends each
//! one before
//!      another one is submitted - resulting in a continuous thread turnover.
//! </ul>

void Executor::setDedicatedThreads(bool dedicated, bool persistent) {
  std::lock_guard<std::mutex> transitionLocker(globalImp->m_transitionMutex);

  m_id->m_dedicatedThreads  = dedicated;
  m_id->m_persistentThreads = persistent;

  m_id->refreshDedicatedList();
}

//---------------------------------------------------------------------

//! Submits a task for execution. The task is executed according to
//! its task load, insertion time and scheduling priority.
void Executor::addTask(RunnableP task) {
  {
    if (task->m_id)  // Must be done outside transition lock, since eventually
      task->m_id->release();  // invoked ~ExecutorId will lock it

    // Updating tasks and workers list - lock against state transitions
    std::lock_guard<std::mutex> transitionLocker(globalImp->m_transitionMutex);

    task->m_id = m_id;
    m_id->addRef();

    globalImp->insertTask(task->schedulingPriority(), task);

    // Workers are std::threads, which any thread may start: the assignments
    // are refreshed here, in whatever thread adds the task (it was a signal,
    // direct in the main thread and queued to it from the others).
    globalImp->refreshAssignments();
  }
}

//---------------------------------------------------------------------

//! Removes the given task from scheduled execution and emits its
//! Runnable::canceled signal. Tasks already under execution are not
//! stopped by this method - although the canceled signal is still emitted.
//! It has no effect if the task is not currently under the task manager's
//! control.
//! \sa \b Runnable::canceled signal and the \b cancelAll method.
void Executor::removeTask(RunnableP task) {
  // If the task does not belong to this Executor, quit.
  if (task->m_id != m_id) return;

  // Updating tasks list - lock against state transitions
  std::lock_guard<std::mutex> transitionLocker(globalImp->m_transitionMutex);

  // Then, look in the global queue - if it is found, emiminate the task and
  // send the canceled signal.
  if (globalImp->removeTask(task->m_schedulingPriority, task)) {
    task->notifyCanceled();
    return;
  }

  // Finally, the task may be running - look in workers.
  std::set<Worker *> &workers = globalImp->m_workers;
  std::set<Worker *>::iterator it;
  for (it = workers.begin(); it != workers.end(); ++it)
    if (task && (*it)->m_task == task) task->notifyCanceled();

  // No need to refresh - tasks were eventually decremented...
}

//---------------------------------------------------------------------

//! Clears the task manager of all tasks added by this Executor and emits
//! the Runnable::canceled signal for each of them. The same specifications
//! described in the \b removeTask method apply here.
//! \sa \b Runnable::canceled signal and the \b removeTask method.
void Executor::cancelAll() {
  // Updating tasks list - lock against state transitions
  std::lock_guard<std::mutex> transitionLocker(globalImp->m_transitionMutex);

  // Clear the tasks chronologically. So, first check currently working
  // tasks.
  std::set<Worker *>::iterator it;
  for (it = globalImp->m_workers.begin(); it != globalImp->m_workers.end();
       ++it) {
    RunnableP task = (*it)->m_task;
    if (task && task->m_id == m_id) task->notifyCanceled();
  }

  // Finally, clear the global tasks list from all tasks inserted by this
  // executor
  // NOTE: An easier way here?
  auto &tasks = globalImp->m_tasks;
  for (auto jt = tasks.begin(); jt != tasks.end();) {
    if (jt->second->m_id == m_id) {
      RunnableP task = jt->second;
      task->notifyCanceled();
      jt = tasks.erase(jt);
    } else
      ++jt;
  }
}

//---------------------------------------------------------------------

//! Declares that only a certain number of tasks added by this Executor
//! may be processed simultaneously. The default is 1 - meaning that tasks
//! added to the executor are completely serialized. A negative task number
//! disables any form of task serialization.
//! \b NOTE: Currently, tasks that do not
//! satisfy this condition avoid blocking execution of tasks not
//! added by the same Executor - even if they were scheduled for later
//! execution.
void Executor::setMaxActiveTasks(int maxActiveTasks) {
  std::lock_guard<std::mutex> transitionLocker(globalImp->m_transitionMutex);

  if (maxActiveTasks <= 0)
    m_id->m_maxActiveTasks = (std::numeric_limits<int>::max)();
  else
    m_id->m_maxActiveTasks = maxActiveTasks;
}

//---------------------------------------------------------------------

int Executor::maxActiveTasks() const {
  std::lock_guard<std::mutex> transitionLocker(globalImp->m_transitionMutex);
  return m_id->m_maxActiveTasks;
}

//---------------------------------------------------------------------

//! Declares a maximum overall task load for the tasks added by this Executor.
//! \b NOTE: The same remark for setMaxActiveTasks() holds here.
void Executor::setMaxActiveLoad(int maxActiveLoad) {
  std::lock_guard<std::mutex> transitionLocker(globalImp->m_transitionMutex);

  m_id->m_maxActiveLoad = maxActiveLoad;
}

//---------------------------------------------------------------------

int Executor::maxActiveLoad() const {
  std::lock_guard<std::mutex> transitionLocker(globalImp->m_transitionMutex);
  return m_id->m_maxActiveLoad;
}

//=====================================================================

//=====================================================================
//      Task adoption methods
//---------------------------------------------------------------------

inline void ExecutorId::newWorker(RunnableP &task) {
  Worker *worker;

  if (m_sleepings.size()) {
    worker = m_sleepings.front();
    m_sleepings.pop_front();
    worker->m_sleepingIn = 0;
    worker->m_task = task;
    worker->updateCountsOnTake();
    worker->m_waitCondition.notify_one();
  } else {
    worker = new Worker;
    globalImp->m_workers.insert(worker);
    worker->m_task = task;
    worker->updateCountsOnTake();
    worker->start();
  }
}

//---------------------------------------------------------------------

inline void Worker::adoptTask(RunnableP &task) {
  m_task = task;
  updateCountsOnTake();
}

//---------------------------------------------------------------------

//  * Le task timedOut sono ex-accumulate che non soddisfano le condizioni
//    standard. Quindi sono bloccanti *ovunque*.

//  * Il refresh dei worker sulle task accumulate *deve* avvenire:
//      -Se e solo se una task dello stesso executor finisce,
//       perche' e' l'unico caso in cui le custom conditions
//       vengono aggiornate

//  * Se un thread dedicato non puo' prendere una task estranea, non e' detto
//    che altri non possano!
//  * Le task che richiedono dedizione dovrebbero essere adottate da thread
//    gia' dedicati, se esistono! => Gli executor che richiedono thread dedicati
//    li creano a parte e non li condividono con nessuno: cioe', thread creati
//    senza dedizione non devono poter adottare task richiedenti dedizione!

//---------------------------------------------------------------------

// Assigns tasks polled from the id's accumulation queue (if id is given) and
// the global tasks queue.
// It works like:

//  a) First look if there exist tasks with timedOut priority and if so
//     try to take them out
//  b) Then look for tasks in the id's accumulation queue
//  c) Finally search in the remaining global tasks queue

void ExecutorImp::refreshAssignments() {
  // QMutexLocker transitionLocker(&globalImp->m_transitionMutex);  //Already
  // covered

  if (m_tasks.empty()) return;

  // Erase the id vector data
  assert(m_executorIdPool.size() == m_waitingFlagsPool.size());
  memset(&m_waitingFlagsPool.front(), 0, m_waitingFlagsPool.size());

  // c) Try with the global queue
  int e, executorsCount = m_executorIdPool.acquiredSize();

  int i, tasksCount = m_tasks.size();
  // From the last task back, as the QMultiMap was. The step back is taken at
  // the start of each pass, never from begin() or from the end() of a map
  // that the erase below has emptied (QMultiMap let the loop do both; a
  // std::multimap does not). After an erase, `it` is the task after the
  // erased one, so the step back lands on the one before it.
  std::multimap<int, RunnableP>::iterator it = m_tasks.end();
  for (i = 0, e = 0; i < tasksCount && e < executorsCount &&
                     it != m_tasks.begin();
       ++i) {
    --it;
    // std::cout<< "global tasks-refreshAss" << std::endl;
    // Take the task
    RunnableP task = it->second;
    task->m_load   = task->taskLoad();

    UCHAR &idWaitingForAnotherTask = m_waitingFlagsPool[task->m_id->m_id];
    if (idWaitingForAnotherTask) continue;

    if (!isExecutable(task)) break;

    if (!task->customConditions()) {
      ++e;
      idWaitingForAnotherTask = 1;
    } else {
      task->m_id->newWorker(task);
      it = m_tasks.erase(it);
    }
  }
}

//---------------------------------------------------------------------

inline bool Worker::canAdopt(const RunnableP &task) {
  return task->m_id->m_sleepings.size() ==
             0 &&  // Always prefer sleeping dedicateds if present
         (!m_master || (m_master.getPointer() == task->m_id));  // If was seized
                                                                // by an
                                                                // Executor,
                                                                // ensure task
                                                                // compatibility
}

//---------------------------------------------------------------------

// Takes a task and assigns it to the worker in a way similar to the one above.
inline void Worker::takeTask() {
  TSmartPointerT<ExecutorId> oldId = m_task->m_id;

  // When a new task is taken, the old one's Executor may seize the worker
  m_master = oldId->m_dedicatedThreads ? oldId : (TSmartPointerT<ExecutorId>)0;

  // c) No accumulated task can be taken - look for a task in the global tasks
  // queue.
  //   If the active load admits it, take the earliest task.

  // Free the old task. NOTE: This instruction MUST be performed OUTSIDE the
  // mutex-protected environment -
  // since user code may be executed upon task destruction - including the mutex
  // relock!!

  globalImp->m_transitionMutex.unlock();

  m_task = 0;
  oldId  = TSmartPointerT<ExecutorId>();

  globalImp->m_transitionMutex.lock();

  // Erase the executor id status pool
  tcg::indices_pool<> &executorIdPool  = globalImp->m_executorIdPool;
  std::vector<UCHAR> &waitingFlagsPool = globalImp->m_waitingFlagsPool;

  assert(waitingFlagsPool.size() == globalImp->m_executorIdPool.size());
  memset(&waitingFlagsPool.front(), 0, waitingFlagsPool.size());

  int e, executorsCount = executorIdPool.acquiredSize();

  int i, tasksCount = globalImp->m_tasks.size();
  // (stepping back as in ExecutorImp::refreshAssignments())
  std::multimap<int, RunnableP>::iterator it = globalImp->m_tasks.end();
  for (i = 0, e = 0; i < tasksCount && e < executorsCount &&
                     it != globalImp->m_tasks.begin();
       ++i) {
    --it;
    // std::cout<< "global tasks-takeTask" << std::endl;
    // Take the first task
    RunnableP task = it->second;
    task->m_load   = task->taskLoad();

    UCHAR &idWaitingForAnotherTask = waitingFlagsPool[task->m_id->m_id];
    if (idWaitingForAnotherTask) continue;

    if (!globalImp->isExecutable(task)) break;

    // In case the worker was captured for dedication, check the task
    // compatibility.
    if (!canAdopt(task)) {
      // some other worker may still take the task... (refreshed here and now:
      // it was a signal queued to the main thread)
      globalImp->refreshAssignments();
      break;
    }

    // Test its custom conditions
    if (!task->customConditions()) {
      ++e;
      idWaitingForAnotherTask = 1;
    } else {
      adoptTask(task);
      it = globalImp->m_tasks.erase(it);

      globalImp->refreshAssignments();
      break;
    }
  }
}
