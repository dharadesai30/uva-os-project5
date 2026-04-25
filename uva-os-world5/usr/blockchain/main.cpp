#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include "Blockchain.h"
#include "libc-os.h"

// TBD must need thse for cpp programs to work ... why? 
class Test {
public:
  Test()  { printf("%s,%d: Hello, Project-N!\n", __func__, __LINE__); }
  // Test()  { printf(""); }
// Test()  {} // wont work -- compiled away??

  ~Test() { printf("%s,%d: Goodbye, Project-N!\n", __func__, __LINE__); }
  // ~Test()  { printf(""); }
// ~Test() { } // wont work -- compiled away??
};
Test test;

extern "C" {
void __libc_init_array(void);       // newlib init.c; not useful besides calling _init()
void __libc_fini_array(void);       // ditto. 
extern void _fini();
void _init(void);
}


int sem_print = -1; 
// wont work. will print wrong values. why?? (b/c of its cpp?
void printf_r(const char *fmt,...) {
    // if (sem_print<0) return; 
    // sys_semp(sem_print); 
    va_list va;
    va_start(va, fmt);
    printf(fmt, va);
    va_end(va);    
    // sys_semv(sem_print); 
}

int main(int argc, char **args) {
    int difficulty = 5;
    enum {NBLOCKS=3};
    unsigned t00, t0; 
    char input_str[128]; // randomness added to hash search

    if (argc>1 && args != NULL && args[1][0] >= '0' && args[1][0] <= '9') {
        difficulty = args[1][0] - '0';
    }
    printf("Using difficulty = %d\n", difficulty);
    sem_print = sys_semcreate(1); assert(sem_print>=0); 

    t00 = uptime_ms(); 
    for (int i=0; i<NBLOCKS;i++) {
        t0 = uptime_ms(); 
        Blockchain bChain(difficulty);
        snprintf(input_str, 128, "Block %d data", i+1);         
        bChain.AddBlock(i+1, input_str);
        printf("Done mining block %d...%d ms\n", i+1, uptime_ms() - t0);
    }

    unsigned total=uptime_ms()-t00;
    printf("total %d ms, avg %d ms/block\n", total, total/NBLOCKS);

    sys_semfree(sem_print);
    return 0;
}