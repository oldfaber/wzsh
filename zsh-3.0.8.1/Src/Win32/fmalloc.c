/*
 * tc.alloc.c (Caltech) 2/21/82
 * Chris Kingsley, kingsley@cit-20.
 *
 * This is a very fast storage allocator.  It allocates blocks of a small
 * number of different sizes, and keeps free lists of each size.  Blocks that
 * don't exactly fit are passed up to the next larger size.  In this
 * implementation, the available sizes are 2^n-4 (or 2^n-12) bytes long.
 * This is designed for use in a program that uses vast quantities of memory,
 * but bombs when it runs out.
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * from
 * $Header: /p/tcsh/cvsroot/tcsh/tc.alloc.c,v 3.46 2006/03/02 18:46:44 christos Exp $
 *
 * oldfaber:
 *      showall() is static and modified
 *      added a return value to morecore
 *      minimum default allocation is 4k
 *      realloc_srclen enlarged to 32
 *      removed useless types (U_char, U_int, ...)
 *      use W32DEBUG
 *
 * if defined(RCHECK)  sizeof(union overhead)==8
 * else                sizeof(union overhead)==4
 * The first bucket is used only for 0 allocations if RCHECK not defined,
 * it's the only case where nbytes <= 8
 *   nbytes = MEMALIGN(MEMALIGN(sizeof(union overhead)) + request + 0);
 *
 * what about failed checks: should we dereference the bad ptr (crashing the
 * application) or return ? for ffree() it's a leak, but for corrupted
 * heap ?
 * CHECK is the same, if W32DEBUG or not ?
 *
 * NOTE: fmalloc() detatches the block from the linked list built by morecore()
 *       and returns the ptr to the block. There is no way to know how many blocks
 *       are allocated and how much heap space is used.
*/


#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>

#include "ntdbg.h"
#include "fmalloc.h"

/* sbrk() on POSIX is declared id unistd.h, but Win32 is not POSIX */
void *sbrk (ptrdiff_t delta);

static void showall();
static char *memtop = NULL;		/* PWP: top of current memory */
static char *membot = NULL;		/* PWP: bottom of allocatable memory */
static int realloc_srchlen = 32;

/*
 * Lots of os routines are busted and try to free invalid pointers.
 * Although our free routine is smart enough and it will pick bad
 * pointers most of the time, in cases where we know we are going to get
 * a bad pointer, we'd rather leak.
 */

/*
 * The overhead on a block is at least 4 bytes.  When free, this space
 * contains a pointer to the next free block, and the bottom two bits must
 * be zero.  When in use, the first byte is set to MAGIC, and the second
 * byte is the size index.  The remaining bytes are for alignment.
 * If range checking is enabled and the size of the block fits
 * in two bytes, then the top two bytes hold the size of the requested block
 * plus the range checking words, and the header word MINUS ONE.
 */

#define RCHECK

#define MEMALIGN(a) (((a) + ROUNDUP) & ~ROUNDUP)

union overhead {
	union overhead *ov_next;		/* when free */
	struct {
		unsigned char ovu_magic;	/* magic number */
		unsigned char ovu_index;	/* bucket # */
#ifdef RCHECK
		unsigned short ovu_size;	/* actual block size */
		unsigned       ovu_rmagic;    	/* range magic number */
#endif
	} ovu;
#define	ov_magic	ovu.ovu_magic
#define	ov_index	ovu.ovu_index
#define	ov_size		ovu.ovu_size
#define	ov_rmagic	ovu.ovu_rmagic
};

#define	MAGIC		0xfd		/* magic # on accounting info */
#define RMAGIC		0x55555555	/* magic # on range info */
#ifdef RCHECK
#define	RSLOP		sizeof (unsigned)
#else
#define	RSLOP		0
#endif


#define ROUNDUP	7

/*
 * nextf[i] is the pointer to the next free block of size 2^(i+3).  The
 * smallest allocatable block is 8 bytes.  The overhead information
 * precedes the data area returned to the user.
 */
#define	NBUCKETS ((sizeof(long) << 3) - 3)
static union overhead *nextf[NBUCKETS] = {0};

/*
 * nmalloc[i] is the difference between the number of mallocs and frees
 * for a given block size.
 */
static unsigned nmalloc[NBUCKETS] = {0};


static int findbucket(union overhead *, int);
static int morecore(int);

#if defined(W32DEBUG)
# define CHECK(a, str, p) \
    if (a) { \
	dbgprintf(PR_ERROR, str, p);	\
	dbgprintf(PR_ERROR, " (memtop = 0x%p membot = 0x%p)\n", memtop, membot);	\
    }
