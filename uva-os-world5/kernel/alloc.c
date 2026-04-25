// #define K2_DEBUG_VERBOSE
// #define K2_DEBUG_INFO
#define K2_DEBUG_WARN

#include "plat.h"
#include "utils.h"
#include "mmu.h"
#include "spinlock.h"


/* Phys memory layout:	cf paging_init() below. 

	Upon init, the Rpi3 hw (GPU) will allocate framebuffer at certain addr. We
	need to know that addr in order to decide paging memory usable to the
	kernel. However, we won't know that addr until we initialize the GPU (cf
	mbox.c). Based on my observation, GPU always allocates the framebuffer at
	fixed locations (below) near the end of phys mem. 

	Based on the assumption, we exclude GUESS_FB_BASE_PA--HIGH_MEMORY from the
	paging memory. (Hope we don't waste too much phys mem)

	If later the GPU init code (mbox.c) finds these assumption is wrong, 
	mem reservation will fail and kernel will panic. 
*/
// qemu always set to 0x3c100000 (a safe choice, I suppose?); the rpi3 hw will
// change based on the requested fb size, e.g. 0x3e8fa000 for 1024x768;
// 0x3e7fe000 for 1360x768... so pick "one size fits all"
#define GUESS_FB_BASE_PA 0x3c100000
#define HIGH_MEMORY0 GUESS_FB_BASE_PA // the actual "highmem"
// #define HIGH_MEMORY0 HIGH_MEMORY

/* below: flags covering from LOW_MEMORY to HIGH_MEMORY, including: 
(1) region for page allocator 
(2) the possible framebuffer region (to be reserved).
(3) the malloc region (to be reserved).
one byte for a page. 1=allocated */
static unsigned char mem_map [ MAX_PAGING_PAGES ] = {0,}; 
unsigned paging_pages_used = 0, paging_pages_total = 0;

/*  Minimalist page allocation 
	all alloc/free funcs below are locked (SMP safe) */
struct spinlock alloc_lock = {.locked=0, .cpu=0, .name="alloc_lock"}; 

static unsigned long LOW_MEMORY = 0; 	// pa
static unsigned long PAGING_PAGES = 0; 
extern char kernel_end; // linker.ld

//  define 0 to disable malloc()
#undef MEM_PAGE_ALLOC

/* allocate a page (zero filled). return kernel va. */
void *kalloc() {
	unsigned long page = get_free_page();
	if (page == 0) return 0;
	return PA2VA(page);
}

/* free p which is kernel va */
void kfree(void *p) {
	free_page(VA2PA(p));
}

/* allocate a page (zero filled). return pa of the page. 0 if failed */
unsigned long get_free_page() {
	acquire(&alloc_lock);
	for (int i = 0; i < PAGING_PAGES-MALLOC_PAGES; i++){
		if (mem_map[i] == 0){
			mem_map[i] = 1; paging_pages_used++;
			release(&alloc_lock);
			unsigned long page = LOW_MEMORY + i*PAGE_SIZE;
			memzero_aligned(PA2VA(page), PAGE_SIZE);
			return page;
		}
	}
	release(&alloc_lock);
	return 0;
}

/* free a page. @p is pa of the page. */
void free_page(unsigned long p){
	acquire(&alloc_lock);
	mem_map[(p - LOW_MEMORY)>>PAGE_SHIFT] = 0; paging_pages_used--;
	release(&alloc_lock);
}

/* reserve a phys region. all pages must be unused previously. 
	caller MUST hold alloc_lock
	is_reserve: 1 for reserve, 0 for free
	return 0 if OK  */
static int _reserve_phys_region(unsigned long pa_start, 
	unsigned long size, int is_reserve) {
	if ((pa_start & ~PAGE_MASK) != 0 || (size & ~PAGE_MASK) != 0) // must align
		{W("pa_start %lx size %lx", pa_start, size);BUG(); return -1;}

	for (unsigned i = ((pa_start-LOW_MEMORY)>>PAGE_SHIFT); 
			i<((pa_start-LOW_MEMORY+size)>>PAGE_SHIFT); i++){
		if (mem_map[i] == is_reserve)	
			{return -2;}      // page already reserved / freed? 
	}	
	for (unsigned i = ((pa_start-LOW_MEMORY)>>PAGE_SHIFT); 
		i<((pa_start-LOW_MEMORY+size)>>PAGE_SHIFT); i++){
		mem_map[i] = is_reserve; 
	}
	if (is_reserve) paging_pages_used += (size>>PAGE_SHIFT); 
		else paging_pages_used -= (size>>PAGE_SHIFT);

	I("%s: %s. pa_start %lx -- %lx size %lx",
		 __func__, is_reserve?"reserved":"freed", 
		 pa_start, pa_start+size, size);
	return 0; 
}

