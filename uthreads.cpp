#include "uthreads.h"
#include <iostream>
#include "stdlib.h"
#include <signal.h>
#include <setjmp.h>
#include <sys/time.h>
#include <deque>
#include <queue>
using std::deque;
using std::queue;


typedef void (*func)();

#define THREAD_ALLOCATION_FAIL_MSG "system error: Thread Allocation Failed"
#define SET_TIMER_FAILED "system error: setting timer failed"

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
		"rol    $0x9,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

#endif


class Thread
{
private:
    sigjmp_buf env;
    int id;
    bool is_blocked;
    bool asked_for_mutix;
    int quantums;
public:
    Thread(int id, func f)
    {
        char stack[STACK_SIZE];
        this->id = id;
        this->is_blocked = false;
        this->asked_for_mutix = false;
        this->quantums = 0;
        address_t sp = (address_t)stack + STACK_SIZE - sizeof(address_t );
        address_t pc = (address_t)f;
        (this->env->__jmpbuf)[JB_SP] = translate_address(sp);
        (this->env->__jmpbuf)[JB_PC] = translate_address(pc);
        sigemptyset(&env->__saved_mask);
    }

    int get_id() const
    {
        return this->id;
    }

    sigjmp_buf *get_env()
    {
        return &(this->env);
    }

    bool get_is_blocked() const
    {
        return is_blocked;
    }
    void inc_quantums()
    {
        quantums += 1;
    }

    int get_quantums() const
    {
        return quantums;
    }

    void block_thread()
    {
        is_blocked = true;
    }

    void unblock_thread()
    {
        is_blocked = false;
    }

    void need_mutix()
    {
        asked_for_mutix = true;
    }

    void release_mutix()
    {
        asked_for_mutix = false;
    }

    bool get_need_mutix() const
    {
        return this->asked_for_mutix;
    }

};


int numThreads = 0;
Thread *threads[MAX_THREAD_NUM] = {nullptr}; // list of all threads each in its own id index
Thread *runningThread; // the currently running thread
deque<Thread*> threads_queue;  // all threads
queue<Thread*> mutex_Blocked; // threads blocked after asking for mutix
bool mutix = true; // if true means mutix is available
Thread* locking_thread = nullptr; // the thread currently locking the mutix
int totalQuantums = 0;
sigset_t set;


void do_nothing()
{
    for(;;){}
}

/**
 * find minimum thread id that has not been used
 * @return id of thread
 */
int get_min()
{
    for (int i = 0; i < MAX_THREAD_NUM; ++i)
    {
        if (threads[i] == nullptr)
        {
            return i;
        }
    }
    return MAX_THREAD_NUM;
}

void block_signals()
{
    //todo
    sigprocmask(SIG_BLOCK, &set, nullptr);
}

void unblock_signals()
{
    //todo : check failure.
    sigprocmask(SIG_UNBLOCK,&set, nullptr);
}
/**
 * Free all threads spawned, call when failure or finished executing
 */
void free_all()
{
    block_signals();
    Thread* current;
    while(!threads_queue.empty())
    {
        current = threads_queue.front();
        threads_queue.pop_front();
        delete current;
    }
    unblock_signals();
}

void print_err(std::string string)
{
    block_signals();
    free_all();
    std::cout<<string<<std::endl;
    exit(1);
}

void switchThreads(bool terminated = false)
{
    block_signals();
    int retVal = sigsetjmp(*(runningThread->get_env()), 1);
    if(retVal != 0)
    {
        if(runningThread->get_need_mutix())
        {
            uthread_mutex_unlock();
        }
        return;
    }

    Thread *new_th = threads_queue.front();
    threads_queue.pop_front();
    if(terminated)
    {
        threads_queue.push_back(new_th);
    }
    else
    {
        totalQuantums += 1;
    }
    while(new_th->get_is_blocked())
    {
        new_th = threads_queue.front();
        threads_queue.pop_front();
    }
    new_th = threads_queue.front();
    runningThread = new_th;
    runningThread->inc_quantums();
    unblock_signals();
    siglongjmp(*(runningThread->get_env()),1);
}