#else
# define CHECK(a, str, p) \
    if (a) { \
	dbgprintf(PR_ERROR, str, p);	\
	dbgprintf(PR_ERROR, " (memtop = 0x%p membot = 0x%p)\n", memtop, membot);	\
    }
#endif


#if defined(W32DEBUG)
/* check that a pointer is in the heap */
static int checkptr(const void *cp)
{
	if (!memtop || !membot) {
		dbgprintf(PR_ERROR, "!!! %s(0x%p): invalid global pointers (membot:0x%p, memtop:0x%p)\n", __FUNCTION__, cp, memtop, membot);
		return (0);
	}
	if (!cp) {
		/* a NULL ptr is OK */
		return (1);
	}
	if (cp >= (void *) memtop) {
		dbgprintf(PR_ERROR, "!!! %s(0x%p): >= heap top (=0x%p)\n", __FUNCTION__, cp, memtop);
		return (0);
	}
	if (cp < (void *) membot) {
		dbgprintf(PR_ERROR, "!!! %s(0x%p): < heap bottom (=0x%p)\n", __FUNCTION__, cp, membot);
		return (0);
	}
	return (1);
}
#endif


void *fmalloc(size_t nbytes)
{
	union overhead *p;
	int bucket = 0;
	size_t shiftr;
#if defined(W32DEBUG)
	size_t malloc_req = nbytes;
#endif

	/*
	 * Convert amount of memory requested into closest block size stored in
	 * hash buckets which satisfies request.  Account for space used per block
	 * for accounting.
	 */
	nbytes = MEMALIGN(MEMALIGN(sizeof(union overhead)) + nbytes + RSLOP);
	shiftr = (nbytes - 1) >> 2;

	/* apart from this loop, this is O(1) */
	while ((shiftr >>= 1) != 0)
		bucket++;
	/*
	 * If nothing in hash bucket right now, request more memory from the
	 * system.
	 */
	if (nextf[bucket] == NULL)
		morecore(bucket);
	if ((p = nextf[bucket]) == NULL) {
		showall();
		dbgprintf(PR_ERROR, "!!! Out of memory, request %lu\n", (unsigned long)nbytes);
		errno = ENOMEM;
		return (NULL);
	}
#if defined(W32DEBUG)
	if (!checkptr(p)) {
                return (NULL);
	}
	/* overwrite error: sometimes nextf[bucket]->ov_next == MAGIC ! */
	if (p->ov_magic == MAGIC) {
		dbgprintf(PR_ERROR, "!!! memory error - request %lu  bucket %d - Break\n", (unsigned long)nbytes, bucket);
		/* fake out of memory ! */
		errno = ENOMEM;
		return (NULL);
	}
#endif
	/* remove from linked list */
	nextf[bucket] = nextf[bucket]->ov_next;
	p->ov_magic = MAGIC;
	p->ov_index = (unsigned char)bucket;
	nmalloc[bucket]++;
#ifdef RCHECK
	/*
	 * Record allocated size of block and bound space with magic numbers.
	 */
	p->ov_size = (unsigned short)((p->ov_index <= 13) ? nbytes - 1 : 0);
	p->ov_rmagic = RMAGIC;
	*((unsigned *) (((char *) p) + nbytes - RSLOP)) = RMAGIC;
#endif
#if defined(W32DEBUG)
        /* malloc_req is defined ony if W32DEBUG */
	dbgprintf(PR_ALLOC, "fmalloc(%lu) returning 0x%p\n", (unsigned long)malloc_req, (char *)p + MEMALIGN(sizeof(union overhead)));
#endif
	return ((void *) (((char *)p) + MEMALIGN(sizeof(union overhead))));
}


/*
 * Allocate more memory to the indicated bucket.
 * Returns 0 if no more core
 */
