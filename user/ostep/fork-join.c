#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include "common_threads.h"

sem_t s;

void *child([[maybe_unused]] void *arg)
{
    sleep(2);
    printf("child\n");
    Sem_post(&s);
    return nullptr;
}

int main()
{
    Sem_init(&s, 0);
    printf("parent: begin\n");
    pthread_t p;
    Pthread_create(&p, nullptr, child, nullptr);
    Sem_wait(&s);
    printf("parent: end\n");
    return 0;
}
