#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>

int
main(void)
{
	pid_t pid = fork();
	int x;
	// char *const *args	
	const char *args[3];
	args[0] = "add";
	args[1] = "1";
	args[2] = "2";
	if(pid == 0) {
		execv("testbin/add", (char **) args);
		exit(0);
	} else if(pid > 0) {
		waitpid(pid, &x, 0);
	}
	
	
	return 0;
}
