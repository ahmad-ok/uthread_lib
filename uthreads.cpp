#include "uthreads.h"
#include <iostream>
#include "stdlib.h"
#include <signal.h>
#include <setjmp.h>
#include <sys/time.h>
#include <list>
using std::list;


typedef void (*func)();

#define THREAD_ALLOCATION_FAIL_MSG "system error: Thread Allocation Failed\n"
#define SET_TIMER_FAILED "system error: setting timer failed\n"
#define SET_SIGACTION_FAIL_MSG "system error: setting sigaction failed\n"
#define INIT_ERROR_MSG "thread library error: invalid quantum length\n"
#define THREAD_NOT_INITIALIZED_ERR_MSG "thread library error: thread does not exist, cannot terminate\n"
#define THREAD_BLOCK_ERROR_MSG "thread library error: cannot block nonexistent thread or main thread (id 0)\n"
#define THREAD_EXCEEDED_MAX_NUM_MSG "thread library error: number of threads exceeded max amount\n"
#define THREAD_DOES_NOT_EXIST_MSG "thread library error: thread does not exist\n"
#define MUTIX_ALREADY_LOCKED_MSG "Mutix is already locked by this thread\n"
#define MUTIX_ALREADY_UNLOCKED_MSG "Mutix is already unlocked\n"
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
struct sigaction sa = {0};
struct itimerval timer;

/**
 * Class Representing a Thread
 */
class Thread
{
private:
    sigjmp_buf env;
    int id;
    bool is_blocked;
    bool asked_for_mutix;  // to check if a thread that starts to run needs the Mutix
    int quantums;
    char stack[STACK_SIZE];
public:
    Thread(int id, func f)
    {
        sigsetjmp(env, 1);
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
    /**
     * @return Thread's ID
     */
    int get_id() const
    {
        return this->id;
    }

    /**
     * @return Thread's buffer
     */
    sigjmp_buf *get_env()
    {
        return &(this->env);
    }

    /**
     * @return is the Thread Blocked
     */
    bool get_is_blocked() const
    {
        return is_blocked;
    }

    /**
     * increments Thread's Quantums by 1
     */
    void inc_quantums()
    {
        quantums += 1;
    }

    /**
     * @return Thread's Quantums
     */
    int get_quantums() const
    {
        return quantums;
    }

    /**
     * set the Thread to Blocked State
     */
    void block_thread()
    {
        is_blocked = true;
    }

    /**
     * set Thread's Sate to Unblocked
     */
    void unblock_thread()
    {
        is_blocked = false;
    }

    /**
     * mark the Thread as waiting for Mutix
     */
    void wait_mutix()
    {
        asked_for_mutix = true;
    }

    /**
     * Mark the Thread not to be Waiting For Mutix
     */
    void release_mutex()
    {
        asked_for_mutix = false;
    }

    bool get_need_mutex() const
    {
        return this->asked_for_mutix;
    }

};
//Global Variables
int threadsNum = 0; // total Threads Number
list<Thread*> readyThreads; //List containing ready Threads
list<Thread*> blockedByMutixThreads; // List Containing Threads Waiting For Mutix
Thread* allThreads[MAX_THREAD_NUM] = {nullptr}; // List of All current Threads
sigset_t set; // set Containing Signals to be masked
int totalQuantums = 0;
Thread* runningThread = nullptr;  // Currently Ready Threads Queue
int quantumusec = 0;
bool mutixIsAvailable = true;
Thread* mutixLocker = nullptr; //Thread Currently Blocking Mutix


/**
 * find minimum thread id that has not been used
 * @return id of thread
 */
int get_min()
{
    for (int i = 0; i < MAX_THREAD_NUM; ++i)
    {
        if (allThreads[i] == nullptr)
        {
            return i;
        }
    }
    return MAX_THREAD_NUM;
}

/**
 * Block Signals in the set
 */
void block_signals()
{
    //todo
    sigprocmask(SIG_BLOCK, &set, nullptr);
}

/**
 * unblock the Blocked Signals
 */
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
    for(int i = 0; i < MAX_THREAD_NUM; ++i)
    {
        delete allThreads[i];
        allThreads[i] = nullptr;
    }
    readyThreads.clear();
    totalQuantums = 0;
    threadsNum = 0;

