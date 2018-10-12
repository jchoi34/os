#include <types.h>
#include <proc.h>
#include <proctable.h>
#include <current.h>
#include <syscall.h>
#include <array.h>
#include <synch.h>
#include <kern/errno.h>
#include <mips/trapframe.h>

pid_t sys_fork(struct trapframe *tf, int *err) {
	(void) tf;
	(void) err;

	return 0;
}
