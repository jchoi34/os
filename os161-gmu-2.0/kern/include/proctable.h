#include <types.h>
#include <array.h>
#include <limits.h>

extern struct array *process_table;
extern struct lock *getpid_lock;
extern struct array *lock_table;
extern struct array *cv_table;
extern struct array *status_table;
