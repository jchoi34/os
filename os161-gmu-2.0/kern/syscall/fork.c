#include <types.h>
#include <proc.h>
#include <proctable.h>
#include <current.h>
#include <syscall.h>
#include <array.h>
#include <synch.h>
#include <kern/errno.h>
#include <mips/trapframe.h>
#include <addrspace.h>

static void init_child_proc(void *p, unsigned long data) {

	(void) data;
	struct trapframe tf;
	tf = *(struct trapframe *)p;
	// activate child's addr space
	as_activate();

	// need to advance program counter
	tf.tf_epc += 4; 

	//indicates success
	tf.tf_v0 = 0;
	tf.tf_a3 = 0;
	
	// start process in usermode
	mips_usermode(&tf);

}

pid_t sys_fork(struct trapframe *tf, int *err) {
	if(tf == NULL)
		return -1;
	int result;
	struct proc *child;
	struct lock *l;
	struct cv *c;
	struct status *s;
	
	// assign a pid and reserve a space in proc table
	KASSERT(getpid_lock != NULL);
	lock_acquire(getpid_lock);
	pid_t pid = get_pid();
        if(pid == -1) { // no more pids
		*err = ENPROC;
                lock_release(getpid_lock);
		goto err0;
        }
        array_set(process_table, pid, child);

	if(array_get(lock_table, pid) == NULL) {
		l = lock_create("lock");
		if(l == NULL) {
        		lock_release(getpid_lock);
			*err = ENOMEM;
			goto err1;
		}
		array_set(lock_table, pid, l);
	}

	if(array_get(cv_table, pid) == NULL) {
		c = cv_create("cv");
		if(c == NULL) {
			lock_destroy(array_get(lock_table, pid));
			array_set(lock_table, pid, NULL);
        		lock_release(getpid_lock);
			*err = ENOMEM;
			goto err1;
		}
		array_set(cv_table, pid, c);
	}

	s = kmalloc(sizeof(struct status));
	if(s == NULL) {
		lock_destroy(array_get(lock_table, pid));
		array_set(lock_table, pid, NULL);
		cv_destroy(array_get(cv_table, pid));
		array_set(cv_table, pid, NULL);
		lock_release(getpid_lock);
		*err = ENOMEM;
		goto err1;
	}
	s->exitcode = -1;
	s->waiting = 0;
	array_set(status_table, pid, s);
	
        lock_release(getpid_lock);
	
	// create the child process and get the calling process's
	// open file handles
	result = proc_fork(&child);	
	
	if(result == ENOMEM) {
		kfree(s);
		*err = ENOMEM;
		goto err1;
	}
	child->pid = pid + 1;
	child->parent = curproc;

	// add child pid to head of list of parent's children
	struct pid_list *parents_child; 
	parents_child = kmalloc(sizeof(*parents_child));
	if(parents_child == NULL) {
		*err = ENOMEM;
		goto err2;
	}
	parents_child->pid = pid+1;
	parents_child->exitcode = -1;
	parents_child->next = curproc->children;	
	curproc->children = parents_child;

	// copy the parent's memory into child processes's addr space
	result = as_copy(curproc->p_addrspace, &child->p_addrspace);
	if(result == ENOMEM) {
		*err = ENOMEM;
		goto err3;
	}

	// create a deep copy of parent's trapframe
	// trapframe struct does not contain any pointers
	// so this should make an exact deep copy
	//struct trapframe child_tf = *tf;
	struct trapframe *child_tf = (struct trapframe *)kmalloc(sizeof(struct trapframe));
	*child_tf = *tf;
	
	// create thread for newly created proc
	result = thread_fork("thread", child, init_child_proc, child_tf, 0);

	if(result == ENOMEM) {
		lock_release(child->lock);
		*err = ENOMEM;
		goto err3;
	} else if (result) {
		lock_release(child->lock);
		*err = result;
		goto err3;
	}
	
	return pid+1;

	err3:
		parents_child = curproc->children;
		curproc->children = curproc->children->next;
		kfree(parents_child);
	err2:
		proc_destroy(child);
	err1:
		lock_acquire(getpid_lock);
		array_remove(process_table, pid);
		lock_release(getpid_lock);
	err0:
		return -1;
}
