/* linker script for "virt" emulated by qemu */

SECTIONS
{
	. = 0xffff000040080000;
	.text.boot : { *(.text.boot) }
	. = ALIGN(0x00001000);
	user_begin = .;
	.text.user : { build/user* (.text) }
	.rodata.user : { build/user* (.rodata) }
	.data.user : { build/user* (.data) }
	.bss.user : { build/user* (.bss) }
	user_end = .;
	. = ALIGN(512);		/*sector aligned*/
	ramdisk_start = .;
	.ramdisk : {build/sd* (.data)}		/* must come before generic *(.data) */
	. = ALIGN(512);		/*sector aligned*/
	ramdisk_end = .;	
	.text :  { *(.text) }
	.rodata : { *(.rodata) }
	.data : { *(.data) }
	. = ALIGN(0x8);
	bss_begin = .;
	.bss : { *(.bss*) } 
	bss_end = .;
	. = ALIGN(0x00001000);
	idmap_dir = .;
	.data.idmapd : {. += (3* (1 << 12));}
	. = ALIGN(0x00001000);
	pg_dir = .;
	.data.pgd : { . += (4 * (1 << 12)); }	/* PG_DIR_SIZE */
	kernel_stack_start = .;
	.data.pgd : { . += (4 * (1 << 12)); }	/* kernel stack, 4 pgs*/
	kernel_stack_end = .;
	kernel_end = .; 
}

