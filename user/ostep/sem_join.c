#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <unistd.h>

sem_t s;

void *child([[maybe_unused]] void *arg)
{
    sleep(2);
    printf("child\n");
    sem_post(&s); // signal here: child is done
    return nullptr;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char *argv[])
{
    sem_init(&s, 0);
    printf("parent: begin\n");
    pthread_t c;
    pthread_create(&c, nullptr, child, nullptr);
    sem_wait(&s); // wait here for child
    printf("parent: end\n");
    return 0;
}
