#include <types.h>
#include <proc.h>
#include <proctable.h>
#include <current.h>
#include <syscall.h>
#include <spl.h>
#include <kern/wait.h>
#include <addrspace.h>

void sys__exit(int exitcode) {
		
	struct lock *l;
	struct cv *c;
	struct addrspace *as;
	int spl = splhigh();
	
	l = array_get(lock_table, curproc->pid-1);
	c = array_get(cv_table, curproc->pid-1);
	lock_acquire(l);
	
	struct pid_list *temp = NULL;
	struct proc *p;

	p = curproc;	
	if(curproc!= NULL && curproc->parent != NULL && (unsigned int) curproc->parent->children != 0xdeadbeef)
		temp = curproc->parent->children;
	
	while(temp != NULL) {
		if(temp->pid == curproc->pid) {
			temp->exitcode = _MKWAIT_EXIT(exitcode);
			temp->exited = 1;
			break;
		}
		temp = temp->next;
	}

	if(temp != NULL && temp->waiting == 1) {
		cv_signal(c, l);
		cv_wait(c, l);	
	}	
	// free linked list of pids
	lock_acquire(getpid_lock);
        struct pid_list *tofree;
        while(curproc->children != NULL) {
		tofree = curproc->children;
                curproc->children = curproc->children->next;  
                kfree(tofree);
		tofree = NULL;
        }                                                                 
	lock_release(getpid_lock);
	curproc->children = NULL;
	lock_release(l);
	
	p = curproc;	
	int run = p->runtype;
	as_deactivate();
	as = proc_setas(NULL); 
	as_destroy(as);
	if(run)
		V(sem_runproc);
	// destroy process after any process waiting on this one signaled
	proc_remthread(curthread);
	proc_destroy(p);
	splx(spl);
	thread_stop();
}
