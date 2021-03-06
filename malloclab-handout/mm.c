/* Optimization Segregated list + Best fit + Address order has 87/100 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "duile",
    /* First member's full name */
    "Duile",
    /* First member's email address */
    "https://www.cnblogs.com/duile",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8
/* Basic constants and macros */
#define WSIZE		4		/* Word and header/footer size (bytes) */ 
#define DSIZE		8		/* Double word size (bytes) */
#define CHUNKSIZE	(1<<12)	/* Extend heap by this amount (bytes) */

/* use for mm_malloc to ensure memory of MAX */
#define MAX(x, y)	((x) > (y)) ? (x) : (y) 

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))            
#define PUT(p, val)  (*(unsigned int *)(p) = (val))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)	((size) | (alloc))

/* Read the size and allocated fields from address p*/
#define GET_SIZE(p)		(GET(p) & ~0x7)
#define GET_ALLOC(p)	(GET(p) & 0x1)	

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)       ((char *)(bp) - WSIZE)                      
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) 

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) 
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) 

/* Given block ptr bp, compute value of its succ and pred */
#define GET_SUCC(bp) 		(*(unsigned int *)(bp))
#define GET_PRED(bp) 		(*((unsigned int *)(bp) + 1))

/* Put a value at address of succ and pred */
#define PUT_SUCC(bp, val) 	(GET_SUCC(bp) = (unsigned int)(val))
#define PUT_PRED(bp, val) 	(GET_PRED(bp) = (unsigned int)(val))


/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) 	((DSIZE) * (((size) + (DSIZE) + (DSIZE-1)) / (DSIZE)))

/* Global variables */
static char* heap_listp = 0;	/* Pointer to first block */
static char* block_list_start  = 0;	/* Pointer to first free block of different size */	

/* Function prototypes for internal helper routines */
static void *extend_heap(size_t words);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);

static void insert(void *new_first);
static void remove_s_p(void *bp);
void* get_sfreeh(size_t size);

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    if ((heap_listp = mem_sbrk(12*WSIZE)) == (void*)-1)
        return -1;
    PUT(heap_listp, 0);                         /* block size <= 32   */ 
    PUT(heap_listp+(1*WSIZE), 0);               /* block size <= 64 */
    PUT(heap_listp+(2*WSIZE), 0);               /* block size <= 128 */
    PUT(heap_listp+(3*WSIZE), 0);               /* block size <= 256 */
    PUT(heap_listp+(4*WSIZE), 0);               /* block size <= 512 */
    PUT(heap_listp+(5*WSIZE), 0);               /* block size <= 1024 */
    PUT(heap_listp+(6*WSIZE), 0);               /* block size <= 2048 */
    PUT(heap_listp+(7*WSIZE), 0);               /* block size <= 4096 */
    PUT(heap_listp+(8*WSIZE), 0);               /* block size > 4096 */
    PUT(heap_listp+(9*WSIZE), PACK(DSIZE, 1));  /* Prologue header */ 
    PUT(heap_listp+(10*WSIZE), PACK(DSIZE, 1)); /* Prologue footer */ 
    PUT(heap_listp+(11*WSIZE), PACK(0, 1));     /* Epilogue header */
    
    block_list_start = heap_listp;              /* pointer to block size <= 32   */ 
    heap_listp += (10 * WSIZE);                 /* pointer to Prologue footer */

    if (extend_heap(2 * DSIZE/WSIZE) == NULL)   /* First Extend: Only require the 16 bytes */
        return -1;
    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;      /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *bp;      
	
	/* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE)                                          
        asize = 2*DSIZE;                                        
    else
        asize = ALIGN(size); 

    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {  
        place(bp, asize);                  
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, CHUNKSIZE);                 
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)  
        return NULL;                                  
    place(bp, asize);                                 
    return bp;	
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{	
    size_t size = GET_SIZE(HDRP(ptr));
	
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    PUT_SUCC(ptr, 0);
    PUT_PRED(ptr, 0);

    coalesce(ptr);
}

/*
 * coalesce - Boundary tag coalescing. Return ptr to coalesced block
 * 			  we must remove succ or pred of next or prev block 
 */
