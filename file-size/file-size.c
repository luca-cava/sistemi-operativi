#include "lib/lib-misc.h"
#include <pthread.h>
#include <semaphore.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <linux/limits.h>

#define BUFFER_CAPACITY 10

typedef struct
{
    char pathfile[PATH_MAX];
    unsigned long size;
    int done;

    sem_t sem_r, sem_w;
} stat_pair;

typedef struct
{
    char pathname[BUFFER_CAPACITY][PATH_MAX];
    int index_in, index_out;

    int done;
    int size;

    // strumenti sincronizzazione e mutua esclusione
    pthread_mutex_t lock;
    sem_t empty;
    sem_t full;
} shared;

typedef struct
{
    // dati privati
    pthread_t tid;
    unsigned int thread_n;
    char *path;

    // dati condivisi
    shared *sh;
    stat_pair *st;
} thread_data;

void dir_thread(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    DIR *dp;
    struct dirent *entry;
    struct stat sb;

    char path_file[PATH_MAX];

    // apro la directory
    if ((dp = opendir(td->path)) == NULL)
        exit_with_sys_err("opendir");

    printf("[D-%u] scansione della cartella '%s'\n", td->thread_n, td->path);

    // itero la cartella
    while ((entry = readdir(dp)))
    {
        snprintf(path_file, PATH_MAX, "%s/%s", td->path, entry->d_name);

        if (lstat(path_file, &sb) == -1)
            exit_with_sys_err("lstat");

        // verifico se il file è regolare
        if (S_ISREG(sb.st_mode))
        {
            printf("[D-%u] trovato il '%s' in %s\n", td->thread_n, entry->d_name, td->path);

            if ((err = sem_wait(&td->sh->empty)) != 0)
                exit_with_err("sem_wait", err);

            if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
                exit_with_err("pthread_thread_lock", err);

            td->sh->index_in = (td->sh->index_in + 1) % BUFFER_CAPACITY;
            td->sh->size++;

            strncpy(td->sh->pathname[td->sh->index_in], path_file, PATH_MAX);

            if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
                exit_with_err("pthread_mutex_unlock", err);

            if ((err = sem_post(&td->sh->full)) != 0)
                exit_with_err("sem_post", err);
        }
    }

    if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    td->sh->done++;

    if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    closedir(dp);
}

void stat_thread(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    char *filepath;
    struct stat sb;

    int done = 0;

    while (!done)
    {
        if ((err = sem_wait(&td->sh->full)) != 0)
            exit_with_err("sem_wait", err);

        if ((err = sem_wait(&td->st->sem_w)) != 0)
            exit_with_err("sem_wait", err);

        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        td->sh->index_out = (td->sh->index_out + 1) % BUFFER_CAPACITY;
        td->sh->size--;

        if (td->sh->done == (td->thread_n - 1) && td->sh->size == 0)
        {
            td->st->done = 1;
            done = 1;
        }

        filepath = td->sh->pathname[td->sh->index_out];

        if (lstat(filepath, &sb) == -1)
            exit_with_sys_err(filepath);

        printf("[STAT] il file '%s' ha dimensione %lu byte\n", filepath, sb.st_size);

        strncpy(td->st->pathfile, filepath, PATH_MAX);
        td->st->size = sb.st_size;

        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);

        if ((err = sem_post(&td->st->sem_r)) != 0)
            exit_with_err("sem_post", err);

        if ((err = sem_post(&td->sh->empty)) != 0)
            exit_with_err("sem_post", err);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage : %s <dir-1> <dir-2> ... <dir-n>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int err;

    thread_data td[argc];
    shared *sh = malloc(sizeof(shared));
    stat_pair *st = malloc(sizeof(stat));

    // init shared
    sh->index_in = 0;
    sh->index_out = 0;
    sh->done = 0;
    sh->size = 0;

    // init stat
    sh->done = 0;

    // init mutex
    if ((err = pthread_mutex_init(&sh->lock, NULL)) != 0)
        exit_with_err("pthread_mutex_init", err);

    // init semaphore
    if ((err = sem_init(&sh->empty, 0, BUFFER_CAPACITY)) != 0)
        exit_with_err("sem_init", err);

    if ((err = sem_init(&sh->full, 0, 0)) != 0)
        exit_with_err("sem_init", err);

    if ((err = sem_init(&st->sem_w, 0, 1)) != 0)
        exit_with_err("sem_init", err);

    if ((err = sem_init(&st->sem_r, 0, 0)) != 0)
        exit_with_err("sem_init", err);

    // D-i
    for (int i = 0; i < argc - 1; i++)
    {
        td[i].sh = sh;
        td[i].thread_n = i + 1;
        td[i].path = argv[i + 1];

        if ((err = pthread_create(&td[i].tid, 0, (void *)dir_thread, &td[i])) != 0)
            exit_with_err("pthread_create", err);
    }

    unsigned long total_bytes = 0;

    while(1)
    {
        if((err = sem_wait(&st->sem_r)) != 0)
            exit_with_err("sem_wait", err);

        total_bytes = total_bytes + st->size;

        if(st->done)
            break;
        else
            printf("[MAIN] con il file '%s' il totale parziale è di %lu\n", st->pathfile, total_bytes);

        if((err = sem_post(&st->sem_w)) != 0)
            exit_with_err("sem_post", err);
    }

    printf("[MAIN] il totale finale è di %lu byte\n", total_bytes);

    // STAT
    td[argc - 1].sh = sh;
    td[argc - 1].thread_n = argc;

    if ((err = pthread_create(&td[argc - 1].tid, 0, (void *)stat_thread, &td[argc - 1])) != 0)
        exit_with_err("pthread_create", err);

    // join
    for (int i = 0; i < argc; i++)
    {
        if ((err = pthread_join(td[i].tid, NULL)) != 0)
            exit_with_err("pthread_join", err);
    }

    // destroy
    pthread_mutex_destroy(&sh->lock);
    sem_destroy(&sh->empty);
    sem_destroy(&sh->full);
    sem_destroy(&st->sem_r);
    sem_destroy(&st->sem_w);
    free(st);
    free(sh);

    exit(EXIT_SUCCESS);
}