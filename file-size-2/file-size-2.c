#include "lib/lib-misc.h"
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <linux/limits.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>

typedef struct __node
{
    unsigned long value;
    struct __node *next;
    struct __node *prev;
} node;

typedef struct
{
    node *head;
    unsigned size;
} list;

void init_list(list *l)
{
    l->head = NULL;
    l->size = 0;
}

void list_insert(list *l, const int value)
{
    node *n = malloc(sizeof(node));
    n->value = value;
    n->prev = NULL;
    n->next = l->head;
    l->head = n;

    if (n->next != NULL)
        n->next->prev = n;

    l->size++;
}

unsigned long extract_min(list *l)
{
    if (l->head == NULL)
        return 0;

    unsigned long min;
    node *min_ptr = l->head;
    node *ptr = l->head->next;

    while (ptr != NULL)
    {
        if (ptr->value < min_ptr->value)
            min_ptr = ptr;

        ptr = ptr->next;
    }

    min = min_ptr->value;

    if (min_ptr->prev != NULL)
        min_ptr->prev->next = min_ptr->next;

    if (min_ptr->next != NULL)
        min_ptr->next->prev = min_ptr->prev;

    if (l->head == min_ptr)
        l->head = l->head->next;

    free(min_ptr);
    l->size--;

    return min;
}

unsigned long extract_max(list *l)
{
    if (l->head == NULL)
        return 0;

    unsigned long max;
    node *max_ptr = l->head;
    node *ptr = l->head->next;

    while (ptr != NULL)
    {
        if (ptr->value > max_ptr->value)
            max_ptr = ptr;

        ptr = ptr->next;
    }

    max = max_ptr->value;

    if (max_ptr->prev != NULL)
        max_ptr->prev->next = max_ptr->next;

    if (max_ptr->next != NULL)
        max_ptr->next->prev = max_ptr->prev;

    if (l->head == max_ptr)
        l->head = l->head->next;

    free(max_ptr);

    l->size--;

    return max;
}

void list_destroy(list *l)
{
    node *ptr = l->head;
    node *tmp;

    while (ptr != NULL)
    {
        tmp = ptr;
        ptr = ptr->next;
        free(tmp);
    }

    free(l);
}

typedef struct
{
    list *l;
    unsigned done;

    // strumenti sincronizzazione e mutua esclusione
    pthread_mutex_t lock;
    pthread_cond_t cond;
} shared;

typedef struct
{
    // dati privati
    pthread_t tid;
    unsigned thread_n;
    unsigned ndir;
    char *dirname;

    // dati condivisi
    shared *sh;
} thread_data;

void dir_thread_function(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    struct dirent *entry;
    struct stat sb;
    char pathfile[PATH_MAX];

    DIR *dp;

    if ((dp = opendir(td->dirname)) == NULL)
        exit_with_sys_err("opendir");

    printf("[D-%u] scansione della cartella '%s'\n", td->thread_n, td->dirname);

    while ((entry = readdir(dp)))
    {
        snprintf(pathfile, PATH_MAX, "%s/%s", td->dirname, entry->d_name);

        if (lstat(pathfile, &sb) == -1)
            exit_with_sys_err(entry->d_name);

        if (S_ISREG(sb.st_mode))
        {
            printf("[D-%u] trovato il file '%s' in %s\n", td->thread_n, entry->d_name, td->dirname);

            if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
                exit_with_err("pthread_mutex_lock", err);

            list_insert(td->sh->l, sb.st_size);

            if ((err = pthread_cond_signal(&td->sh->cond)) != 0)
                exit_with_err("pthread_cond_signal", err);

            if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
                exit_with_err("pthread_mutex_unlock", err);
        }
    }

    if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    td->sh->done++;

    if ((err = pthread_cond_broadcast(&td->sh->cond)) != 0)
        exit_with_err("pthread_cond_broadcast", err);

    if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    closedir(dp);
}

void add_thread_function(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    unsigned long min, max, sum;
    unsigned done = 0;

    while (1)
    {
        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        while (td->sh->l->size < 2 && td->sh->done != td->ndir)
        {
            if ((err = pthread_cond_wait(&td->sh->cond, &td->sh->lock)) != 0)
                exit_with_err("pthread_cond_wait", err);
        }

        if (td->sh->done == td->ndir && td->sh->l->size == 1)
        {
            if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
                exit_with_err("pthread_mutex_unlock", err);

            break;
        }

        min = extract_min(td->sh->l);
        max = extract_max(td->sh->l);
        sum = min + max;

        list_insert(td->sh->l, sum);

        printf("[ADD-%u] il minimo (%lu) ed il massimo (%lu) sono stati sostituiti da %lu; l'insieme ha adesso %u elementi.\n", td->thread_n, min, max, sum, td->sh->l->size);

        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);
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
    unsigned ndir = argc - 1;
    thread_data td[ndir + 2];
    shared *sh = malloc(sizeof(shared));

    // init shared
    sh->done = 0;
    sh->l = malloc(sizeof(list));
    init_list(sh->l);

    // init mutex
    if ((err = pthread_mutex_init(&sh->lock, NULL)) != 0)
        exit_with_err("pthread_mutex_init", err);

    // init var cond
    if ((err = pthread_cond_init(&sh->cond, NULL)) != 0)
        exit_with_err("pthread_cond_init", err);

    // DIR-i
    for (int i = 0; i < ndir; i++)
    {
        td[i].sh = sh;
        td[i].thread_n = i + 1;
        td[i].dirname = argv[i + 1];

        if ((err = pthread_create(&td[i].tid, 0, (void *)dir_thread_function, &td[i])) != 0)
            exit_with_err("pthread_create", err);
    }

    // ADD-j
    for (int i = 0; i < 2; i++)
    {
        td[i + ndir].sh = sh;
        td[i + ndir].thread_n = i + 1;
        td[i + ndir].ndir = ndir;

        if ((err = pthread_create(&td[i + ndir].tid, 0, (void *)add_thread_function, &td[i + ndir])) != 0)
            exit_with_err("pthread_create", err);
    }

    // join
    for (int i = 0; i < ndir + 2; i++)
    {
        if ((err = pthread_join(td[i].tid, NULL)) != 0)
            exit_with_err("pthread_join", err);
    }

    printf("[MAIN] i thread secondari hanno terminato e il totale finale è di %lu byte.\n", sh->l->head->value);

    // destroy
    list_destroy(sh->l);
    pthread_mutex_destroy(&sh->lock);
    pthread_cond_destroy(&sh->cond);
    free(sh);

    exit(EXIT_SUCCESS);
}