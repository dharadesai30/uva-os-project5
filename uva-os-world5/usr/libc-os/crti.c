// for c++ apps
// https://wiki.osdev.org/Calling_Global_Constructors#GNU_Compiler_Collection_-_System_V_ABI

/*
	the whole crt0/crti/crtn business is to make sure ctro/dtro from cpp *.o
	are invoked. 
	- crt0 will call _init() and _fini()
	- crti defines _init() and _fini(), which iterate through two arrays of function pointers
	   these two arrays are from the .init_array and .fini_array sections, respectively
	   these two arrays contain all ctors and dtors, per the ARM BPABI (bare platform ABI) and the linker
	- crti further defines symbols (_init_array_start/_fini_array_start) marking the starts of the two arrays; 
	- crtn defines symbols marking the ends of the two arrays
	- how to make sure these start/end markers are placed at the beginning of .init_array/.fini_array?
		1. by linker script (cf usercpp.ld, has its own caveats)
		2. by placing crti.o and crtn.o at the beginning and end of the object files to be linked
		method 2 is used here

	fxl, Jan 2025
*/

/* crti.c for ARM - bare platform ABI (BPABI) - use -std=c99 */
typedef void (*func_ptr)(void);
 
extern func_ptr _init_array_start[0], _init_array_end[0];
extern func_ptr _fini_array_start[0], _fini_array_end[0];
 
void _init(void)
{
	for ( func_ptr* func = _init_array_start; func != _init_array_end; func++ )
		(*func)();
}
 
void _fini(void)
{
	for ( func_ptr* func = _fini_array_start; func != _fini_array_end; func++ )
		(*func)();
}
 
func_ptr _init_array_start[0] __attribute__ ((used, section(".init_array"), aligned(sizeof(func_ptr)))) = { };
func_ptr _fini_array_start[0] __attribute__ ((used, section(".fini_array"), aligned(sizeof(func_ptr)))) = { };