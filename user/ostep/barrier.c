#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "common_threads.h"

// Homework Semaphores 3:
// Now go one step further by implementing a general solution to barrier synchronization.
// Assume there are two points in a sequential piece of code,
// called P1 and P2. Putting a barrier between P1 and P2 guarantees that all
// threads will execute P1 before any one thread executes P2. Your task: write
// the code to implement a barrier() function that can be used in this manner.
// It is safe to assume you know N (the total number of threads in the
// running program) and that all N threads will try to enter the barrier. Again,
// you should likely use two semaphores to achieve the solution and some
// other integers to count things.

// If done correctly, each child should print their "before" message
// before either prints their "after" message. Test by adding sleep(1)
// calls in various locations.

sem_t barrier_sem;
pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;
int n                       = 2; // Number of threads
int count                   = 0; // How many threads have arrived

void barrier()
{
    Mutex_lock(&count_mutex);
    count++;
    if (count == n) {
        Sem_post(&barrier_sem);
    }
    Mutex_unlock(&count_mutex);

    Sem_wait(&barrier_sem);
    Sem_post(&barrier_sem);
}

void *child_1([[maybe_unused]] void *arg)
{
    Mutex_lock(&print_mutex);
    printf("child 1: before\n");
    Mutex_unlock(&print_mutex);

    sleep(1);
    barrier(); // All threads much reach this point before any can continue
    sleep(1);

    sleep(1);
    Mutex_lock(&print_mutex);
    printf("child 1: after\n");
    Mutex_unlock(&print_mutex);
    return nullptr;
}

void *child_2([[maybe_unused]] void *arg)
{
    Mutex_lock(&print_mutex);
    printf("child 2: before\n");
    Mutex_unlock(&print_mutex);

    sleep(1);
    barrier(); // All threads much reach this point before any can continue
    sleep(1);

    Mutex_lock(&print_mutex);
    printf("child 2: after\n");
    Mutex_unlock(&print_mutex);
    return nullptr;
}

int main()
{
    pthread_t p1, p2;
    printf("parent: begin\n");

    Sem_init(&barrier_sem, 0);
    Pthread_create(&p1, nullptr, child_1, nullptr);
    Pthread_create(&p2, nullptr, child_2, nullptr);
    Pthread_join(p1, nullptr);
    Pthread_join(p2, nullptr);
    printf("parent: end\n");
    return 0;
}
