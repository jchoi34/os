#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <syscall.h>

#define unused 0
#define used 1

unsigned int TOTAL_PAGES;
struct pt_entry *page_table;

void vm_bootstrap(void) {
	ram_bootstrap();
	paddr_t ram_size = ram_getsize();
	if(ram_size % PAGE_SIZE)
		panic("Ram not page size aligned");
	
	// allocate space in ram for the page table
	// and mark its ending location
	unsigned long size;
	TOTAL_PAGES = ram_size / PAGE_SIZE;		
	size = TOTAL_PAGES * sizeof(struct pt_entry);
	size = ROUNDUP(size, PAGE_SIZE); 	
	// take ram space for page table
	paddr_t ptbase_addr = ram_stealmem(size/PAGE_SIZE);
	if(ptbase_addr == 0)
		panic("page_size * npages > ramsize");
	page_table = (struct pt_entry *) PADDR_TO_KVADDR(ptbase_addr);
	// map physical frames to page table minus what's allocated
	// for the page table
	TOTAL_PAGES -= (size/PAGE_SIZE);
	unsigned int i;
	for(i = 0; i < TOTAL_PAGES; i++) {
		page_table[i].p_addr = ram_stealmem(1);
		if(page_table[i].p_addr == 0)
			panic("ram");
		page_table[i].state = unused;
		page_table[i].next = NULL;
	}
}

// used by kmalloc
vaddr_t
alloc_kpages(unsigned npages)
{
	unsigned int i;
	unsigned int count = 0;
	for(i = 0; i < TOTAL_PAGES; i++) {
		if(page_table[i].state == unused) {
			count++;
			if(count == npages) {
				unsigned int j;
				unsigned int first_block = i - (npages - 1);
				for(j = first_block; j < i; j++) {
					page_table[j].next = &page_table[j+1];
					page_table[j].state = used;
				}
				page_table[j].state = used;
				return PADDR_TO_KVADDR(page_table[first_block].p_addr);
			}
		}
		else {
			count = 0;
		}
	}

	// not enough memory
	return 0;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

void
free_kpages(vaddr_t addr)
{
	// need to add check before freeing memory

	unsigned int i;
	for(i = 0; i < TOTAL_PAGES; i++) {
		if(page_table[i].p_addr == KVADDR_TO_PADDR(addr)) 
			break;
	}
	
	if(i == TOTAL_PAGES)
		return;

	int count = 0;
	struct pt_entry *temp = &page_table[i];	
	while(temp != NULL) {
		temp->state = unused;
		temp = temp->next;
		count++;	
	}
	as_zero_region(page_table[i].p_addr, count);
}

// deal with TLB
int vm_fault(int faulttype, vaddr_t faultaddress) {
	vaddr_t vbase1, vbase2, vtop1, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i, writable, complete;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;
	
	faultaddress &= PAGE_FRAME;

	switch(faulttype) {
		case VM_FAULT_READONLY:
			sys__exit(0);	
		case VM_FAULT_READ:
		case VM_FAULT_WRITE:
			break;
		default:
			return EINVAL;	
	}

	if(curproc == NULL) {
		// No process. This is probably a kernel fault early
		// Return EFAULT so as to panic instead of
		// getting into an infinite faulting loop.
		return EFAULT;
	}

	as = proc_getas();

	if(as == NULL) {
		// No address space set up. This is probably also a
		// kernel fault early in boot.
		return EFAULT;
	}

	 /* Assert that the address space has been set up properly. */
        KASSERT(as->as_vbase1 != 0);
        KASSERT(as->as_pbase1 != 0);
        KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
        KASSERT(as->as_stackpbase != 0);
        KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
        KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
        KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
        vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
        stackbase = USERSTACK - VM_STACKPAGES * PAGE_SIZE;
        stacktop = USERSTACK;	
	complete = as->complete;
	
        if (faultaddress >= vbase1 && faultaddress < vtop1) {
                paddr = (faultaddress - vbase1) + as->as_pbase1;
		writable = as->writable1;
        }
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		paddr = (faultaddress - vbase2) + as->as_pbase2;
		writable = as->writable2;
	}
        else if (faultaddress >= stackbase && faultaddress < stacktop) {
                paddr = (faultaddress - stackbase) + as->as_stackpbase;
		writable = 1;
        }
        else {
                return EFAULT;
        }

        /* make sure it's page-aligned */
        KASSERT((paddr & PAGE_FRAME) == paddr);

        /* Disable interrupts on this CPU while frobbing the TLB. */
        spl = splhigh();
	for (i=0; i<NUM_TLB; i++) {
                tlb_read(&ehi, &elo, i);
                if (elo & TLBLO_VALID) {
                        continue;
                }
                ehi = faultaddress;
		if(!complete || writable)
                	elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		else
                	elo = paddr | TLBLO_VALID;
                DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
                tlb_write(ehi, elo, i);
                splx(spl);
                return 0;
        }
	
	// replace a TLB entry
	ehi = faultaddress;
        if(!complete || writable)
               	elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
	else
               	elo = paddr | TLBLO_VALID;
	tlb_random(ehi, elo);
	splx(spl);
	return 0;
}


void
vm_tlbshootdown_all(void)
{
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
        (void)ts;
}
