#include "uthreads.h"
#include <iostream>
#include "stdlib.h"
#include <signal.h>
#include <setjmp.h>
#include <sys/time.h>
#include <deque>
using std::deque;


typedef void (*func)();

#define THREAD_ALLOCATION_FAIL_MSG "system error: Thread Allocation Failed"

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
public:
    Thread(int id, func f)
    {
        char stack[STACK_SIZE];
        this->id = id;
        this->is_blocked = false;
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

    void switch_blocked()
    {
        is_blocked = !is_blocked;
    }

};


int numThreads = 0;
Thread *threads[MAX_THREAD_NUM] = {nullptr};
Thread *runningThread;
bool used_ids[MAX_THREAD_NUM] = {false};
deque<Thread*> threads_queue;
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
        if (!used_ids[i])
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

int uthread_init(int quantum_usecs)
{
    if(quantum_usecs <= 0)
    {
        return -1;
    }
    Thread* mainThread = new Thread(0, do_nothing);
    threads_queue.push_back(mainThread);
    used_ids[0] = true;
    threads[0] = mainThread;
    numThreads += 1;
    runningThread = mainThread; // todo ?
    sigaddset(&set, SIGALRM);
    return 0;
}



void switchThreads(bool is_terminated = false)
{
    block_signals();
    int retVal = sigsetjmp(*(runningThread->get_env()), 1);
    if(retVal != 0)
    {
        return;
    }
    Thread *new_th = threads_queue.front();
    threads_queue.pop_front();
    threads_queue.push_back(new_th);
    while(new_th->get_is_blocked())
    {
        new_th = threads_queue.front();
        threads_queue.pop_front();
    }
    Thread* new_thread = threads_queue.front();


}

int uthread_spawn(void (*f)(void))
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
        used_ids[id] = true;
        threads[id] = th;
        ++numThreads;
    } catch (std::bad_alloc&)
    {
        std::cerr << THREAD_ALLOCATION_FAIL_MSG << std::endl;
        free_all();
        exit(1);
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
    if(!used_ids[tid])
    {
        return -1;
    }

    if(tid == 0)
    {
        //unblock_signals();
        free_all();
        exit(0);
    }

    if (runningThread == threads[tid])
    {

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
        used_ids[tid] = false;
    }

    unblock_signals();
}