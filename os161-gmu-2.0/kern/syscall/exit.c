#include <types.h>
#include <proc.h>
#include <proctable.h>
#include <current.h>
#include <syscall.h>
#include <kern/wait.h>
#include <addrspace.h>

void sys__exit(int exitcode) {
		
	struct status *s;	
	struct lock *l;
	struct cv *c;
	struct addrspace *as;

	l = array_get(lock_table, curproc->pid-1);
	c = array_get(cv_table, curproc->pid-1);
	lock_acquire(l);
	s = array_get(status_table, curproc->pid-1);
	s->exitcode = _MKWAIT_EXIT(exitcode);

	if(s->waiting == 1) {
		cv_signal(c, l);
		cv_wait(c, l);	
	}	
	// free linked list of pids
        struct pid_list *tofree;
        while(curproc->children != NULL) {
		tofree = curproc->children;
                curproc->children = curproc->children->next;  
                kfree(tofree);
        }                                                                 
	lock_release(l);
	
	struct proc *p = curproc;	

	as_deactivate();
	as = proc_setas(NULL); 
	as_destroy(as);
	// destroy process after any process waiting on this one signaled
	proc_remthread(curthread);
	proc_destroy(p);
	thread_stop();
}
