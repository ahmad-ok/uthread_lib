#include "uthreads.h"
#include <signal.h>
#include <setjmp.h>
#include <sys/time.h>
#include <queue>
using std::queue;

typedef unsigned long address_t;
typedef void (*func)();


#define JB_SP 6
#define JB_PC 7

address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
                 "rol    $0x9,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

class Thread
{
private:
    sigjmp_buf env;
    unsigned int id;
public:
    Thread(unsigned int id, func f)
    {
        char[STACK_SIZE] stack;
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


