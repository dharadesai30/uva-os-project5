// ctr0 for c++ apps, for which _init() _fini() must be called
// https://wiki.osdev.org/Calling_Global_Constructors#GNU_Compiler_Collection_-_System_V_ABI
// c program shall not use this _main(). cf crt0.c 
extern int main();
int exit(int) __attribute__((noreturn));
extern void _init(void); 
extern void _fini(void);

void _main(int argc, const char **argv) {
    _init(); // crti.c
    main(argc, argv);
    _fini(); // crti.c
    exit(0); // goes to libc
}