// crt0 (c version); no invocations to _init _fini
// for c++, cf crt0.c

// b/c c/cpp apps need different crt0 (cf their sources), crt0 shall separate from 
// from libc-os which both c/cpp need to link to

extern int main();
int exit(int) __attribute__((noreturn));

void _main() {
    main();
    exit(0); // goes to libc
}