    unblock_signals();
}


/**
 * @param string
 * print Error MSG free all and exit
 */
void print_err(const std::string& string)
{
    block_signals();
    free_all();
    std::cerr<<string<<std::endl;
    exit(1);
}

/**
 * main Thread's Function
 */
void main_loop()
{
    while (true) { }
}

/**
 * Removes A ready Thread from the ready Threads List with the given id
 * @param tid
 */
void remove_from_ready(int tid)
{
    block_signals();
    for(auto it = readyThreads.begin(); it != readyThreads.end();it++)
    {
        if ((*it) == allThreads[tid])
        {
            readyThreads.erase(it);
            unblock_signals();
            return;
        }
    }
    unblock_signals();
}

/**
 * Switch to the next Thread in the Queue
 * @param termination
 * @param blocking
 */
void switchThreads(bool termination = false, bool blocking = false)
{
    block_signals();
    totalQuantums += 1;

    //Note: RunningThread is always in the front of the queue
    // if  running thread is terminated no need to save sate
    // the end of the queue
    if(!termination) {
        int retval = sigsetjmp(*(runningThread->get_env()), 1);
        if (retval != 0) {
            // if we run Thread that was Blocked because of mutix ask for it again
            if(runningThread->get_need_mutex())
            {
                if(uthread_mutex_lock() == 0)
                {
                    //release the need of Mutix if succeeds to unlock it
                    runningThread->release_mutex();
                }
            }

            unblock_signals();
            return;
        }
    }

    // if the state is being blocked or terminated we dont push it back to the ready Queue
    if (!termination && !blocking)
    {
        readyThreads.push_back(runningThread);
    }

    // Get the next Thread to run
    readyThreads.pop_front();
    runningThread = readyThreads.front();
    runningThread->inc_quantums();

    // reset The Timer
    timer.it_value.tv_sec = quantumusec/1000000;
    timer.it_value.tv_usec = quantumusec%1000000;
    if(setitimer(ITIMER_VIRTUAL, &timer, nullptr))
    {
        print_err(SET_TIMER_FAILED);
    }

    unblock_signals();
    siglongjmp(*(runningThread->get_env()), 1);

}

/**
 * Time Handler
 * @param sig
 */
void time_handler(int sig)
{
    switchThreads();
}

/**
 * initiate The Library
 * @param quantum_usecs
 * @return
 */
int uthread_init(int quantum_usecs)
{
    // check valid Input
    if(quantum_usecs <= 0)
    {
        fprintf(stderr, INIT_ERROR_MSG);
        return -1;
    }
    quantumusec = quantum_usecs;

    // Set Signal Handler
    sa.sa_handler = &time_handler;
    if (sigaction(SIGVTALRM,&sa,NULL)){
        print_err(SET_SIGACTION_FAIL_MSG);
    }

    // set The Main Thread
    auto* mainThread = new Thread(0, main_loop); //todo: mem leak
    totalQuantums = 1;
    mainThread->inc_quantums();
    readyThreads.push_back(mainThread);
    allThreads[0] = mainThread;
    threadsNum += 1;
    runningThread = mainThread;
    sigaddset(&set, SIGALRM);

    // set The Timer
    timer.it_value.tv_sec = quantum_usecs/1000000;
    timer.it_value.tv_usec = quantum_usecs%1000000;
    if(setitimer(ITIMER_VIRTUAL, &timer, NULL))
    {
        print_err(SET_TIMER_FAILED);
    }

    return 0;
}

int uthread_spawn(void (*f)(void))
{
    block_signals();

    // check Valid Inputs
    if(threadsNum >= MAX_THREAD_NUM)
    {
        fprintf(stderr, THREAD_EXCEEDED_MAX_NUM_MSG);
        unblock_signals();
        return -1;
    }

    // get Minimal Valid Id
    int id = get_min();

    // initialize The Thread
    try
    {
        auto* th = new Thread(id, f);
        readyThreads.push_back(th);
        allThreads[id] = th;
        ++threadsNum;

    } catch (std::bad_alloc&)
    {
        print_err(THREAD_ALLOCATION_FAIL_MSG);
    }

    unblock_signals();
    return id;
}