static int morecore(int bucket)
{
	union overhead *op;
	char *newtop;
	int rnu;	       	/* 2^rnu bytes will be requested */
	int nblks;		/* become nblks blocks of the desired size */
	int siz;

	if (nextf[bucket]) {
		dbgprintf(PR_ERROR, "!!! %s(%d) error: bucket empty\n", __FUNCTION__, bucket);
		return (0);
	}
	/*
	 * Insure memory is allocated on a page boundary.  Should make getpagesize
	 * call?
	 */
	op = (union overhead *) sbrk(0);
	memtop = (char *) op;
	if (memtop == NULL)
		/* heap NOT initialised */
		return (0);
	if (membot == NULL)
		membot = memtop;
        /* condition always false, VirtualAlloc() allocation granularity is 64K,
           morecore() allocation granularity is 4K */
	if ((size_t) op & 0x3ff) {
		newtop = (char *) sbrk(1024 - ((size_t) op & 0x3ff));
		if (newtop != ((char *)(-1)))
			memtop = newtop;
		else
			return (0);
		memtop += (long) (1024 - ((size_t) op & 0x3ff));
	}

	/* take 4k unless the block is bigger than that */
	rnu = (bucket <= 9) ? 12 : bucket + 3;
	nblks = 1 << (rnu - (bucket + 3));	/* how many blocks to get */
	newtop = (char *) sbrk((ptrdiff_t) (1 << rnu));
	op = (union overhead *) newtop;
	/* no more room! */
	if ((size_t) op == (size_t)(-1))
		return (0);
	memtop = newtop;
	memtop += (long) (1 << rnu);
	/*
	 * Round up to minimum allocation size boundary and deduct from block count
	 * to reflect.
	 */
	if (((size_t) op) & ROUNDUP) {
		op = (union overhead *) (((size_t) op + (ROUNDUP + 1)) & ~ROUNDUP);
		nblks--;
	}
	/*
	 * Add new memory allocated to that on free list for this hash bucket.
	 */
	nextf[bucket] = op;
	dbgprintf(PR_ALLOC, "%s(%d) returns 0x%p\n", __FUNCTION__, bucket, (void *)op);
	siz = 1 << (bucket + 3);
	while (--nblks > 0) {
		op->ov_next = (union overhead *) (((char *) op) + siz);
		op = (union overhead *) (((char *) op) + siz);
	}
	op->ov_next = NULL;
#if defined(W32DEBUG)
	showall();
#endif
        return (1);
}


void ffree(void *cp)
{
	int size;
	union overhead *op;

	if (cp == NULL)
		return;
	CHECK(!memtop || !membot,
			"ffree(0x%p) called before any allocations.", cp);
	if (cp < (void *)membot || cp >= (void *)memtop) {
		/* @@@@ beware: dereferences cp! */
		dbgprintf(PR_ALL, "%s(0x%p[\"%s\"]) free foreign block\n", __FUNCTION__, cp, (char *)cp);
                /* @@@@ should free() ? */
		return;
	}
	op = (union overhead *) (((char *) cp) - MEMALIGN(sizeof(union overhead)));
	CHECK(op->ov_magic != MAGIC, "ffree(0x%p) bad block MAGIC", cp);

#ifdef RCHECK
	if (op->ov_index <= 13) {
//		CHECK(*(unsigned *) ((char *) op + op->ov_size + 1 - RSLOP) != RMAGIC,
//				"ffree(0x%p) bad range check.", cp);
		if (*(unsigned *) ((char *) op + op->ov_size + 1 - RSLOP) != RMAGIC) {
			dbgprintf(PR_ERROR, "!!! %s(0x%p) bad range check.\n", __FUNCTION__, cp);
			CHECK(op->ov_index >= NBUCKETS, "ffree(0x%p) bad block index.", cp);
			/* leak memory, avoid corrupting more ! */
                        return;
		}
	} else
		dbgprintf(PR_ALLOC, "unchecked ffree(0x%p) for bucket %d\n", cp, op->ov_index);
#endif
        CHECK(op->ov_index >= NBUCKETS, "ffree(0x%p) bad block index.", cp);
	size = op->ov_index;
	op->ov_next = nextf[size];
	nextf[size] = op;
	dbgprintf(PR_ALLOC, "%s(0x%p) for bucket %d\n", __FUNCTION__, cp, size);

	nmalloc[size]--;
}


void *fcalloc(size_t i, size_t j)
{
	char *cp;

	i *= j;
	/* i*j MAY_OVERFLOW */
	cp = (char *) fmalloc(i);
	memset(cp, 0, i);

	return (cp);
}


/*
 * When a program attempts "storage compaction" as mentioned in the
 * old malloc man page, it realloc's an already freed block.  Usually
 * this is the last block it freed; occasionally it might be farther
 * back.  We have to search all the free lists for the block in order
 * to determine its bucket: 1st we make one pass thru the lists
 * checking only the first block in each; if that fails we search
 * ``realloc_srchlen'' blocks in each list for a match (the variable
 * is extern so the caller can modify it).  If that fails we just copy
 * however many bytes was given to realloc() and hope it's not huge.
 */


