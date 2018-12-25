/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009, 2014
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than runprogram() does.
 */

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
#include <filetable.h>
#include <spl.h>
#include <syscall.h>
#include <test.h>
#include <copyinout.h>
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
runprogram(char *progname, char **args, unsigned long argc)
{
	splhigh();
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
	int sizeCharPointer = sizeof(char *);
	long int i;

	// check args size limit not reached
	if(argc > 1) {
		unsigned long l = 0;
		char *buf = kmalloc(sizeCharPointer);
		if(buf == NULL) { 
			spl0();	
			V(sem_runproc);
			return ENOMEM;
		}
		for(i = 0; i < (long int) argc; i++) {
			l += strlen(args[i]) + 1 + sizeCharPointer;		
			if(l + (l % 4) + ((l + (l % 4)) % 8)  > ARG_MAX) {
				kfree(buf);
				spl0();
				V(sem_runproc);
				return E2BIG;
			}
		}

		kfree(buf);
	}
	userptr_t argv[argc];
	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		spl0();
		V(sem_runproc);
		return result;
	}

	/* We should be a new process. */
	KASSERT(proc_getas() == NULL);

	/* Set up stdin/stdout/stderr if necessary. */
	if (curproc->p_filetable == NULL) {
		curproc->p_filetable = filetable_create();
		if (curproc->p_filetable == NULL) {
			vfs_close(v);
			spl0();
			V(sem_runproc);
			return ENOMEM;
		}

		result = open_stdfds("con:", "con:", "con:");
		if (result) {
			vfs_close(v);
			spl0();
			V(sem_runproc);
			return result;
		}
	}

	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		vfs_close(v);
		spl0();
		V(sem_runproc);
		return ENOMEM;
	}

	char **args_buf;
	if(argc > 1) {
		args_buf = kmalloc(sizeof(char **));
		if(args_buf == NULL) {
			vfs_close(v);
			spl0();
			V(sem_runproc);
			return ENOMEM;
		}

		for(i = 0; i < (signed long) argc; i++)	{
			args_buf[i] = (char *) kmalloc(strlen(args[i]) + 1);
			if(args_buf[i] == NULL) {
				int j;
				for(j = 0; j < i; j++)
					kfree(args_buf[j]);
				kfree(args_buf);
				vfs_close(v);
				spl0();
				V(sem_runproc);
				return ENOMEM;
			}
			strcpy(args_buf[i], args[i]);
		}
	}
	/* Switch to it and activate it. */
	proc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		spl0();
		goto err;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		spl0();
		goto err;
	}

	unsigned long total_len = 0, str_size;

	if(argc > 1) {
		// copy out strings to user stack
		for(i = 0; i < (long int) argc; i++) {
			str_size = strlen(args_buf[i]) + 1;
			total_len += str_size; 
			argv[i] = (userptr_t) stackptr - total_len;
			result = copyoutstr((const char*)args_buf[i], argv[i], str_size, NULL);
			if(result){ 
				spl0();
				goto err;
			}
		}


		unsigned long pad = ((stackptr - total_len) - ((argc + 1) * sizeCharPointer)) % 8;
		stackptr = (stackptr - total_len) - pad;
		stackptr -= sizeCharPointer;
		result = copyout((const void *) &argv[i], (userptr_t) stackptr, sizeCharPointer);

		if(result) {
			spl0();
			goto err;
		}
		// copy out pointers to the strings to user stack
		for(i = argc - 1; i >= 0; i--) {
			stackptr -= sizeCharPointer;
			result = copyout((const void *)&argv[i], (userptr_t) stackptr, sizeCharPointer);
			if(result) {
				spl0();
				goto err;
			}

		}
		for(i = 0; i < (signed int)argc; i++)
			kfree(args_buf[i]);
		kfree(args_buf);
	}
	spl0();
	if(argc == 1) {
		/* Warp to user mode. */
		enter_new_process(argc /*argc*/, NULL /*userspace addr of argv*/,
				NULL /*userspace addr of environment*/,
				stackptr, entrypoint);
	}
	else {
		enter_new_process(argc /*argc*/, (userptr_t) stackptr /*userspace addr of argv*/,
				NULL /*userspace addr of environment*/,
				stackptr, entrypoint);
	}
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;

err:
	if(argc > 1) {
		for(i = 0; i < (signed int) argc; i++)
			kfree(args_buf[i]);
		kfree(args_buf);
	}
	V(sem_runproc);
	return result;
}