int uthread_terminate(int tid)
{
    block_signals();

    //Check Valid Inputs
    if(tid >= MAX_THREAD_NUM || allThreads[tid] == nullptr)
    {
        fprintf(stderr,THREAD_NOT_INITIALIZED_ERR_MSG);
        unblock_signals();
        return -1;
    }

    // free All and exit if terminate Main
    if(tid == 0)
    {
        free_all();
        exit(0);
    }

    // take Schedualing Decision if a Thread Terminates itself
    if(runningThread == allThreads[tid])
    {

        //Note: The Thread will be Removed From Ready Threads in Switch
        // free The Thread and switch Threads
        --threadsNum;
        allThreads[tid] = nullptr;
        delete runningThread;
        runningThread = nullptr;
        switchThreads(true, false);
    }

    // if Blocked it wont be in readyQueue
    if(!allThreads[tid]->get_is_blocked())
    {
        remove_from_ready(tid); //note this delete is based on all threads so dont delete before remove
    }

    // free The Thread and delete it
    delete allThreads[tid];
    allThreads[tid] = nullptr;
    --threadsNum;
    unblock_signals();
    return 0;
}

int uthread_block(int tid)
{
    block_signals();

    // check Valid Inputs
    if(tid >= MAX_THREAD_NUM || allThreads[tid] == nullptr || tid ==0)
    {
        fprintf(stderr, THREAD_BLOCK_ERROR_MSG);
        unblock_signals();
        return -1;
    }

    // if Already Blocked RETURN 0
    if(allThreads[tid]->get_is_blocked())
    {
        unblock_signals();
        return 0;
    }

    // if Running Thread is Being Blocked Switch Threads
    if(allThreads[tid] == runningThread)
    {
        runningThread->block_thread();
        switchThreads(false, true);
        unblock_signals();
        return 0;
    }
    //otherwise Block The Thread and Remove it from ready Queue
    allThreads[tid]->block_thread();
    remove_from_ready(tid);
    unblock_signals();
    return 0;
}

int uthread_resume(int tid)
{
    block_signals();

    //Check Valid Inputs
    if(tid >= MAX_THREAD_NUM || allThreads[tid]== nullptr)
    {
        fprintf(stderr,"thread library error:  hey \n");
        unblock_signals();
        return -1;
    }

    // if it was Bloacked push it to Ready Queue and unblock it
    if (allThreads[tid]->get_is_blocked())
    {
        allThreads[tid]->unblock_thread();
        readyThreads.push_back(allThreads[tid]);
    }

    unblock_signals();
    return 0;

}

int uthread_get_tid()
{
    return runningThread->get_id();
}

int uthread_get_total_quantums()
{
    return totalQuantums;
}

int uthread_get_quantums(int tid)
{
    block_signals();
    if(tid >= MAX_THREAD_NUM || allThreads[tid] == nullptr)
    {
        fprintf(stderr, THREAD_DOES_NOT_EXIST_MSG);
        unblock_signals();
        return -1;
    }
    unblock_signals();
    return allThreads[tid]->get_quantums();
}


int uthread_mutex_lock()
{
    block_signals();
    if(mutixIsAvailable)
    {
        mutixIsAvailable = false;
        mutixLocker = runningThread;
        unblock_signals();
        return 0;
    }

    if(mutixLocker == runningThread)
    {
        fprintf(stderr, MUTIX_ALREADY_LOCKED_MSG);
        unblock_signals();
        return -1;
    }

    //todo Check what should happen when tthread is locked from mutix then blocked by user using uthread_block
    blockedByMutixThreads.push_back(runningThread);
    runningThread->wait_mutix();
    uthread_block(runningThread->get_id());
    unblock_signals();
    return 0;
}

int uthread_mutex_unlock()
{
    block_signals();
    if(mutixIsAvailable)
    {
        fprintf(stderr, MUTIX_ALREADY_UNLOCKED_MSG);
        unblock_signals();
        return -1;
    }

    if(mutixLocker != runningThread)
    {
        unblock_signals();
        return -1;
    }

    mutixIsAvailable = true;
    mutixLocker = nullptr;
    while(!blockedByMutixThreads.empty())
    {
        Thread* th = blockedByMutixThreads.front();
        blockedByMutixThreads.pop_front();
        readyThreads.push_back(th);
        th->unblock_thread();
    }

    unblock_signals();
    return 0;
}