/* same as above. but caller MUST NOT hold alloc_lock */
int reserve_phys_region(unsigned long pa_start, unsigned long size) {
	int ret; 
	acquire(&alloc_lock); 
	ret = _reserve_phys_region(pa_start, size, 1/*reserve*/);
	release(&alloc_lock); 
	return ret; 
}

/* same as above. but caller MUST NOT hold alloc_lock */
int free_phys_region(unsigned long pa_start, unsigned long size) {
	int ret; 
	acquire(&alloc_lock); 
	ret = _reserve_phys_region(pa_start, size, 0/*free*/);
	release(&alloc_lock); 
	return ret; 
}

/* init kernel's memory mgmt 
	return: # of paging pages */
static void alloc_init (unsigned char *ulBase, unsigned long ulSize); 

unsigned int paging_init() {
	LOW_MEMORY = VA2PA(PGROUNDUP((unsigned long)&kernel_end));	
	PAGING_PAGES = (HIGH_MEMORY0 - LOW_MEMORY) / PAGE_SIZE; // comment above
	
    BUG_ON(2 * MALLOC_PAGES >= PAGING_PAGES); // too many malloc pages 

    /* reserve a virtually contig region for malloc()  */
    if (MALLOC_PAGES) {
        acquire(&alloc_lock); 
		int ret = _reserve_phys_region(HIGH_MEMORY0-MALLOC_PAGES*PAGE_SIZE, 
			MALLOC_PAGES*PAGE_SIZE, 1); BUG_ON(ret); 
        alloc_init(PA2VA(HIGH_MEMORY0-MALLOC_PAGES*PAGE_SIZE), 
			MALLOC_PAGES*PAGE_SIZE); 		
        release(&alloc_lock);
    } 

	printf("phys mem: %08x -- %08x\n", PHYS_BASE, PHYS_BASE + PHYS_SIZE);
	printf("\t kernel: %08x -- %08lx\n", KERNEL_START, VA2PA(&kernel_end));
	printf("\t paging mem: %08lx -- %08x\n", LOW_MEMORY, HIGH_MEMORY0-(MALLOC_PAGES<<PAGE_SHIFT));
	printf("\t\t %lu%s %ld pages\n", 
		int_val((HIGH_MEMORY0 - LOW_MEMORY)),
		int_postfix((HIGH_MEMORY0 - LOW_MEMORY)),
		PAGING_PAGES);
    printf("\t malloc mem: %08x -- %08x\n", HIGH_MEMORY0-(MALLOC_PAGES<<PAGE_SHIFT), HIGH_MEMORY0);
	printf("\t\t %lu%s\n", int_val(MALLOC_PAGES * PAGE_SIZE),
                                 int_postfix(MALLOC_PAGES * PAGE_SIZE)); 
	printf("\t reserved for framebuffer: %08x -- %08x\n", 
		HIGH_MEMORY0, HIGH_MEMORY);

	paging_pages_total = ((HIGH_MEMORY0-LOW_MEMORY)>>PAGE_SHIFT) - MALLOC_PAGES; 

	return PAGING_PAGES; 
}

/* 
	Simple malloc/free support. Main idea: 

	the allocator is given a contig virtual region (its pool) at boot time. it
	has a set of N fixed block sizes to allocate; corresponding to these sizes,
	it maintains N freelists (called buckets), which are initally empty. 

	after boot: as malloc() is invoked, the allocator takes a block of memory
	from the start of its memory pool; as free() is invoked, the allocator
	returns the block to one of the freelist. Therefore, for future malloc(),
	the allocator will try to look at the freelists first, and only chop off
	from the pool if no blocks are available on the freelists. 

	malloc() fails if: the requested size is too large, larger than any of the
	fixed block size; or no blocks are on the freelists && the pool runs out

	NB: it's a simple design: it does NOT split or merge blocks. there will be
	mem waste due to internal fragmentation. 

	Used by the usb driver & surface flinger (sf.c)
	Originally from: USPi - An USB driver for Raspberry Pi written in C
	alloc.c   cf LICENSE 
*/

#define MEM_DEBUG	1	// keep malloc statics (per blocksize)

/* the code below implements a page allocator, which is compiled away now.
 if we replace our simple page alloc in the future, we may turn it back on */
// #define MEM_PAGE_ALLOC 1   // disable page allocator code below

#define BLOCK_ALIGN	16
#define ALIGN_MASK	(BLOCK_ALIGN-1)

/* the data structure that prefixes the allocated data (i.e. `block'). 
	that is, TBlockHeader is not part of the allocated data */
