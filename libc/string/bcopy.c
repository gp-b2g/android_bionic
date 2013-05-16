/*	$OpenBSD: bcopy.c,v 1.5 2005/08/08 08:05:37 espie Exp $ */
/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
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

#include <string.h>

/*
 * sizeof(word) MUST BE A POWER OF TWO
 * SO THAT wmask BELOW IS ALL ONES
 */
typedef	long word;		/* "word" used for optimal copy speed */

#define	wsize	sizeof(word)
#define	wmask	(wsize - 1)

/*
 * Copy a block of memory, handling overlap.
 * This is the routine that actually implements
 * (the portable versions of) bcopy, memcpy, and memmove.
 */
#ifdef MEMCOPY
void *
memcpy(void *dst0, const void *src0, size_t length)
#else
#ifdef MEMMOVE
void *
memmove(void *dst0, const void *src0, size_t length)
#else
void
bcopy(const void *src0, void *dst0, size_t length)
#endif
#endif
{
	char *dst = dst0;
	const char *src = src0;
	size_t t;

	if (length == 0 || dst == src)		/* nothing to do */
		goto done;

	/*
	 * Macros: loop-t-times; and loop-t-times, t>0
	 */
#define	TLOOP(s) if (t) TLOOP1(s)
#define	TLOOP1(s) do { s; } while (--t)

	if ((unsigned long)dst < (unsigned long)src) {
    #if defined(__ARM_NEON__) && !defined(ARCH_ARM_USE_NON_NEON_MEMCPY)
        memcpy(dst, src, length);
	#else
		/*
		 * Copy forward.
		 */
		t = (long)src;	/* only need low bits */
		if ((t | (long)dst) & wmask) {
			/*
			 * Try to align operands.  This cannot be done
			 * unless the low bits match.
			 */
			if ((t ^ (long)dst) & wmask || length < wsize)
				t = length;
			else
				t = wsize - (t & wmask);
			length -= t;
			TLOOP1(*dst++ = *src++);
		}
		/*
		 * Copy whole words, then mop up any trailing bytes.
		 */
		t = length / wsize;
		TLOOP(*(word *)dst = *(word *)src; src += wsize; dst += wsize);
		t = length & wmask;
		TLOOP(*dst++ = *src++);
    #endif
	} else {
    #if defined(__ARM_NEON__) && !defined(ARCH_ARM_USE_NON_NEON_MEMCPY)
        src += length;
        dst += length;
        if (!(((unsigned long)dst ^ (unsigned long)src) & 0x03)) {
            // can be aligned
            asm volatile (
                "pld        [%[src], #-64]                  \n"
                "tst        %[src], #0x03                   \n"
                "beq        .Lbbcopy_aligned                \n"

            ".Lbbcopy_make_align:                           \n"
                "ldrb       r12, [%[src], #-1]!             \n"
                "subs       %[length], %[length], #1        \n"
                "strb       r12, [%[dst], #-1]!             \n"
                "beq        .Lbbcopy_out                    \n"
                "tst        %[src], #0x03                   \n"
                "bne        .Lbbcopy_make_align             \n"

            ".Lbbcopy_aligned:                              \n"
                "cmp        %[length], #64                  \n"
                "blt        .Lbbcopy_align_less_64          \n"
            ".Lbbcopy_align_loop64:                         \n"
                "vldmdb     %[src]!, {q0 - q3}              \n"
                "sub        %[length], %[length], #64       \n"
                "cmp        %[length], #64                  \n"
                "pld        [%[src], #-64]                  \n"
                "pld        [%[src], #-96]                  \n"
                "vstmdb     %[dst]!, {q0 - q3}              \n"
                "bge        .Lbbcopy_align_loop64           \n"
                "cmp        %[length], #0                   \n"
                "beq        .Lbbcopy_out                    \n"

            ".Lbbcopy_align_less_64:                        \n"
                "cmp        %[length], #32                  \n"
                "blt        .Lbbcopy_align_less_32          \n"
                "vldmdb     %[src]!, {q0 - q1}              \n"
                "subs       %[length], %[length], #32       \n"
                "vstmdb     %[dst]!, {q0 - q1}              \n"
                "beq        .Lbbcopy_out                    \n"

            ".Lbbcopy_align_less_32:                        \n"
                "cmp        %[length], #16                  \n"
                "blt        .Lbbcopy_align_less_16          \n"
                "vldmdb     %[src]!, {q0}                   \n"
                "subs       %[length], %[length], #16       \n"
                "vstmdb     %[dst]!, {q0}                   \n"
                "beq        .Lbbcopy_out                    \n"

            ".Lbbcopy_align_less_16:                        \n"
                "cmp        %[length], #8                   \n"
                "blt        .Lbbcopy_align_less_8           \n"
                "vldmdb     %[src]!, {d0}                   \n"
                "subs       %[length], %[length], #8        \n"
                "vstmdb     %[dst]!, {d0}                   \n"
                "beq        .Lbbcopy_out                    \n"

            ".Lbbcopy_align_less_8:                         \n"
                "cmp        %[length], #4                   \n"
                "blt        .Lbbcopy_align_less_4           \n"
                "ldr        r12, [%[src], #-4]!             \n"
                "subs       %[length], %[length], #4        \n"
                "str        r12, [%[dst], #-4]!             \n"
                "beq        .Lbbcopy_out                    \n"

            ".Lbbcopy_align_less_4:                         \n"
                "cmp        %[length], #2                   \n"
                "blt        .Lbbcopy_align_less_2           \n"
                "ldrh       r12, [%[src], #-2]!             \n"
                "subs       %[length], %[length], #2        \n"
                "strh       r12, [%[dst], #-2]!             \n"
                "beq        .Lbbcopy_out                    \n"

            ".Lbbcopy_align_less_2:                         \n"
                "ldrb       r12, [%[src], #-1]!             \n"
                "strb       r12, [%[dst], #-1]!             \n"

            ".Lbbcopy_out:                                  \n"
                :
                : [src] "r" (src), [dst] "r" (dst), [length] "r" (length)
                : "memory", "cc", "r12"
            );
        } else {
            // can not be aligned
            asm volatile (
                "cmp        %[length], #64                  \n"
                "pld        [%[src], #-32]                  \n"
                "blt        .Lbbcopy___less_64              \n"
                "mov        r12, #-32                       \n"
                "sub        %[src], %[src], #32             \n"
                "sub        %[dst], %[dst], #32             \n"
            ".Lbbcopy___loop64:                             \n"
                "vld1.8     {q0 - q1}, [%[src]], r12        \n"
                "vld1.8     {q2 - q3}, [%[src]], r12        \n"
                "sub        %[length], %[length], #64       \n"
                "cmp        %[length], #64                  \n"
                "pld        [%[src], #-64]                  \n"
                "pld        [%[src], #-96]                  \n"
                "vst1.8     {q0 - q1}, [%[dst]], r12        \n"
                "vst1.8     {q2 - q3}, [%[dst]], r12        \n"
                "bge        .Lbbcopy___loop64               \n"
                "cmp        %[length], #0                   \n"
                "beq        .Lbcopy_out                     \n"
                "add        %[src], %[src], #32             \n"
                "add        %[dst], %[dst], #32             \n"

            ".Lbbcopy___less_64:                            \n"
                "cmp        %[length], #32                  \n"
                "blt        .Lbbcopy___less_32              \n"
                "sub        %[src], %[src], #32             \n"
                "sub        %[dst], %[dst], #32             \n"
                "vld1.8     {q0 - q1}, [%[src]]             \n"
                "subs       %[length], %[length], #32       \n"
                "vst1.8     {q0 - q1}, [%[dst]]             \n"
                "beq        .Lbcopy_out                     \n"

            ".Lbbcopy___less_32:                            \n"
                "cmp        %[length], #16                  \n"
                "blt        .Lbbcopy___less_16              \n"
                "sub        %[src], %[src], #16             \n"
                "sub        %[dst], %[dst], #16             \n"
                "vld1.8     {q0}, [%[src]]                  \n"
                "subs       %[length], %[length], #16       \n"
                "vst1.8     {q0}, [%[dst]]                  \n"
                "beq        .Lbcopy_out                     \n"

            ".Lbbcopy___less_16:                            \n"
                "cmp        %[length], #8                   \n"
                "blt        .Lbbcopy___less_8               \n"
                "sub        %[src], %[src], #8              \n"
                "sub        %[dst], %[dst], #8              \n"
                "vld1.8     {d0}, [%[src]]                  \n"
                "subs       %[length], %[length], #8        \n"
                "vst1.8     {d0}, [%[dst]]                  \n"
                "beq        .Lbcopy_out                     \n"

            ".Lbbcopy___less_8:                             \n"
                "cmp        %[length], #4                   \n"
                "blt        .Lbbcopy___less_4               \n"
                "ldr        r12, [%[src], #-4]!             \n"
                "subs       %[length], %[length], #4        \n"
                "str        r12, [%[dst], #-4]!             \n"
                "beq        .Lbcopy_out                     \n"

            ".Lbbcopy___less_4:                             \n"
                "cmp        %[length], #2                   \n"
                "blt        .Lbbcopy___less_2               \n"
                "ldrh       r12, [%[src], #-2]!             \n"
                "subs       %[length], %[length], #2        \n"
                "strh       r12, [%[dst], #-2]!             \n"
                "beq        .Lbcopy_out                     \n"

            ".Lbbcopy___less_2:                             \n"
                "ldrb       r12, [%[src], #-1]!             \n"
                "strb       r12, [%[dst], #-1]!             \n"

            ".Lbcopy_out:                                   \n"
                :
                : [src] "r" (src), [dst] "r" (dst), [length] "r" (length)
                : "memory", "cc", "r12"
            ); 
        }
	#else
		/*
		 * Copy backwards.  Otherwise essentially the same.
		 * Alignment works as before, except that it takes
		 * (t&wmask) bytes to align, not wsize-(t&wmask).
		 */
		src += length;
		dst += length;
		t = (long)src;
		if ((t | (long)dst) & wmask) {
			if ((t ^ (long)dst) & wmask || length <= wsize)
				t = length;
			else
				t &= wmask;
			length -= t;
			TLOOP1(*--dst = *--src);
		}
		t = length / wsize;
		TLOOP(src -= wsize; dst -= wsize; *(word *)dst = *(word *)src);
		t = length & wmask;
		TLOOP(*--dst = *--src);
    #endif
	}
done:
#if defined(MEMCOPY) || defined(MEMMOVE)
	return (dst0);
#else
	return;
#endif
}
