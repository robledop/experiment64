#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "common_threads.h"

// Homework Semaphores 3:
// Now go one step further by implementing a general solution to barrier syn-
// chronization. Assume there are two points in a sequential piece of code,
// called P1 and P2 . Putting a barrier between P1 and P2 guarantees that all
// threads will execute P1 before any one thread executes P2 . Your task: write
// the code to implement a barrier() function that can be used in this man-
// ner. It is safe to assume you know N (the total number of threads in the
// running program) and that all N threads will try to enter the barrier. Again,
// you should likely use two semaphores to achieve the solution, and some
// other integers to count things. See barrier.c for details.

// If done correctly, each child should print their "before" message
// before either prints their "after" message. Test by adding sleep(1)
// calls in various locations.

sem_t s1, s2;

void *child_1(void *arg)
{
    printf("child 1: before\n");
    // what goes here?
    printf("child 1: after\n");
    return nullptr;
}

void *child_2(void *arg)
{
    printf("child 2: before\n");
    // what goes here?
    printf("child 2: after\n");
    return nullptr;
}

int main(int argc, char *argv[])
{
    pthread_t p1, p2;
    printf("parent: begin\n");
    // init semaphores here
    pthread_create(&p1, nullptr, child_1, nullptr);
    pthread_create(&p2, nullptr, child_2, nullptr);
    pthread_join(p1, nullptr);
    pthread_join(p2, nullptr);
    printf("parent: end\n");
    return 0;
}

