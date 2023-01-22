#include "threadtools.h"
#include <stdlib.h>
#include <stdio.h> 
#include <poll.h>
/*
 * Print out the signal you received.
 * If SIGALRM is received, reset the alarm here.
 * This function should not return. Instead, call longjmp(sched_buf, 1).
 */
void sighandler(int signo) {
    // TODO
    if(signo == SIGALRM) {
        printf("caught SIGALRM\n");
        alarm(timeslice);
    } else  if(signo == SIGTSTP) {
        printf("caught SIGTSTP\n");
    }
    sigprocmask(SIG_SETMASK, &base_mask, NULL);
    longjmp(sched_buf, 1);
}

/*
 * Prior to calling this function, both SIGTSTP and SIGALRM should be blocked.
 */
void scheduler() {
    int from = setjmp(sched_buf);
    // TODO move ready thread from waiting queue to ready queue
    if(from != 0) {
        struct pollfd*pfds = (struct pollfd*)malloc(sizeof(struct pollfd) * wq_size);
        struct tcb** new_wq = (struct tcb**)malloc(sizeof(struct tcb*) * wq_size);
        int new_wq_size = 0;
        for(int i=0; i<wq_size; i++) {
            pfds[i].fd = waiting_queue[i]->fd;
            pfds[i].events = POLLIN;
            pfds[i].revents = 0;
            new_wq[i] = NULL;
        }
        int readyN = poll(pfds, wq_size, 0);
        // test ready
        for(int i=0; i<wq_size; i++) {
            if(pfds[i].revents & POLLIN) {// ready , push it to reqdy queue
                ready_queue[rq_size] = waiting_queue[i];
                rq_size++;
            } else { // still in waiting_queue, push it into new waiting_queue
                new_wq[new_wq_size] = waiting_queue[i];
                new_wq_size++;
            }
            waiting_queue[i] = NULL;
        }
        for(int i=0; i<new_wq_size; i++) {
            waiting_queue[i] = new_wq[i];
        }
        wq_size = new_wq_size;
        free(pfds);
        free(new_wq);
    }
    if(from == 1) {// from sighandler(yield)
        rq_current = (rq_current == rq_size - 1 ? 0 : rq_current + 1);  
    } else if(from == 2 || from == 3) {// from async read
        if(from == 3)
            free(RUNNING);// if from exit free the memory
        else {
            waiting_queue[wq_size] = RUNNING;
            wq_size++;
        }
        RUNNING = ready_queue[rq_size-1];
        ready_queue[rq_size - 1] = NULL;
        if(rq_current == rq_size - 1) 
            rq_current = 0;
        rq_size--;
        if(rq_size <= 0) {// if ready_queue is empty
            if(wq_size <= 0) // all jobs are done
                return;
            struct pollfd*pfds = (struct pollfd*)malloc(sizeof(struct pollfd) * wq_size);
            struct tcb** new_wq = (struct tcb**)malloc(sizeof(struct tcb*) * wq_size);
            int new_wq_size = 0;
            for(int i=0; i<wq_size; i++) {
                pfds[i].fd = waiting_queue[i]->fd;
                pfds[i].events = POLLIN;
                pfds[i].revents = 0;
                new_wq[i] = NULL;
            }
            int readyN = poll(pfds, wq_size, -1); // wait forever
                                                  // test ready
            for(int i=0; i<wq_size; i++) {
                if(pfds[i].revents & POLLIN) {// ready , push it to reqdy queue
                    ready_queue[rq_size] = waiting_queue[i];
                    rq_size++;
                } else { // still in waiting_queue, push it into new waiting_queue
                    new_wq[new_wq_size] = waiting_queue[i];
                    new_wq_size++;
                }
                waiting_queue[i] = NULL;
            }
            for(int i=0; i<new_wq_size; i++) {
                waiting_queue[i] = new_wq[i];
            }
            wq_size = new_wq_size;
            free(pfds);
            free(new_wq);
        }
    } 
    longjmp(RUNNING->environment, 1);
}
