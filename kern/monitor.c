// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
    { "backtrace", "Display backtrace", mon_backtrace },
    { "showmappings", "Display virtual addresses mappings", mon_showmappings },
    { "setpageflags", "Set PTE flags for a page associated with a VA", mon_setpageflags},
    { "memdump", "Dump memory at a virtual address", mon_memdump},
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
    struct Eipdebuginfo dbg = {0};
    uint32_t*           ebp;

    cprintf("Stack backtrace:\n");
    
    for (ebp = (uint32_t*)read_ebp() ; ebp ; ebp = (uint32_t*)*ebp) {
        cprintf("ebp is %08x\n", ebp);
        debuginfo_eip((uintptr_t)ebp[1], &dbg);

		cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n",
                 ebp, ebp[1], ebp[2], ebp[3], ebp[4], ebp[5], ebp[6]);
        cprintf("    %s:%d: %.*s+%d\n",
                 dbg.eip_file, dbg.eip_line, dbg.eip_fn_namelen, dbg.eip_fn_name, (ebp[1] - dbg.eip_fn_addr));
    }

	return 0;
}

int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
    uint32_t cr3;
    uintptr_t va;
    uintptr_t start_va;
    uintptr_t end_va;
    pte_t *pte;

    if (argc < 3) {
        cprintf("usage: showmappings <start_va> <end_va>\n");
        return 0;
    }

    cr3 = rcr3();

    start_va = strtol(argv[1], NULL, 16);
    end_va   = strtol(argv[2], NULL, 16);

    cprintf("Virt Addr\tPhys Addr\tFlags\n");

    for (va = start_va; va < end_va; va += PGSIZE) {
        if ((pte = pgdir_walk(KADDR(cr3), (void*)va, 0)) == NULL)
            continue;
        
        cprintf("%08x\t%08x\t%03x\n", va, PTE_ADDR(*pte), *pte & 0xfff);
    }


    return 0;
}

int
mon_setpageflags(int argc, char **argv, struct Trapframe *tf)
{
    uint32_t cr3;
    uintptr_t va;
    uint32_t flags;
    pte_t *pte;
    
    if (argc < 3) {
        cprintf("usage: setpageflags <virt address> <flags>\n");
        return 0;
    }

    cr3 = rcr3();

    va    = strtol(argv[1], NULL, 16);
    flags = strtol(argv[2], NULL, 16) & 0xfff;

    if ((pte = pgdir_walk(KADDR(cr3), (void*)va, 0)) == NULL)
        return 0;

    *pte = (*pte & 0xfffff000) | flags;

    return 0;
}

static void memdump_va(uintptr_t addr) {
    uint32_t i;
    uintptr_t ptr;
    
    const uint32_t DUMP_SIZE = 128;
    const uint32_t LINE_SIZE = 8;
    
    for (ptr = addr; ptr < addr + DUMP_SIZE; ptr += LINE_SIZE) {
        cprintf("%08x: ", ptr);

        for (i = 0; i < LINE_SIZE; ++i) {
            cprintf("%02x ", *((uint8_t*)ptr + i));
        }

        for (i = 0; i < LINE_SIZE; ++i) {
            char ch = *((uint8_t*)ptr + i);

            if (ch > 126 || ch < 32) {
                cprintf("%c", '.');
            } else {
                cprintf("%c", ch);
            }
        }

        cprintf("\n");
    }
}

int
mon_memdump(int argc, char **argv, struct Trapframe *tf)
{
    uint32_t addr;
    bool p;

    if (argc < 3) {
        cprintf("usage: memdump <p/v> <physical/virtual address>\n");
        return 0;
    }

    p    = argv[1][0] == 'p';
    addr = strtol(argv[2], NULL, 16);
    addr = p ? addr + KERNBASE : addr;

    memdump_va(addr);

    return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
