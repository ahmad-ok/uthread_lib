#include "uthreads.h"
#include <iostream>
#include "stdlib.h"
#include <signal.h>
#include <setjmp.h>
#include <sys/time.h>
#include <queue>
using std::queue;


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
    unsigned int id;
public:
    Thread(unsigned int id, func f)
    {
        char stack[STACK_SIZE];
        this->id = id;
        address_t sp = (address_t)stack + STACK_SIZE - sizeof(address_t );
        address_t pc = (address_t)f;
        (this->env->__jmpbuf)[JB_SP] = translate_address(sp);
        (this->env->__jmpbuf)[JB_PC] = translate_address(pc);
        sigemptyset(&env->__saved_mask);
    }

    unsigned int get_id() const
    {
        return this->id;
    }
};


int numThreads = 0;
Thread *currentThread;
bool used_ids[MAX_THREAD_NUM] = {false};
queue<Thread*> threads_queue;
sigset_t set;

void do_nothing()
{
    for(;;){}
}
int uthread_init(int quantum_usecs)
{
    if(quantum_usecs <= 0)
    {
        return -1;
    }
    Thread* mainThread = new Thread(0, do_nothing);
    threads_queue.push(mainThread);
    used_ids[0] = true;
    numThreads += 1;
    currentThread = mainThread; // todo ?
    sigaddset(&set, SIGALRM);
    return 0;
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
    sigprocmask(SIG_BLOCK, &set, nullptr);
}

void unblock_signals()
{
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
        threads_queue.pop();
        delete current;
    }

    unblock_signals();

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
        threads_queue.push(th);
        used_ids[id] = true;
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

int uthread_terminate(int tid)
{}