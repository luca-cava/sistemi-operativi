#include "lib/lib-misc.h"
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/limits.h>

#define BUFFER_CAPACITY 10

typedef struct
{
    char buffer[BUFFER_CAPACITY][PATH_MAX];
    int top;

    int exit;
    int done;

    // strumenti sincronizzazione e mutua esclusione
    pthread_mutex_t lock;
    sem_t empty;
    sem_t full;
} shared;

typedef struct
{
    char pathname[PATH_MAX];
    long dimensione;

    int exit;

    // strumenti sincronizzazione e mutua esclusione
    sem_t sem_r;
    sem_t sem_w;
} tot;

typedef struct
{
    // dati privati
    pthread_t tid;
    int thread_n;
    char *input_dir;

    // dati condivisi
    shared *sh;
    tot *t;
} thread_data;

void dir_thread_function(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    DIR *d;
    struct dirent *entry;
    struct stat sb;

    char pathfile[PATH_MAX];

    if ((d = opendir(td->input_dir)) == NULL)
        exit_with_sys_err("opendir");

    printf("[DIR-%d] scansione della cartella '%s'\n", td->thread_n + 1, td->input_dir);

    while ((entry = readdir(d)))
    {
        snprintf(pathfile, PATH_MAX, "%s/%s", td->input_dir, entry->d_name);

        if (lstat(pathfile, &sb) == -1)
            exit_with_sys_err(entry->d_name);

        if (S_ISREG(sb.st_mode))
        {
            printf("[DIR-%d] trovato il file '%s' in '%s'\n", td->thread_n + 1, entry->d_name, td->input_dir);

            if ((err = sem_wait(&td->sh->empty)) != 0)
                exit_with_err("sem_wait", err);

            if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
                exit_with_err("pthread_mutex_lock", err);

            td->sh->top++;
            strncpy(td->sh->buffer[td->sh->top], pathfile, PATH_MAX);

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

    printf("[DIR-%d] scansione della cartella conclusa '%s'\n", td->thread_n + 1, td->input_dir);

    closedir(d);
}

void stati_thread_function(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    struct stat sb;
    char filepath[PATH_MAX];
    int done = 0;

    while(!done)
    {
        if((err = sem_wait(&td->sh->full)) != 0)
            exit_with_err("sem_wait", err);

        if((err = sem_wait(&td->t->sem_w)) != 0)
            exit_with_err("sem_wait", err);

        if((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        strncpy(filepath, td->sh->buffer[td->sh->top], PATH_MAX);
        td->sh->top--;

        if(td->sh->done == (td->thread_n - 1) && td->sh->top == -1)
        {
            td->t->exit = 1;
            done = 1;
        }

        if(lstat(filepath, &sb) == -1)
            exit_with_sys_err(filepath);

        printf("[STAT] il file '%s' ha dimensione %lu byte\n", filepath, sb.st_size);

        strncpy(td->t->pathname, filepath, PATH_MAX);
        td->t->dimensione = sb.st_size;

        if((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);

        if((err = sem_post(&td->t->sem_r)) != 0)
            exit_with_err("sem_post", err);

        if((err = sem_post(&td->sh->empty)) != 0)
            exit_with_err("sem_post", err);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: %s <dir-1> <dir-2> ... <dir-n>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int err;

    thread_data td[argc];
    shared *sh = malloc(sizeof(shared));
    tot *t = malloc(sizeof(tot));

    // init shared
    sh->top = -1;
    sh->exit = 0;
    sh->done = 0;

    // init tot
    t->exit = 0;

    // init mutex
    if ((err = pthread_mutex_init(&sh->lock, NULL)) != 0)
        exit_with_err("pthread_mutex_init", err);

    // init semaphore
    if ((err = sem_init(&sh->empty, 0, BUFFER_CAPACITY)) != 0)
        exit_with_err("sem_init", err);

    if ((err = sem_init(&sh->full, 0, 0)) != 0)
        exit_with_err("sem_init", err);

    if ((err = sem_init(&t->sem_r, 0, 0)) != 0)
        exit_with_err("sem_init", err);

    if ((err = sem_init(&t->sem_w, 0, 1)) != 0)
        exit_with_err("sem_init", err);

    // STAT
    td[argc - 1].sh = sh;
    td[argc - 1].t = t;
    td[argc - 1].thread_n = argc;
    if ((err = pthread_create(&td[argc - 1].tid, 0, (void *)stati_thread_function, &td[argc - 1])) != 0)
        exit_with_err("pthread_create", err);

    // DIR-i
    for (int i = 0; i < argc - 1; i++)
    {
        td[i].sh = sh;
        td[i].thread_n = i;
        td[i].input_dir = argv[i+1];

        if ((err = pthread_create(&td[i].tid, 0, (void *)dir_thread_function, &td[i])) != 0)
            exit_with_err("pthread_create", err);
    }

    long totale = 0;
    // MAIN
    while(1)
    {
        if((err = sem_wait(&t->sem_r)) != 0)
            exit_with_err("sem_wait", err);

        totale = totale + t->dimensione;
        if(t->exit == 1)
            break;
        else
            printf("[MAIN] con il file '%s' il totale parziale è %ld byte\n", t->pathname, totale);

        if((err = sem_post(&t->sem_w)) != 0)
            exit_with_err("sem_post", err);
    }

    // join
    for (int i = 0; i < argc; i++)
        if((err = pthread_join(td[i].tid, NULL)) != 0)
            exit_with_err("pthread_join", err);

    printf("[MAIN] il totale finale è di %ld byte\n", totale);

    // destroy
    pthread_mutex_destroy(&sh->lock);
    sem_destroy(&sh->empty);
    sem_destroy(&sh->full);
    free(sh);

    sem_destroy(&t->sem_r);
    sem_destroy(&t->sem_w);
    free(t);

    exit(EXIT_SUCCESS);
}