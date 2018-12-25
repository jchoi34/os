#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/unistd.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <openfile.h>
#include <copyinout.h>
#include <filetable.h>
#include <syscall.h>
#include <test.h>
#include <proctable.h>

/*
 * Open a file on a selected file descriptor. Takes care of various
 * minutiae, like the vfs-level open destroying pathnames.
 */
static
int
placed_open(const char *path, int openflags, int fd)
{
	struct openfile *newfile, *oldfile;
	char mypath[32];
	int result;

	/*
	 * The filename comes from the kernel, in fact right in this
	 * file; assume reasonable length. But make sure we fit.
	 */
	KASSERT(strlen(path) < sizeof(mypath));
	strcpy(mypath, path);

	result = openfile_open(mypath, openflags, 0664, &newfile);
	if (result) {
		return result;
	}

	/* place the file in the filetable in the right slot */
	filetable_placeat(curproc->p_filetable, newfile, fd, &oldfile);

	/* the table should previously have been empty */
	KASSERT(oldfile == NULL);

	return 0;
}

/*
 * Open the standard file descriptors: stdin, stdout, stderr.
 *
 * Note that if we fail part of the way through we can leave the fds
 * we've already opened in the file table and they'll get cleaned up
 * by process exit.
 */
static
int
open_stdfds(const char *inpath, const char *outpath, const char *errpath)
{
	int result;

	result = placed_open(inpath, O_RDONLY, STDIN_FILENO);
	if (result) {
		return result;
	}

	result = placed_open(outpath, O_WRONLY, STDOUT_FILENO);
	if (result) {
		return result;
	}

	result = placed_open(errpath, O_WRONLY, STDERR_FILENO);
	if (result) {
		return result;
	}

	return 0;
}

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
sys_execv(char *progname, char **args, int *err)
{
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
	int sizeCharPointer = sizeof(char *);
	int argc = 0;
	
	P(sem_exec);
	
	unsigned long l = 0;
	if(args != NULL) {
		char *buf = kmalloc(sizeCharPointer);
		if(buf == NULL) {
			*err = ENOMEM;
			V(sem_exec);
			return -1;
		}
		// check the memory addresses if they are valid and get total size of argv by counting each stirng arg's length
		while(args[argc] != NULL && strlen(args[argc]) > 0) {
			result = copyin((const_userptr_t)args[argc], buf, sizeCharPointer);
			if(result) {
				*err = result;
				kfree(buf);
				V(sem_exec);
				return -1;
			} 	
			l += strlen(args[argc++]) + 1 + sizeCharPointer;	// size of each arg + '/0'
			// check including padding and (char *) size
			if(l + (l % 4) + ((l + (l % 4)) % 8) + 4  > ARG_MAX) {
				*err = E2BIG;
				kfree(buf);
				V(sem_exec);
				return -1;	
			}
		}
		kfree(buf);
	}
	userptr_t argv[argc];

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		kprintf("\nERROR\n");
		*err = result;
		V(sem_exec);
		return -1;
	}

	/* Set up stdin/stdout/stderr if necessary. */
	if (curproc->p_filetable == NULL) {
		curproc->p_filetable = filetable_create();
		if (curproc->p_filetable == NULL) {
			*err = ENOMEM;
			goto err;
		}

		result = open_stdfds("con:", "con:", "con:");
		if (result) {
			*err = result;
			goto err;
		}
	}

	
	int i;
	unsigned long total_len = 0, str_size;	
	l -= (argc - 1) * sizeCharPointer;
	char *args_buffer = kmalloc(l);
	
	if(l > 0 && args_buffer == NULL) {
		*err = ENOMEM;
		goto err;
	}

	// copy in arg strings from user to kernel
	for(i = 0; i < argc; i++) {
		str_size = strlen(args[i]) + 1;
		// copy string into kernel
		result = copyinstr((const_userptr_t)args[i], args_buffer + total_len, str_size, NULL);
		if(result) {
			*err = result;
			kfree(args_buffer);
			goto err1;
		}
		total_len += str_size + 1;
	}
	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		*err = ENOMEM;
		goto err;
	}
	as_destroy(curproc->p_addrspace);

	/* Switch to it and activate it. */
	proc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		*err = result;
		goto err;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		*err = result;
		V(sem_exec);
		return -1;
	}
	

	total_len = 0;	
	unsigned long offset = 0;
	// copy out arg strings from kernel to user stack
	for(i = 0; i < argc; i++) {
		str_size = strlen(args_buffer + offset) + 1;
		total_len += str_size; 
		
	//	if(str_size + (total_len % PAGE_SIZE) > PAGE_SIZE)
	//		total_len += total_len % PAGE_SIZE;
		
		// place string into user stack * stack grows downward
		argv[i] = (userptr_t) stackptr - total_len;	// keep track of string start
		result = copyoutstr(args_buffer+offset, (userptr_t) stackptr - total_len, str_size, NULL);
		if(result) {
			*err = result;
			goto err2;
		}
		offset += str_size + 1;
	}

	kfree(args_buffer);

	// pad so arg pointers are aligned by size of char * and make sure stackptr starts at
	// 8 byte aligned address
	
	unsigned long pad;
	pad = ((stackptr - total_len) - ((argc + 1) * sizeCharPointer)) % 8;
	stackptr = (stackptr - total_len) - pad;
	stackptr -= sizeCharPointer;
	argv[i] = NULL;
	kprintf("\nhi\n");
	result = copyout((const void *) &argv[i], (userptr_t) stackptr, sizeCharPointer);
	if(result) {
		*err = result;
		V(sem_exec);
		kfree(args_buffer);
		return -1;
	}

	kprintf("\nhi\n");
	// copy out argv (pointers to the string arguments)
	for(i = argc - 1; i >= 0; i--) {
		kprintf("i %d\n", i);
		stackptr -= sizeCharPointer;
                result = copyout((const void *)&argv[i], (userptr_t)stackptr, sizeCharPointer);
                if(result) {
                        *err = result;
                        V(sem_exec);
                        return -1;
                }
	}
	V(sem_exec);
	/* Warp to user mode. */
	enter_new_process(argc, (userptr_t)stackptr /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");

	*err = EINVAL;
	return -1;

	err2:
		kfree(args_buffer);

		V(sem_exec);
		return -1;

	err1:
		kfree(args_buffer);

	err:
		vfs_close(v);
		V(sem_exec);
		return -1;
}

