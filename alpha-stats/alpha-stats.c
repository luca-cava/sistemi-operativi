#include "lib/lib-misc.h"
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <ctype.h>
#include <fcntl.h>

#define ALFABETO_SIZE 26

typedef enum
{
    AL_N,
    MZ_N,
    PARENT_N
} thread_n;

typedef struct
{
    char c;
    unsigned long stats[ALFABETO_SIZE];
    int done;

    // strumenti per la sincronizzazione e la mutua esclusione
    sem_t sem[3];
} shared;

typedef struct
{
    // dati privati
    pthread_t tid;
    int thread_n;

    // dati condivisi
    shared *sh;
} thread_data;

void thread_function(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    while (1)
    {
        if ((err = sem_wait(&td->sh->sem[td->thread_n])) != 0)
            exit_with_err("sem_wait", err);

        if (td->sh->done == 1)
            break;

        td->sh->stats[td->sh->c - 'a']++;

        if ((err = sem_post(&td->sh->sem[PARENT_N])) != 0)
            exit_with_err("sem_post", err);
    }
}

int main(int argc, char **argv)
{
    int err;

    if (argc < 2)
    {
        printf("Usage %s <file.txt>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    thread_data td[2];
    shared *sh = malloc(sizeof(shared));

    // inizializzo i semafori
    if ((err = sem_init(&sh->sem[PARENT_N], 0, 1)) != 0)
        exit_with_err("sem_init", err);

    if ((err = sem_init(&sh->sem[AL_N], 0, 0)) != 0)
        exit_with_err("sem_init", err);

    if ((err = sem_init(&sh->sem[MZ_N], 0, 0)) != 0)
        exit_with_err("sem_init", err);

    char *map;
    struct stat s_file;

    for (int i = 0; i < 2; i++)
    {
        td[i].sh = sh;
        td[i].thread_n = i;

        if ((err = pthread_create(&td[i].tid, 0, (void *)thread_function, &td[i])) != 0)
            exit_with_err("pthread_create", err);
    }

    int fd;
    // apro il file
    if ((fd = open(argv[1], O_RDONLY)) == -1)
        exit_with_err("open", err);

    // eseguo lo stat del file per conoscere la sua dim
    if ((err = fstat(fd, &s_file)) == -1)
        exit_with_err("fstat", err);

    // mappo il file in memoria in sola lettura
    if ((map = mmap(NULL, s_file.st_size, PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED)
        exit_with_err("mmap", err);

    // chiudo il file
    if ((err = close(fd)) == -1)
        exit_with_err("close", err);

    // leggo il file carattere per carattere
    int i = 0;
    while (i < s_file.st_size)
    {
        if ((map[i] >= 'a' && map[i] <= 'z') || (map[i] >= 'A' && map[i] <= 'Z'))
        {
            if ((err = sem_wait(&sh->sem[PARENT_N])) != 0)
                exit_with_err("sem_wait", err);

            // inserisco il carattere nella struttura dati condivisa
            sh->c = tolower(map[i]);

            if (map[i] <= 'l')
            {
                if ((err = sem_post(&sh->sem[AL_N])) != 0)
                    exit_with_err("sem_post", err);
            }
            else
            {
                if ((err = sem_post(&sh->sem[MZ_N])) != 0)
                    exit_with_err("sem_post", err);
            }
        }

        i++;
    }

    if ((err = sem_wait(&sh->sem[PARENT_N])) != 0)
        exit_with_err("sem_wait", err);

    printf("Statistiche sul file: %s\n", argv[1]);

    for (int i = 0; i < ALFABETO_SIZE; i++)
        printf("%c: %lu\t", i + 'a', sh->stats[i]);

    printf("\n");

    sh->done = 1;
    for (int i = 0; i < 2; i++)
    {
        if ((err = sem_post(&sh->sem[i])) != 0)
            exit_with_err("sem_post", err);
    }

    // join
    for (int i = 0; i < 2; i++)
    {
        if ((err = pthread_join(td[i].tid, NULL)) != 0)
            exit_with_err("pthread_join", err);
    }

    // destroy
    for (int i = 0; i < 3; i++)
        sem_destroy(&sh->sem[i]);

    free(sh);

    // rilascio la mappatuta del file in memoria
    if ((err = munmap(map, s_file.st_size)) == -1)
        exit_with_err("munmap", err);

    exit(EXIT_SUCCESS);
}