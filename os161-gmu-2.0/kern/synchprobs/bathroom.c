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

#include <types.h>
#include <lib.h>
#include <clock.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define NPEOPLE 20

int count;	// 3 max
int turn; // 0 = men's turn; 1 = female's turn
int done; // 3 max
volatile int numMen;
volatile int numFemale;
struct lock *turncount_lock;
struct cv *waiting_male;
struct cv *waiting_female;

static
	void
shower()
{
	// The thread enjoys a refreshing shower!
	clocksleep(1);
}

// Boy will get in line and if it's the first person in line then 3 or less boys will go first then 3 or less females will go next (after atleast 3 boys go it's the females' turns). If the boy gets in line while it's the girls turn (first person in line was a girl or 3 boys went in already) the boy will wait until he is woken up by someone else (may be woken up by a girl or a boy). As a boy gets out if he's the last boy out of three boys to have gone to the bathroom or if there's no other boys left but him (in the bathroom and in line) he will try to notify three girls to wake up and go enter the bathroom. If there's no girls waiting in line he will just try to tell another boy to go in there.
static
	void
boy(void *p, unsigned long which)
{
	(void)p;
	numMen++;
	lock_acquire(turncount_lock);
	kprintf("boy #%ld starting\n", which);
	if(turn == -1) {
		turn = man;
	}
	else if(turn == woman)
		cv_wait(waiting_male, turncount_lock);

	if(count == 3)
		cv_wait(waiting_male, turncount_lock);
	count++;
	lock_release(turncount_lock);
	kprintf("boy #%ld entering bathroom...\n", which);
	shower();
	kprintf("boy #%ld leaving bathroom\n", which);
	lock_acquire(turncount_lock);
	numMen--;
	if(++done % 3 == 0 || numMen == 0){
		count = 0; done = 0; turn = woman;
		cv_signal(waiting_female, turncount_lock);
		cv_signal(waiting_female, turncount_lock);
		cv_signal(waiting_female, turncount_lock);
	}
	else if(numFemale == 0) {
		cv_signal(waiting_male, turncount_lock);
	}
	lock_release(turncount_lock);
}

// Girl will get in line and if it's the first person in line then 3 or less girls will go first then 3 or less boys will go next (after atleast 3 girls go it's the boys' turns). If the girl gets in line while it's the boys turn (first person in line was a boy or 3 girls went in already) the girl will wait until she is woken up by someone else (may be woken up by a girl or a boy). As a girl gets out if she's the last girl out of three girls to have gone to the bathroom or if there's no other girls left but her (in the bathroom and in line) she will try to notify three boys to wake up and go enter the bathroom. If there's no boys waiting in line she will just try to tell another girl to go in there.
static
	void
girl(void *p, unsigned long which)
{
	(void)p;
	numFemale++;
	lock_acquire(turncount_lock);
	kprintf("girl #%ld starting\n", which);
	if(turn == -1)
		turn = woman;
	else if(turn == man)
		cv_wait(waiting_female, turncount_lock);

	if(count == 3)
		cv_wait(waiting_female, turncount_lock);
	count++;
	lock_release(turncount_lock);
	kprintf("girl #%ld entering bathroom\n", which);
	shower();
	kprintf("girl #%ld leaving bathroom\n", which);
	lock_acquire(turncount_lock);
	numFemale--;
	if(++done % 3 == 0 || numFemale == 0){
		count = 0; done = 0; turn = man;
		cv_signal(waiting_male, turncount_lock);
		cv_signal(waiting_male, turncount_lock);
		cv_signal(waiting_male, turncount_lock);
	}
	else if(numMen == 0) {
		cv_signal(waiting_female, turncount_lock);
	}
	lock_release(turncount_lock);
}


// If a girl gets in line first it's the girls' turns otherwise if a boy gets in line first it's the boys' turns first. 3 or less people will go depending on which gender's turn it is and after those 3 people have gone it will be the next gender's turn to go. It will keep alternating between genders and no more than 3 people of a certain gender can go in a row unless there's only one gender type left (E.G., only 4 boys left to go). However, if a person of the opposite gender showed up in line then it will try to make 3 or less people of that gender go first.
// This could be changed for allowing more than 1 person of a certain gender in the bathroom if there's already been 3 people of that gender who have gone and there's no one of the opposite gender waiting in line. E.G., only 6 boys in line but after the first 3 have gone the other 3 would have to go in one at a time. The scenario has an equal amount of girls and boys so for now it would be okay.  
	int
bathroom(int nargs, char **args)
{

	int i, err=0;
	count = 0;	// 3 max
	turn = -1; // 0 = men's turn; 1 = female's turn
	done = 0; // 3 max
	numMen = 0;
	numFemale = 0;

	(void)nargs;
	(void)args;
	turncount_lock = lock_create("lock");
	waiting_male = cv_create("males");
	waiting_female = cv_create("females");

	for (i = 0; i < NPEOPLE; i++) {
		switch(i % 2) {
			case 0:
				err = thread_fork("Boy Thread", NULL,
						boy, NULL, i);
				break;
			case 1:
				err = thread_fork("Girl Thread", NULL,
						girl, NULL, i);
				break;
		}
		if (err) {
			panic("bathroom: thread_fork failed: %s)\n",
					strerror(err));
		}
	}


	return 0;
}

