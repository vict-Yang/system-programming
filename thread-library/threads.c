#include "threadtools.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

void fibonacci(int id, int arg) {
    thread_setup(id, arg);

    for (RUNNING->i = 1; ; RUNNING->i++) {
        if (RUNNING->i <= 2)
            RUNNING->x = RUNNING->y = 1;
        else {
            /* We don't need to save tmp, so it's safe to declare it here. */
            int tmp = RUNNING->y;  
            RUNNING->y = RUNNING->x + RUNNING->y;
            RUNNING->x = tmp;
        }
        printf("%d %d\n", RUNNING->id, RUNNING->y);
        sleep(1);

        if (RUNNING->i == RUNNING->arg) {
            thread_exit();
        }
        else {
            thread_yield();
        }
    }
}

void collatz(int id, int arg) {
    thread_setup(id, arg);
    RUNNING->x = RUNNING->arg;
    while(RUNNING->x != 1) {
        if(RUNNING->x & 1) {
            RUNNING->x = (RUNNING->x * 3) + 1;
        } else {
            RUNNING->x /= 2;
        }
        printf("%d %d\n", RUNNING->id, RUNNING->x);
        sleep(1);
        if(RUNNING->x == 1) {
            thread_exit();
        } else {
            thread_yield();
        }
    }
}

void max_subarray(int id, int arg) {
    // TODO
    thread_setup(id, arg);
    for(RUNNING->i = 1;; RUNNING->i++) {
        int ret = async_read(5);
        int curN = 0, neg = 1;
        for(int j=0; j<4; j++) {
            if(RUNNING->buf[j] == ' ')
                continue;
            if(RUNNING->buf[j] != '-')
                curN = curN * 10 + (int)(RUNNING->buf[j] - '0');
            else 
                neg = -1;
        }
        curN *= neg;
        RUNNING->x = (RUNNING->x > 0 ? RUNNING->x : 0) + curN;
        if(RUNNING->x > RUNNING->y)
            RUNNING->y = RUNNING->x;
        printf("%d %d\n", RUNNING->id, RUNNING->y);
        sleep(1);
        if(RUNNING->i == RUNNING->arg) {
            thread_exit();
        }
        else {
            thread_yield();
        }
    }
}