void time_handler(int sig)
{
    switchThreads();
}


int uthread_init(int quantum_usecs)
{
    if(quantum_usecs <= 0)
    {
        return -1;
    }
    struct sigaction sa = {0};
    struct itimerval timer;
    sa.sa_handler = &time_handler;
    totalQuantums += 1;
    Thread* mainThread = new Thread(0, do_nothing); //todo: mem leak
    mainThread->inc_quantums();
    threads_queue.push_back(mainThread);
    threads[0] = mainThread;
    numThreads += 1;
    runningThread = mainThread; // todo ?
    sigaddset(&set, SIGALRM);

    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = quantum_usecs;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = quantum_usecs;

    if(setitimer(ITIMER_VIRTUAL, &timer, NULL))
    {
        print_err(SET_TIMER_FAILED);
    }
    return 0;
}

int uthread_spawn(void (*f)())
{
    block_signals();
    if(numThreads >= MAX_THREAD_NUM)
    {
        unblock_signals();
        return -1;
    }
    try
    {
        int id = get_min();
        Thread* th = new Thread(id, f);
        threads_queue.push_back(th);
        threads[id] = th;
        ++numThreads;
    } catch (std::bad_alloc&)
    {
        print_err(THREAD_ALLOCATION_FAIL_MSG);
    }

    unblock_signals();
    return 0;
}

int uthread_get_tid()
{
    return runningThread->get_id();
}

int uthread_terminate(int tid)
{
    block_signals();
    if(threads[tid] == nullptr)
    {
        return -1;
    }

    if(tid == 0)
    {
        free_all();
        exit(0);
    }

    if (runningThread == threads[tid])
    {
        threads[tid] = nullptr;
        switchThreads(true);
    }
    else
    {
        Thread* curr = threads[tid];
        for(auto it = threads_queue.begin(); it != threads_queue.end();it++)
        {
            if ((*it)->get_id() == tid)
            {
                threads_queue.erase(it);
                break;
            }
        }

        delete curr;
        threads[tid] = nullptr;
        --numThreads;
    }

    unblock_signals();
    return 0;
}

int uthread_block(int tid)
{
    block_signals();
    if(threads[tid] == nullptr || tid == 0)
    {
        unblock_signals();
        return -1;
    }

    threads[tid]->block_thread();
    if(threads[tid] == runningThread)
    {
        switchThreads();
    }
    unblock_signals();
    return 0;
}

int uthread_resume(int tid)
{
    block_signals();
    if(threads[tid] == nullptr)
    {
        unblock_signals();
        return -1;
    }
    threads[tid]->unblock_thread();
    unblock_signals();
    return 0;
}


int uthread_get_total_quantums()
{
    return totalQuantums;
}

int uthread_get_quantums(int tid)
{
    if(threads[tid] == nullptr)
    {
        return -1;
    }
    return threads[tid]->get_quantums();
}

int uthread_mutex_lock()
{
    block_signals();
    if(mutix)
    {
        mutix = false;
        locking_thread = runningThread;
    }
    else
    {
        if(locking_thread == runningThread)
        {
            unblock_signals();
            return -1;
        }
        mutex_Blocked.push(runningThread);
        runningThread->need_mutix();
        uthread_block(runningThread->get_id());
    }

    unblock_signals();
    return 0;
}

int uthread_mutex_unlock()
{

    block_signals();
    if(mutix)
    {
        unblock_signals();
        return -1;
    }

    mutix = true;
    runningThread->release_mutix();
    Thread* unblock_thread = mutex_Blocked.front();
    unblock_thread->unblock_thread();
    mutex_Blocked.pop();
    unblock_signals();
    return 0;
}