static void *coalesce(void *bp) 
{
    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {            /* alloc-> bp ->alloc */
		/* then we will insert(bp) */
    }

    else if (prev_alloc && !next_alloc) {      /* alloc-> bp ->free */
        remove_s_p(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size,0));
    }

    else if (!prev_alloc && next_alloc) {      /* free-> bp ->alloc */
        remove_s_p(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);					   /* prev block as first free block */
    }

    else {                                     /* free-> bp ->free */
        remove_s_p(NEXT_BLKP(bp));
        remove_s_p(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + 
            GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    
    insert(bp);
    
    return bp;
}

/*
 * remove_s_p - remove the block from free list
                by changing pointer of succ and pred.
 */
static void remove_s_p(void *bp)
{
    void *root = get_sfreeh(GET_SIZE(HDRP(bp))); 
    void* pred = GET_PRED(bp);
    void* succ = GET_SUCC(bp);

    PUT_PRED(bp, NULL);
    PUT_SUCC(bp, NULL);

    if (pred == NULL)
    {	
    	/* NULL-> bp ->NULL */
    	PUT_SUCC(root, succ);
    	
    	/* NULL-> bp ->FREE_BLK */
        if (succ != NULL) PUT_PRED(succ, NULL);
        
    }
    else
    {	
        /* FREE_BLK-> bp ->NULL */
    	PUT_SUCC(pred, succ);
    	
    	/* FREE_BLK-> bp ->FREE_BLK */
        if (succ != NULL) PUT_PRED(succ, pred);
    }
}

/*
 * insert - NOT FILO use from small to big
 */
static void insert(void* bp)
{
    if (bp == NULL)
        return;
    void* root = get_sfreeh(GET_SIZE(HDRP(bp)));
    void* pred = root;
    void* succ = GET(root);

    /* size form small to big to aviod always use big free block */
    while (succ != NULL)
    {
        if (GET_SIZE(HDRP(succ)) >= GET_SIZE(HDRP(bp))) break;
        pred = succ;
        succ = GET_SUCC(succ);
    }

    /* Luckly! the first succ block bigger than bp block */
    /* So bp is root and pred of bp is NULL*/
    if (pred == root)
    {   
        /* 1. root -> insert, First insert*/
        PUT(root, bp);
        PUT_PRED(bp, NULL);
        PUT_SUCC(bp, succ);

        /* 2. root -> insert -> xxx, First insert and then having free block*/
        /* if succ != NULL, bp is pred of succ */
        if (succ != NULL) PUT_PRED(succ, bp);
    }

    /* Unluckly! the first succ block smaller than bp block */
    /* So bp is NOT root and pred of bp EXISTS*/
    else
    {   
        /* 3. root -> xxx -> insert, Last insert*/
        PUT_PRED(bp, pred);
        PUT_SUCC(bp, succ);
        PUT_SUCC(pred, bp);

        /* 4. xxx-> insert -> xxx , Middle insert*/
        /* if succ != NULL, bp is pred of succ*/
        if (succ != NULL) PUT_PRED(succ, bp);
    }
}

/* 
 * place - Place block of asize bytes at start of free block bp 
 *         and split if remainder would be at least minimum block size
 */
static void place(void *bp, size_t asize)
{
    size_t bsize = GET_SIZE(HDRP(bp));   
    
    /* 'bp' will be an allocted block, so we remove 'bp' from free list*/
    remove_s_p(bp);

    if ((bsize - asize) >= (2*DSIZE)) { 
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        
        /* bp: Remainder of block */
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(bsize-asize, 0));
        PUT(FTRP(bp), PACK(bsize-asize, 0));
        PUT_SUCC(bp, 0);
        PUT_PRED(bp, 0);
        coalesce(bp);
    }
    else {
        PUT(HDRP(bp), PACK(bsize, 1));
        PUT(FTRP(bp), PACK(bsize, 1));
    }
}

/* 
 * find_fit - Find a fit for a block with asize bytes 
 */
static void* find_fit(size_t asize)
{
    for (void* root = get_sfreeh(asize); root != (heap_listp-WSIZE); root += WSIZE){
        void* bp = GET(root);
        while (bp){
            if (GET_SIZE(HDRP(bp)) >= asize) return bp;
            bp = GET_SUCC(bp);
        }
    }
    return NULL;
}


/* 
 * find_num - find the head block of seglist
 */
void* get_sfreeh(size_t size)
{	
	int i = 0;
	
	/* i > 4096 */
	if(size >= 4096)
		return block_list_start + (8 * WSIZE);
	
	/* i <= 32 */
	size = size >> 5;
	
	/* other */
	while (size){
		size = size >> 1;
		i++;
	}
	
    return block_list_start + (i * WSIZE);
}

/* 
 * extend_heap - Extend heap with free block and return its block pointer
 */
static void *extend_heap(size_t words) 
{
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE; 
    if ((long)(bp = mem_sbrk(size)) == -1)  
        return NULL;                                        
	
    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));         /* Free block header */   
    PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */                                    
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */
	    
	/* Initialize explicit free list ptr: sfree_lists, succ and pred */
	PUT_SUCC(bp, 0);			/* Free block succ */          
	PUT_PRED(bp, 0);	  		/* Free block pred */
			
	return coalesce(bp);
}

/*
 * mm_realloc - Naive implementation of realloc
 */
void *mm_realloc(void *ptr, size_t newsize)
{
    size_t oldsize;
    void *newptr;

    /* If size == 0 then this is just free, and we return NULL. */
    if(newsize == 0) {
        mm_free(ptr);
        return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if(ptr == NULL) {
        return mm_malloc(newsize);
    }
	
	/* begain change the size of block that be pointered by ptr */
	/* mm_malloc() fails equals realloc() fails */
    newptr = mm_malloc(newsize);

    /* If realloc() fails the original block is left untouched  */
    if(!newptr) {
        return 0;
    }

    /* Copy the old data. */
    /* 
    Only copy the data, 
    if old data bigger than new data, 
    size of oldblock change to equal newblock, 
    but old data equal or smaller than new data, 
    size of oldblock not change 
    */
    oldsize = GET_SIZE(HDRP(ptr));
    if(newsize < oldsize) 
    	oldsize = newsize;
    memcpy(newptr, ptr, oldsize);

    /* Free the old block. */
    mm_free(ptr);

    return newptr;
}







