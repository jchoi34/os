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
	struct status *s;
	struct lock *l;
	struct cv *c;
	
	lock_acquire(getpid_lock);
	p = array_get(process_table, pid - 1);	// get the process from table using pid
	if(p == NULL){	// process nonexistent
		lock_release(getpid_lock);
		if(status != NULL)
			*status = ESRCH;
		*err = ESRCH;
		return -1;
	}
	l = array_get(lock_table, pid-1);
	c = array_get(cv_table, pid-1);
	lock_acquire(l);
	s = array_get(status_table, pid-1);
	if(s->exitcode != -1){	// process already exited so return immediately
		lock_release(l);
		lock_release(getpid_lock);	
		return pid;
	}
	else {			// process not yet exited
		lock_release(getpid_lock);	
		s->waiting = 1;	// let child know parent is waiting	
		cv_wait(c, l); // wait for the child while releasing its lock

		if(status != NULL) {	// get the status from child
			if(WIFEXITED(s->exitcode))
				*status = WEXITSTATUS(s->exitcode);
			else if(WIFSIGNALED(s->exitcode))
				*status = WTERMSIG(s->exitcode);
			else if(WIFSTOPPED(s->exitcode))
				*status = WSTOPSIG(s->exitcode);
		}

		cv_signal(c, l);	// let the child continue with its exit
		lock_release(l);	
	}

	return pid;
}
