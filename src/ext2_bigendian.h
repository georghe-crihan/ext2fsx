/*	
 * Copyright (c) 2003
 *	Brian Bergstrand.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *
 * $Revision$
 */
 
#ifdef __ppc__
 
static __inline__ void __arch_swap_16 (__u16 from, __u16 *to)
{
   __asm__ volatile("sthbrx %1, 0, %2" : "=m" (*to) : "r" (from), "r" (to));
}

static __inline__ void __arch_swap_32 (__u32 from, __u32 *to)
{
   __asm__ volatile("stwbrx %1, 0, %2" : "=m" (*to) : "r" (from), "r" (to));
}
 
#endif // __ppc__
 
static __inline__ __u16 le16_to_cpu (__u16 val)
{
   __u16 n;
   
   __arch_swap_16(val, &n);
   
   return (n);
}

static __inline__ __u32 le32_to_cpu (__u32 val)
{
   __u32 n;
   
   __arch_swap_32(val, &n);
   
   return (n);
}

#define cpu_to_le16(x) le16_to_cpu((x))

#define cpu_to_le32(x) le32_to_cpu((x))
