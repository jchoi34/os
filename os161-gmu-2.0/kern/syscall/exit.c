#include <types.h>
#include <proc.h>
#include <proctable.h>
#include <current.h>
#include <syscall.h>
#include <kern/wait.h>

void sys__exit(int exitcode) {
		
	struct status *s;	
	struct lock *l;
	struct cv *c;

	l = array_get(lock_table, curproc->pid-1);
	pid_t p = curproc->pid-1;
	kprintf("%d\n", p);
	c = array_get(cv_table, curproc->pid-1);
	lock_acquire(l);
	s = array_get(status_table, curproc->pid-1);
	s->exitcode = _MKWAIT_EXIT(exitcode);

	if(s->waiting == 1) {
		cv_signal(c, l);
		cv_wait(c, l);	
	}	
	lock_release(l);
	// destroy process after any process waiting on this one signaled
	proc_destroy(curproc);
}