void *frealloc(void *cp, size_t nbytes)
{
	size_t onb;
	union overhead *op;
	void *res;
	int i;
	int was_alloced = 0;
#if defined(W32DEBUG)
	size_t realloc_req = nbytes;
#endif

	if (cp == NULL)
		return (fmalloc(nbytes));
	/* check for reallocating memory allocated outside us */
	if (cp < (void *)membot || cp >= (void *)memtop) {
		dbgprintf(PR_ALL, "%s(0x%p, %u): reallocating foreign block\n", __FUNCTION__, cp, nbytes);
		/* this is the CRT realloc! returning NULL will exit(1) zsh! */
		return realloc(cp, nbytes);
	}
	op = (union overhead *) (((char *) cp) - MEMALIGN(sizeof(union overhead)));
	if (op->ov_magic == MAGIC) {
		was_alloced++;
		i = op->ov_index;
	}
	else
		/*
		 * Already free, doing "compaction".
		 *
		 * Search for the old block of memory on the free list.  First, check the
		 * most common case (last element free'd), then (this failing) the last
		 * ``realloc_srchlen'' items free'd. If all lookups fail, then assume
		 * the size of the memory block being realloc'd is the smallest
		 * possible.
		 */
		if ((i = findbucket(op, 1)) < 0 &&
		    (i = findbucket(op, realloc_srchlen)) < 0)
			i = 0;

	onb = MEMALIGN(nbytes + MEMALIGN(sizeof(union overhead)) + RSLOP);

	/* avoid the copy if same size block */
	if (was_alloced && (onb <= (unsigned) (1 << (i + 3))) &&
	    (onb > (unsigned) (1 << (i + 2)))) {
#ifdef RCHECK
		/* JMR: formerly this wasn't updated ! */
		nbytes = MEMALIGN(MEMALIGN(sizeof(union overhead))+nbytes+RSLOP);
		*((unsigned *) (((char *) op) + nbytes - RSLOP)) = RMAGIC;
		op->ov_rmagic = RMAGIC;
		op->ov_size = (unsigned short)((op->ov_index <= 13) ? nbytes - 1 : 0);
#endif
#if defined(W32DEBUG)
		dbgprintf(PR_ALLOC, "%s(0x%p, %lu): old ptr 0x%p for bucket %d\n", __FUNCTION__, cp, (unsigned long)realloc_req, cp, i);
#endif
		return (cp);
	}
	if ((res = fmalloc(nbytes)) == NULL)
		return (NULL);
	if (cp != res) {		/* common optimization */
		/*
		 * christos: this used to copy nbytes! It should copy the
		 * smaller of the old and new size
		 */
		onb = (size_t) (1 << (i + 3)) - MEMALIGN(sizeof(union overhead)) - RSLOP;
		(void) memmove(res, cp, onb < nbytes ? onb : nbytes);
	}
	if (was_alloced)
		ffree(cp);
#if defined(W32DEBUG)
	dbgprintf(PR_ALLOC, "%s(0x%p, %lu): new 0x%p for bucket %d\n", __FUNCTION__, cp, (unsigned long)realloc_req, res, i);
#endif
	return (res);
}



/*
 * Search ``srchlen'' elements of each free list for a block whose
 * header starts at ``freep''.  If srchlen is -1 search the whole list.
 * Return bucket number, or -1 if not found.
 */
static int
findbucket(union overhead *freep, int srchlen)
{
	union overhead *p;
	size_t i;
	int j;

	for (i = 0; i < NBUCKETS; i++) {
		j = 0;
		for (p = nextf[i]; p && j != srchlen; p = p->ov_next) {
			if (p == freep)
				return ((int)i);
			j++;
		}
	}
	return (-1);
}


/*
 * mstats - print out statistics about malloc
 *
 * Prints two lines of numbers, one showing the length of the free list
 * for each size category, the second showing the number of mallocs -
 * frees for each size category.
 */
static void showall(void)
{
	unsigned int i, j;
	union overhead *p;
	unsigned totfree = 0, totused = 0;
	char outbuf[2048];
	size_t pos;

	strcpy(outbuf, "current memory allocation:\nfree: ");
	pos = strlen(outbuf);
        /* the default heap size is 32M => 20 bits are enough ! */
	for (i = 0; i < NBUCKETS-9; i++) {
		for (j = 0, p = nextf[i]; p; p = p->ov_next, j++)
			continue;
		pos += (size_t)wsprintfA(outbuf + pos,  " %4d", j);
		totfree += j * (1 << (i + 3));
	}
	pos += (size_t)wsprintfA(outbuf + pos, "\nused: ");
	for (i = 0; i < NBUCKETS-9; i++) {
		pos += (size_t)wsprintfA(outbuf + pos, " %4u", nmalloc[i]);
		totused += nmalloc[i] * (1 << (i + 3));
	}
	dbgprintf(PR_ALLOC, "%s\nTotal in use: %d, total free: %d\nAllocated membot=0x%p memtop=0x%p\n",
		            outbuf, totused, totfree, membot, memtop);
}