typedef struct TBlockHeader
{
	unsigned int	nMagic		__attribute__ ((packed));		//4
#define BLOCK_MAGIC	0x424C4D43	
	unsigned int	nSize		__attribute__ ((packed));		//4
	struct TBlockHeader *pNext	__attribute__ ((packed));		//8 for aarch64
	// unsigned int	nPadding	__attribute__ ((packed));	
	unsigned char	Data[0];	// actual data follows, shall align to BLOCK_ALIGN
} TBlockHeader;			

/* "bucket" -- a freelist of blocks. these blocks all of the same size.
the allocator have many freelists like this  */
typedef struct TBlockBucket {
	unsigned int	nSize; // size of all the blocks on this list 
#ifdef MEM_DEBUG
	unsigned int	nCount;	// # of blocks already allocated (i.e. off the freelist)
	unsigned int	nMaxCount; // # of blocks ever allocated
#endif
	TBlockHeader	*pFreeList;
} TBlockBucket;

#ifdef MEM_PAGE_ALLOC
typedef struct TFreePage {
	unsigned int	nMagic;
#define FREEPAGE_MAGIC	0x50474D43
	struct TFreePage *pNext;
} TFreePage;

typedef struct TPageBucket {
#ifdef MEM_DEBUG
	unsigned int	nCount;
	unsigned int	nMaxCount;
#endif
	TFreePage	*pFreeList;
} TPageBucket;
#endif

/* THE global memory pool, initialized during boot (cf alloc_init()). 
  s_pNextBlock is where the next block is chopped off from the pool */
static unsigned char *s_pNextBlock = 0;	 // base. BLOCK_ALIGN aligned
static unsigned char *s_pBlockLimit = 0; // end of it

#ifdef MEM_PAGE_ALLOC
static unsigned char *s_pNextPage;
static unsigned char *s_pPageLimit;
#endif

// mutiple freelists, each holding free blocks of a specific size
static TBlockBucket s_BlockBucket[] = {{0x40}, {0x400}, {0x1000}, {0x4000}, 
							{0x40000}, {0x80000}, {0x100000}, {0}};
// the largest bucket. malloc() exceeding this size will fail
// 0x80000 - 512KB; 0x100000 - 1MB. Doom's fb is ~1000KB (640x400x4)
#define MAX_ALLOC_SIZE 0x100000

#ifdef MEM_PAGE_ALLOC
static TPageBucket s_PageBucket;		// a freelist of pages...
#endif

/* init data structures for kernel malloc()
	ulBase, ulSize: virt region to be managed by malloc. (kernel va)
    must be page aligned? */
static void alloc_init (unsigned char *ulBase, unsigned long ulSize) {
#if 0       // fxl: not needed
	if (ulBase < MEM_HEAP_START) {
		ulBase = MEM_HEAP_START;
	}
	ulSize = ulLimit - ulBase;
#endif

#ifdef MEM_PAGE_ALLOC
    unsigned char *ulLimit = ulBase + ulSize;
	unsigned long ulQuarterSize = ulSize / 4;
	// fxl: 1/4 mem goes under malloc, otehrs goes under page alloc. hardcoded
	s_pNextBlock = (unsigned char *) ulBase;
	s_pBlockLimit = (unsigned char *) (ulBase + ulQuarterSize);

	s_pNextPage = (unsigned char *) ((ulBase + ulQuarterSize + PAGE_SIZE) & ~PAGE_MASK);
	s_pPageLimit = (unsigned char *) ulLimit;
#else
	s_pNextBlock = (unsigned char *) ulBase;
	s_pBlockLimit = (unsigned char *) (ulBase + ulSize);
#endif
}

/* return total mem (both under malloc and page alloc) */
unsigned long mem_get_size (void) {
#ifdef MEM_PAGE_ALLOC
	return (unsigned long) (s_pBlockLimit - s_pNextBlock) + (s_pPageLimit - s_pNextPage);
#else
	return (unsigned long) (s_pBlockLimit - s_pNextBlock);
#endif
}

