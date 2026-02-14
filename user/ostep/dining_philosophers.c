#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int num_loops;
    int thread_id;
} arg_t;

static bool parse_nonnegative_int(const char *text, int *out)
{
    if (!text || !out || *text == '\0')
        return false;
    errno = 0;
    char *end = nullptr;
    const long value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 0 || value > INT_MAX)
        return false;
    *out = (int)value;
    return true;
}

sem_t forks[5];
sem_t print_lock;

void space(int s)
{
    sem_wait(&print_lock);
    int i;
    for (i = 0; i < s * 10; i++)
        printf(" ");
}

void space_end()
{
    sem_post(&print_lock);
}

int left(int p)
{
    return p;
}

int right(int p)
{
    return (p + 1) % 5;
}

void get_forks(int p)
{
    if (p == 4) {
        space(p);
        printf("4 try %d\n", right(p));
        space_end();
        sem_wait(&forks[right(p)]);
        space(p);
        printf("4 try %d\n", left(p));
        space_end();
        sem_wait(&forks[left(p)]);
    } else {
        space(p);
        printf("try %d\n", left(p));
        space_end();
        sem_wait(&forks[left(p)]);
        space(p);
        printf("try %d\n", right(p));
        space_end();
        sem_wait(&forks[right(p)]);
    }
}

void put_forks(int p)
{
    sem_post(&forks[left(p)]);
    sem_post(&forks[right(p)]);
}

void think()
{
}

void eat()
{
}

void *philosopher(void *arg)
{
    arg_t *args = (arg_t *)arg;

    space(args->thread_id);
    printf("%d: start\n", args->thread_id);
    space_end();

    int i;
    for (i = 0; i < args->num_loops; i++) {
        space(args->thread_id);
        printf("%d: think\n", args->thread_id);
        space_end();
        think();
        get_forks(args->thread_id);
        space(args->thread_id);
        printf("%d: eat\n", args->thread_id);
        space_end();
        eat();
        put_forks(args->thread_id);
        space(args->thread_id);
        printf("%d: done\n", args->thread_id);
        space_end();
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: dining_philosophers <num_loops>\n");
        exit(1);
    }
    int num_loops = 0;
    if (!parse_nonnegative_int(argv[1], &num_loops)) {
        fprintf(stderr, "invalid num_loops: %s\n", argv[1]);
        exit(1);
    }
    printf("dining: started\n");

    int i;
    for (i = 0; i < 5; i++)
        sem_init(&forks[i], 1);
    sem_init(&print_lock, 1);

    pthread_t p[5];
    arg_t a[5];
    for (i = 0; i < 5; i++) {
        a[i].num_loops = num_loops;
        a[i].thread_id = i;
        pthread_create(&p[i], nullptr, philosopher, &a[i]);
    }

    for (i = 0; i < 5; i++)
        pthread_join(p[i], nullptr);

    printf("dining: finished\n");
    return 0;
}
