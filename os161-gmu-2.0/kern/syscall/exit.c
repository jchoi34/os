#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/unistd.h>
#include <lib.h>
#include <proc.h>
#include <current.h>

void sys__exit(int exitcode) {
	curpoc->exitcode = exitcode;
		
		
}
