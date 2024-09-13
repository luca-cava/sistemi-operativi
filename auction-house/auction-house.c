#include "lib/lib-misc.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

#define NAME_SIZE 50
#define LINE_SIZE 100

typedef struct
{
    char name[NAME_SIZE];
    int minimum_offer;
    int maximum_offer;
    int offer;

    int num_asta;
    int id_richiedente;

    // cond
    int start;
    int done;

    // strumenti per la sincronizzazione e la mutua esclusione
    pthread_mutex_t lock;
    pthread_cond_t cond_j;
    pthread_cond_t cond_b;
} shared;

typedef struct
{
    // dati privati
    pthread_t tid;
    unsigned thread_n;
    int offerte_fatte;

    // dati condivisi
    shared *sh;
} thread_data;

void thread_function(void *args)
{
    int err;
    int r;
    thread_data *td = (thread_data *)args;

    // ottengo il lock della struttura dati condivisa
    if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    printf("[B%u] avviato e pronto\n", td->thread_n + 1);

    td->sh->start = td->sh->start - 1;

    // sveglio il thread J
    if ((err = pthread_cond_signal(&td->sh->cond_j)) != 0)
        exit_with_err("pthread_cond_signal", err);

    // rilascio il lock
    if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    while (1)
    {
        // ottengo il lock della struttura dati condivisa
        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        // verifico le condizioni di operabilità
        while (td->offerte_fatte >= td->sh->num_asta || td->sh->done != 1)
        {
            //printf("[B%u] aspetto perche' offerte_fatte %d > num_asta %d\n", td->thread_n + 1, td->offerte_fatte, td->sh->num_asta);
            if ((err = pthread_cond_wait(&td->sh->cond_b, &td->sh->lock)) != 0)
                exit_with_err("pthread_cond_wait", err);
        }

        
        td->offerte_fatte++;
        td->sh->id_richiedente = td->thread_n+1;

        int max = td->sh->maximum_offer;
        r = (rand() % max )+ 1;
        td->sh->offer = r;

        printf("[B%u] invio offerta di %d EUR per asta n.%d\n", td->thread_n + 1, td->sh->offer, td->sh->num_asta);

        td->sh->start = td->sh->start - 1;

        // sveglio il thread J
        if ((err = pthread_cond_signal(&td->sh->cond_j)) != 0)
            exit_with_err("pthread_cond_signal", err);

        // rilascio il lock
        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);
    }
}

int main(int argc, char **argv)
{
    srand(time(NULL));
    int err;

    if (argc < 2)
    {
        printf("Usage %s <auction-file> <num-bidders>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *input_file = argv[1];
    int bidders = atoi(argv[2]);

    thread_data td[bidders];
    shared *sh = malloc(sizeof(shared));

    // apro il file in sola lettura
    FILE *f = fopen(input_file, "r");
    if (f == NULL)
    {
        fprintf(stderr, "File non valido");
        exit(EXIT_FAILURE);
    }

    // init shared
    sh->start = bidders;
    sh->num_asta = 1;
    sh->done = 0;

    // init mutex
    if ((err = pthread_mutex_init(&sh->lock, NULL)) != 0)
        exit_with_err("pthread_mutex_init", err);

    // init var cond
    if ((err = pthread_cond_init(&sh->cond_j, NULL)) != 0)
        exit_with_err("pthread_cond_init", err);

    if ((err = pthread_cond_init(&sh->cond_b, NULL)) != 0)
        exit_with_err("pthread_cond_init", err);

    // B-i
    for (int i = 0; i < bidders; i++)
    {
        td[i].sh = sh;
        td[i].thread_n = i;
        td[i].offerte_fatte = 0;

        if ((err = pthread_create(&td[i].tid, 0, (void *)thread_function, &td[i])) != 0)
            exit_with_err("pthread_create", err);
    }

    // ottengo il lock
    if ((err = pthread_mutex_lock(&sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    // verifico le condizioni di operabilità
    while (sh->start != 0)
        if ((err = pthread_cond_wait(&sh->cond_j, &sh->lock)) != 0)
            exit_with_err("pthread_cond_wait", err);

    sh->done = 1;

    // rilascio il look
    if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    printf("[J] Possono iniziare le aste\n");

    // leggo il file riga per riga
    int i = 0;
    char buffer[LINE_SIZE];
    while (fgets(buffer, LINE_SIZE, f))
    {
        // ottengo il lock
        if ((err = pthread_mutex_lock(&sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        strcpy(sh->name, strtok(buffer, ","));
        sh->minimum_offer = atoi(strtok(NULL, ","));
        sh->maximum_offer = atoi(strtok(NULL, ","));
        sh->start = bidders;

        printf("[J] lancio l'asta n.%d per %s con offerta minima di %d EUR e massima di %d EUR\n", sh->num_asta, sh->name, sh->minimum_offer, sh->maximum_offer);

        // sveglio tutti i thread
        if ((err = pthread_cond_broadcast(&sh->cond_b)) != 0)
            exit_with_err("pthread_cond_broadcast", err);

        // rilascio il look
        if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);

        // ottengo il lock
        if ((err = pthread_mutex_lock(&sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        // verifico le condizioni di operabilità
        while (sh->start != 0)
        {
            if ((err = pthread_cond_wait(&sh->cond_j, &sh->lock)) != 0)
                exit_with_err("pthread_cond_wait", err);

            printf("[J] ricevuta offerta da B%d\n", sh->id_richiedente);

            // verificare offerta

            // sveglio tutti i thread
            if ((err = pthread_cond_broadcast(&sh->cond_b)) != 0)
                exit_with_err("pthread_cond_broadcast", err);
        }

        printf("[J] l'asta n.%d per %s si è conclusa\n\n", sh->num_asta, sh->name);
        sh->num_asta++;
        sh->done = 0;

        // rilascio il look
        if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);

        i++;
    }

    // join
    for (int i = 0; i < bidders; i++)
        if ((err = pthread_join(td[i].tid, NULL)) != 0)
            exit_with_err("pthread_join", err);

    // destroy
    pthread_mutex_destroy(&sh->lock);
    pthread_cond_destroy(&sh->cond_j);
    pthread_cond_destroy(&sh->cond_b);
    free(sh);

    // chiudo il file
    fclose(f);
}