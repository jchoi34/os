/*
 * Copyright (c) 2001, 2002, 2009
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
 * Driver code for whale mating problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define NMATING 10

struct semaphore *males;
struct semaphore *females;

int count = 0;

struct cv *matecv;

struct semaphore *wakemales;
struct semaphore *wakefemales;

struct lock *malelock;
struct lock *femalelock;
struct lock *matchmakerlock;
struct lock *countlock;

// announce the male's arrival and wait to be woken up by a matchmaker
// after waking up the male is starts mating
static
void
male(void *p, unsigned long which)
{
	(void)p;
	kprintf("male whale #%ld starting\n", which);
	lock_acquire(malelock);
	V(males);	
	P(wakemales);
	kprintf("male whale #%ld mating\n", which);
	lock_acquire(countlock);
	if((count = (count + 1) % 3) == 0){
		cv_broadcast(matecv, countlock);	
	} else{
		cv_wait(matecv, countlock);
	}	
	lock_release(countlock);
	kprintf("male whale #%ld finished\n", which);
	lock_release(malelock);
}

// announce the female's arrival and wait to be woken up by a matchmaker
// after waking up the female is starts mating
static
	void
female(void *p, unsigned long which)
{
	(void)p;
	kprintf("female whale #%ld starting\n", which);
	lock_acquire(femalelock);
	V(females);
	P(wakefemales);
	kprintf("female whale #%ld mating\n", which);
	lock_acquire(countlock);
	if((count = (count + 1) % 3) == 0){
		cv_broadcast(matecv, countlock);	
	} else{
		cv_wait(matecv, countlock);
	}	
	lock_release(countlock);
	kprintf("female whale #%ld finished\n", which);
	lock_release(femalelock);
}

// find a male and then a female and then wake up a male and a female
// after waking up a male and female the matchmaker starts mating
static
	void
matchmaker(void *p, unsigned long which)
{
	(void) p;
	kprintf("matchmaker whale #%ld starting\n", which);
	lock_acquire(matchmakerlock);
	P(males);
	P(females);	
	V(wakemales);
	V(wakefemales);
	kprintf("matchmaker whale #%ld mating\n", which);
	lock_acquire(countlock);
	if((count = (count + 1) % 3) == 0){
		cv_broadcast(matecv, countlock);	
	} else{
		cv_wait(matecv, countlock);
	}	
	lock_release(countlock);
	kprintf("matchmaker whale #%ld finished\n", which);
	lock_release(matchmakerlock);
}


// Change this function as necessary
	int
whalemating(int nargs, char **args)
{
	int i, j, err=0;
	int count = 0;
	males = sem_create("males", 0);
	females = sem_create("females", 0);

	wakemales = sem_create("wakemales", 0);
	wakefemales = sem_create("wakefemales", 0);

	malelock = lock_create("malelock");
	femalelock = lock_create("femalelock");
	matchmakerlock = lock_create("matchmakerlock");
	countlock = lock_create("countlock");

	matecv = cv_create("mating");

	(void)nargs;
	(void)args;

	for (i = 0; i < 3; i++) {
		for (j = 0; j < NMATING; j++) {
			count++;
			switch(i) {
				case 0:
					err = thread_fork("Male Whale Thread",
							NULL, male, NULL, j);
					break;
				case 1:
					err = thread_fork("Female Whale Thread",
							NULL, female, NULL, j);
					break;
				case 2:
					err = thread_fork("Matchmaker Whale Thread",
							NULL, matchmaker, NULL, j);
					break;
			}
			if (err) {
				panic("whalemating: thread_fork failed: %s)\n",
						strerror(err));
			}
		}
	}
	return 0;
}
