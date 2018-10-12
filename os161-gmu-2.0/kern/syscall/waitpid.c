#include <types.h>
#include <proc.h>
#include <proctable.h>
#include <current.h>
#include <syscall.h>
#include <array.h>
#include <synch.h>
#include <kern/errno.h>
#include <kern/wait.h>

pid_t sys_waitpid(pid_t pid, int *status, int options, int *err) {

	if(options != 0) {
		if(status != NULL)
			*status = EINVAL;
		*err = EINVAL;
		return -1;
	}
	
	int valid_pid = 0;
	struct pid_list *temp = curproc->children;
	while(temp != NULL) {
		if(pid == temp->pid) {
			valid_pid = 1;
			break;
		}	
		temp = temp->next;
	}
	if(!valid_pid) {
		if(status != NULL)
			*status = ECHILD;	// pid arg is not a child of this process
		*err = ECHILD;
		return -1;
	}
	
	struct proc *p;

	lock_acquire(getpid_lock);
	p = array_get(process_table, pid - 1);	// get the process from table using pid
	if(p == NULL){	// process nonexistent
		lock_release(getpid_lock);
		if(status != NULL)
			*status = ESRCH;
		*err = ESRCH;
		return -1;
	}
	lock_acquire(p->lock);
	if(p->exitcode != -1){	// process already exited so return immediately
		lock_release(p->lock);
		lock_release(getpid_lock);	
		return pid;
	}
	else {			// process not yet exited
		lock_release(getpid_lock);	
		p->waiting = 1;	// let child know parent is waiting	
		cv_wait(p->p_cv, p->lock); // wait for the child while releasing its lock

		if(status != NULL) {	// get the status from child
			if(WIFEXITED(p->exitcode))
				*status = WEXITSTATUS(p->exitcode);
			else if(WIFSIGNALED(p->exitcode))
				*status = WTERMSIG(p->exitcode);
			else if(WIFSTOPPED(p->exitcode))
				*status = WSTOPSIG(p->exitcode);
		}

		cv_signal(p->p_cv, p->lock);	// let the child continue with its exit
		lock_release(p->lock);	
	}

	return pid;
}
