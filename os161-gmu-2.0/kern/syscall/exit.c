#include <types.h>
#include <proc.h>
#include <current.h>
#include <syscall.h>
#include <kern/wait.h>

void sys__exit(int exitcode) {
		
		
	lock_acquire(curproc->lock);
	curproc->exitcode = _MKWAIT_EXIT(exitcode);

	if(curproc->waiting == 1) {
		cv_signal(curproc->p_cv, curproc->lock);
		cv_wait(curproc->p_cv, curproc->lock);	
	}	
	lock_release(curproc->lock);
	// destroy process after any process waiting on this one signaled
	proc_destroy(curproc);
}
