#include <stdio.h>
#include <unistd.h>
#include "common_threads.h"

// Homework Semaphores 2
// Let’s now generalize this a bit by investigating the rendezvous problem.
// The problem is as follows: you have two threads, each of which are about
// to enter the rendezvous point in the code. Neither should exit this part of
// the code before the other enters it. Consider using two semaphores for this
// task

// If done correctly, each child should print their "before" message
// before either prints their "after" message. Test by adding sleep(1)
// calls in various locations.

sem_t s1, s2;
pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;

void *child_1([[maybe_unused]] void *arg)
{
    sleep(1);
    pthread_mutex_lock(&print_lock);
    printf("child 1: before\n");
    pthread_mutex_unlock(&print_lock);
    sleep(1);

    Sem_post(&s1);
    Sem_wait(&s2);

    sleep(1);
    pthread_mutex_lock(&print_lock);
    printf("child 1: after\n");
    pthread_mutex_unlock(&print_lock);
    sleep(1);
    return nullptr;
}

void *child_2([[maybe_unused]] void *arg)
{
    sleep(1);
    pthread_mutex_lock(&print_lock);
    printf("child 2: before\n");
    pthread_mutex_unlock(&print_lock);
    sleep(1);

    Sem_post(&s2);
    Sem_wait(&s1);

    sleep(1);
    pthread_mutex_lock(&print_lock);
    printf("child 2: after\n");
    pthread_mutex_unlock(&print_lock);
    sleep(1);
    return nullptr;
}

int main()
{
    pthread_t p1, p2;
    pthread_mutex_lock(&print_lock);
    printf("parent: begin\n");
    pthread_mutex_unlock(&print_lock);

    Sem_init(&s1, 0);
    Sem_init(&s2, 0);

    Pthread_create(&p1, nullptr, child_1, nullptr);
    Pthread_create(&p2, nullptr, child_2, nullptr);
    Pthread_join(p1, nullptr);
    Pthread_join(p2, nullptr);
    pthread_mutex_lock(&print_lock);
    printf("parent: end\n");
    pthread_mutex_unlock(&print_lock);
    return 0;
}
