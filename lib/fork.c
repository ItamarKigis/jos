// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
    pte_t pte = uvpt[(uintptr_t)addr >> PGSHIFT];
    addr      = ROUNDDOWN(addr, PGSIZE); // needed to be aligned for memmove and syscalls

    // check that it's a write to a cow page
    if (!( (err & FEC_WR) && (pte & PTE_COW) ))
        panic("pgfault: not a cow write\n");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.

    // allocate a new page at PFTEMP
    if ((r = sys_page_alloc(0, (void*)PFTEMP, PTE_W | PTE_U | PTE_P)) < 0)
        panic("pgfault: sys_page_alloc failed %e\n", r);

    // move page contents to page at PFTEMP
    memmove((void*)PFTEMP, (const void*)addr, (size_t)PGSIZE);
   
    // map our page to PFTEMP
    if ((r = sys_page_map(0, (void*)PFTEMP, 0, addr, PTE_W | PTE_U | PTE_P)) < 0)
        panic("pgfault: sys_page_map failed %e\n", r);
   
    // unmap PFTEMP
    if ((r = sys_page_unmap(0, (void*)PFTEMP)) < 0)
        panic("pgfault: sys_page_unmap failed %e\n", r);
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
    void *addr;
    pte_t pte;

    addr = (void*)(pn << PGSHIFT);
    pte  = uvpt[pn];

    if (pte & (PTE_W | PTE_COW)) {
        if ((r = sys_page_map(0, addr, envid, addr, PTE_P | PTE_U | PTE_COW)) < 0)
            panic("duppage: sys_page_map failed. %e\n", r);
        
        if ((r = sys_page_map(0, addr,     0, addr, PTE_P | PTE_U | PTE_COW)) < 0)
            panic("duppage: sys_page_map failed. %e\n", r);

        return 0;
    }

    // page is not writable and not COW - readonly
    // map it to envid with same flags as parent
    if ((r = sys_page_map(0, addr, envid, addr, pte & 0xfff)) < 0)
        panic("duppage: sys_page_map failed. %e\n", r);

	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
    envid_t   child;
    uintptr_t addr;
    int       r;

    set_pgfault_handler(pgfault);

    if ((child = sys_exofork()) < 0)
        panic("fork: sys_exofork failed. %e\n", child);

    if (child == 0) {
        // child executing
        // like dumbfork
        thisenv = &envs[ENVX(sys_getenvid())];
        return 0;
    }

    // parent executing

    // copy address space at range [0, UTOP - PGSIZE)
    // (notice that UXSTACKTOP = UTOP so we stop 1 page earlier to not dup the uxstack)
    for (addr = 0; addr < (UTOP - PGSIZE); addr += PGSIZE) {
        // addr doesn't point to a mapped page
        if (!uvpd[addr >> PTSHIFT] || !uvpt[addr >> PGSHIFT])
            continue;

        duppage(child, addr >> PGSHIFT);
    }

    // Lab 4 Challenge
    // batch page_alloc and page_map to a single syscall
    int calls1[4] = {
        SYS_page_alloc, // alloc new page for child's UXSTACK (addr points to UXSTACKTOP - PGSIZE)
        SYS_page_map,   // map child's UXSTACK to a temp page on parent to make memmove possible
        -1,
        -1
    };

    int args1[4][5] = {
        {child, addr, PTE_W | PTE_U | PTE_P,           0,                     0},
        {child, addr,                     0, (int)PFTEMP, PTE_W | PTE_U | PTE_P},
        {0},
        {0}
    };
    
    if ((r = sys_batch(calls1, args1)) < 0)
        panic("fork: sys_batch failed %e\n", r);
   
    // memmove..
    memmove((void*)PFTEMP, (const void*)addr, (size_t)PGSIZE);

    // Lab 4 Challenge
    // batch page_unmap, env_set_pgfault_upcall and sys_env_set_status
    int calls2[4] = {
        SYS_page_unmap,             // unmap parent's PFTEMP, no need for it anymore
        SYS_env_set_pgfault_upcall, // set pgfault upcall for child to match parent
        SYS_env_set_status,         // set child to runnable
        -1
    };

    int args2[4][5] = {
        {0,     (int)PFTEMP,                   0, 0, 0},
        {child, (int)thisenv->env_pgfault_upcall, 0, 0, 0},
        {child, ENV_RUNNABLE,                  0, 0, 0},
        {0}
    };
    
    if ((r = sys_batch(calls2, args2)) < 0)
        panic("fork: sys_batch failed %e\n", r);

    return child;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
