#include <types.h>
#include <current.h>

pid_t sys_getpid(void){
        return curproc->pid;
}
