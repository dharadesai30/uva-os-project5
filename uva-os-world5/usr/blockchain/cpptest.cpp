// https://github.com/NJU-ProjectN/navy-apps/blob/master/tests/cpp-test/main.cpp

/*
/// WITHOUT -fno-use-cxa-atexit  ... 
Ctor.... (called via dso ()
>>> bt
#0  Test::Test (this=0xe368 <test>) at cpptest.cpp:9
#1  0x00000000000010a8 in __static_initialization_and_destruction_0 (__initialize_p=1, __priority=65535) at cpptest.cpp:13
#2  0x00000000000010e4 in _GLOBAL__sub_I___dso_handle () at cpptest.cpp:26
#3  0x000000000000af7c in _init ()
#4  0x0000000000003d58 in __libc_init_array () at ../newlib/libc/misc/init.c:40
#5  0x000000000000104c in main () at cpptest.cpp:21

Dtor....(NOT called via _fini; will crash if call fini
>>> bt
#0  Test::~Test (this=0xe368 <test>, __in_chrg=<optimized out>) at cpptest.cpp:10
#1  0x0000000000004590 in __call_exitprocs (code=code@entry=0, d=d@entry=0x0) at ../newlib/libc/stdlib/__call_atexit.c:123
#2  0x0000000000001458 in exit (code=code@entry=0) at ../newlib/libc/stdlib/exit.c:60
#3  0x0000000000001160 in _main () at syscall.c:44

/// with -fno-use-cxa-atexit
Ctor (called via _init)
Dtor (called via _fini)
>>> bt
#0  Test::~Test (this=0xe300 <test>, __in_chrg=<optimized out>) at cpptest.cpp:30
#1  0x00000000000010d4 in __static_initialization_and_destruction_0 (__initialize_p=0, __priority=65535) at cpptest.cpp:33
#2  0x0000000000001110 in _GLOBAL__sub_D_test () at cpptest.cpp:47
#3  0x000000000000ae74 in _fini ()
#4  0x0000000000001068 in main () at cpptest.cpp:45

*/
#include <stdio.h>

// void * __dso_handle; 

class Test {
public:
  Test()  { printf("%s,%d: Hello, Project-N!\n", __func__, __LINE__); }
  ~Test() { printf("%s,%d: Goodbye, Project-N!\n", __func__, __LINE__); }
};
Test test;

////////////////////////////////////////////////////

extern "C" {
void __libc_init_array(void);       // newlib init.c; not useful besides calling _init()
void __libc_fini_array(void);       // ditto. 
extern void _fini();
void _init(void);
}

int main(int argc, char **argv) {
    // __libc_init_array(); // see above
    // _init();        // sufficient, will call ctor (&register dtor). if not called, dtor wont be called.
    printf("argc %d\n", argc); 
    printf("%s,%d: Hello world!!\n", __func__, __LINE__);
    // _fini();     /// will cause problems
  return 0;
}

