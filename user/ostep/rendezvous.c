#include <semaphore.h>
#include <stdio.h>
#include <unistd.h>

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
