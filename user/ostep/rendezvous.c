#include <stdio.h>
#include <unistd.h>
#include "common_threads.h"

// Homework Semaphores 2
// Let’s now generalize this a bit by investigating the rendezvous problem.
// The problem is as follows: you have two threads, each of which are about
// to enter the rendezvous point in the code. Neither should exit this part of
// the code before the other enters it. Consider using two semaphores for this
// task, and see rendezvous.c for details.

// If done correctly, each child should print their "before" message
// before either prints their "after" message. Test by adding sleep(1)
// calls in various locations.

sem_t s1, s2;

void *child_1(void *arg) {
    printf("child 1: before\n");
    // what goes here?
    printf("child 1: after\n");
    return NULL;
}

void *child_2(void *arg) {
    printf("child 2: before\n");
    // what goes here?
    printf("child 2: after\n");
    return NULL;
}

int main(int argc, char *argv[]) {
    pthread_t p1, p2;
    printf("parent: begin\n");
    // init semaphores here
    Pthread_create(&p1, NULL, child_1, NULL);
    Pthread_create(&p2, NULL, child_2, NULL);
    Pthread_join(p1, NULL);
    Pthread_join(p2, NULL);
    printf("parent: end\n");
    return 0;
}

