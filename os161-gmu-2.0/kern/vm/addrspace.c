/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
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
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <spl.h>
#include <mips/tlb.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */


struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	/*
	 * Initialize as needed.
	 */
	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;
	as->complete = 0;

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	/*
	 * Write this.
	 */

	newas->as_vbase1 = old->as_vbase1;
	newas->writable1 = old->writable1;
	newas->as_npages1 = old->as_npages1;
	newas->as_vbase2 = old->as_vbase2;
	newas->writable2 = old->writable2;
	newas->as_npages2 = old->as_npages2;

	// Use as_prepare_load to find free pages for
	// the new address space
	if(as_prepare_load(newas)) {
		as_destroy(newas);
		return ENOMEM;
	}

	KASSERT(newas->as_pbase1 != 0);
	KASSERT(newas->as_pbase2 != 0);
        KASSERT(newas->as_stackpbase != 0);

	memmove((void *)PADDR_TO_KVADDR(newas->as_pbase1),
                (const void *)PADDR_TO_KVADDR(old->as_pbase1),
                old->as_npages1*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(newas->as_pbase2),
		(const void *)PADDR_TO_KVADDR(old->as_pbase2),
		old->as_npages2*PAGE_SIZE);

        memmove((void *)PADDR_TO_KVADDR(newas->as_stackpbase),
                (const void *)PADDR_TO_KVADDR(old->as_stackpbase),
                VM_STACKPAGES*PAGE_SIZE);

	newas->complete = 1;
	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */


	if(as->as_pbase1 != 0) {
		free_kpages(PADDR_TO_KVADDR(as->as_pbase1));
	}
	if(as->as_pbase2 != 0) {
		free_kpages(PADDR_TO_KVADDR(as->as_pbase2));
	}

	if(as->as_stackpbase != 0) {
		free_kpages(PADDR_TO_KVADDR(as->as_stackpbase));
	}
	kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	/*
	 * Write this.
	 */
	int spl;
	spl = splhigh();
	int i;
        for (i=0; i<NUM_TLB; i++) {
                tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
        }

        splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writable, int executable)
{
	/*
	 * Write this.
	 */
	size_t npages;

	/* Align the region. First, the base... */
        sz += vaddr & ~(vaddr_t)PAGE_FRAME;
        vaddr &= PAGE_FRAME;

	/* ...and now the length. */
        sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

        npages = sz / PAGE_SIZE;

	(void)readable;
	(void)executable;

	if (as->as_vbase1 == 0) {
                as->as_vbase1 = vaddr;
		as->writable1 = writable;
                as->as_npages1 = npages;
                return 0;
        }

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->writable2 = writable;
		as->as_npages2 = npages;
		return 0;
	}

	//return EUNIMP;
	return -1;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
        bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */
	KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
        KASSERT(as->as_stackpbase == 0);

	as->as_pbase1 = KVADDR_TO_PADDR(alloc_kpages(as->as_npages1));
        if (as->as_pbase1 == 0) {
                return ENOMEM;
        }

	as->as_pbase2 = KVADDR_TO_PADDR(alloc_kpages(as->as_npages2));
	if (as->as_pbase2 == 0) {
		return ENOMEM;
	}

	as->as_stackpbase = KVADDR_TO_PADDR(alloc_kpages(VM_STACKPAGES));
	if (as->as_stackpbase == 0) {
                return ENOMEM;
        }

	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
        as_zero_region(as->as_stackpbase, VM_STACKPAGES);

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */


	as->complete = 1;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