void *malloc (unsigned ulSize) {
	assert (s_pNextBlock != 0);
	if (ulSize > MAX_ALLOC_SIZE) 
		return 0; 
		
	acquire(&alloc_lock);

	// find the freelist with the smallest block size that can serve malloc()
	TBlockBucket *pBucket; 
	for (pBucket = s_BlockBucket; pBucket->nSize > 0; pBucket++) {
		if (ulSize <= pBucket->nSize) {
			ulSize = pBucket->nSize; // round up the alloc size
			break;
		}
	}

	TBlockHeader *pBlockHeader; // the free block to return
	if (   pBucket->nSize > 0
	    && (pBlockHeader = pBucket->pFreeList) != 0) { // found a block, spin it off
		assert (pBlockHeader->nMagic == BLOCK_MAGIC);
		pBucket->pFreeList = pBlockHeader->pNext;
	} else { 
		/* the apropriate freelist has no free block. chop one off from global pool (s_pNextBlock).
			actual size == the requested alloc size + block header + padding */
        BUG_ON(!pBucket->nSize);// ulSize exceeds blk size of last freelist. Cannot happen as we checked alloc size at the start of this func*/
		pBlockHeader = (TBlockHeader *) s_pNextBlock;
		s_pNextBlock += (sizeof (TBlockHeader) + ulSize + BLOCK_ALIGN-1) & ~ALIGN_MASK; // round up (so there's some leak?)
		if (s_pNextBlock > s_pBlockLimit) { // run out of reserved region for malloc()
            release(&alloc_lock); BUG(); // XXX: reverse s_pNextBlock? 
			return 0;
		}
		pBlockHeader->nMagic = BLOCK_MAGIC;
		pBlockHeader->nSize = (unsigned) ulSize;	
	}

#ifdef MEM_DEBUG
    if (++pBucket->nCount > pBucket->nMaxCount)
        pBucket->nMaxCount = pBucket->nCount;
#endif    
    release(&alloc_lock);

	pBlockHeader->pNext = 0; // the newly block is allocated, i.e. off any freelist

	void *pResult = pBlockHeader->Data;
	assert (((unsigned long) pResult & ALIGN_MASK) == 0);

	return pResult;
}

/* return the mem block to one of the freelists */
void free (void *pBlock) {
	assert (pBlock != 0);
	TBlockHeader *pBlockHeader = (TBlockHeader *) ((unsigned long) pBlock - sizeof (TBlockHeader));
	assert (pBlockHeader->nMagic == BLOCK_MAGIC);
    BUG_ON(pBlockHeader->pNext != 0); // as of now, the block should be off any freelist

    int freed = 0; 

	for (TBlockBucket *pBucket = s_BlockBucket; pBucket->nSize > 0; pBucket++) {
		if (pBlockHeader->nSize == pBucket->nSize) { // found the list
            acquire(&alloc_lock);
			pBlockHeader->pNext = pBucket->pFreeList;
			pBucket->pFreeList = pBlockHeader;
#ifdef MEM_DEBUG
			pBucket->nCount--;
#endif
        	release(&alloc_lock);
            freed = 1; 
			break;
		}
	}
    // not be able to find a freelist to return the given block, why?
    // W("pBlockHeader->nSize %u 0x%x", pBlockHeader->nSize, pBlockHeader->nSize); 
	BUG_ON(!freed);
}

#ifdef MEM_PAGE_ALLOC
void *palloc (void)
{
	assert (s_pNextPage != 0);

	EnterCritical ();

#ifdef MEM_DEBUG
	if (++s_PageBucket.nCount > s_PageBucket.nMaxCount)
	{
		s_PageBucket.nMaxCount = s_PageBucket.nCount;
	}
#endif

	TFreePage *pFreePage;
	if ((pFreePage = s_PageBucket.pFreeList) != 0)
	{
		assert (pFreePage->nMagic == FREEPAGE_MAGIC);
		s_PageBucket.pFreeList = pFreePage->pNext;
		pFreePage->nMagic = 0;
	}
	else
	{
		pFreePage = (TFreePage *) s_pNextPage;
		
		s_pNextPage += PAGE_SIZE;

		if (s_pNextPage > s_pPageLimit)
		{
			LeaveCritical ();

			return 0;		// TODO: system should panic here
		}
	}

	LeaveCritical ();
	
	return pFreePage;
}

void pfree (void *pPage) {
	assert (pPage != 0);
	TFreePage *pFreePage = (TFreePage *) pPage;
	
	EnterCritical ();

	pFreePage->nMagic = FREEPAGE_MAGIC;

	pFreePage->pNext = s_PageBucket.pFreeList;
	s_PageBucket.pFreeList = pFreePage;

#ifdef MEM_DEBUG
	s_PageBucket.nCount--;
#endif

	LeaveCritical ();
}
#endif

#ifdef MEM_DEBUG
void dump_mem_info (void) {
	unsigned total=0; 
	for (TBlockBucket *pBucket = s_BlockBucket; pBucket->nSize > 0; pBucket++) {
		I("alloc: malloc(%u): outstanding %u blocks (max %u)",
			     pBucket->nSize, pBucket->nCount, pBucket->nMaxCount);
		total+=(pBucket->nSize)*pBucket->nCount; 
	}
	I("alloc: TOTAL allocated %lu %s (%d/100)", 
		int_val(total), int_postfix(total), total*100/(MALLOC_PAGES * PAGE_SIZE));

#ifdef MEM_PAGE_ALLOC
	LoggerWrite (LoggerGet (), "alloc", LogDebug, "palloc: %u pages (max %u)",
		     s_PageBucket.nCount, s_PageBucket.nMaxCount);
#endif
}
#endif

