#include "lib/lib-misc.h"
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#define DESCRIPTION 50
#define BUFFER_SIZE 100

typedef enum
{
    BLOCK,
    RECEIVE,
    SEND
} operation;

typedef struct
{
    char description[DESCRIPTION];
    int min_offer;
    int max_offer;
    int current_offer;
    int id_current_bidder;

    int bidders_n;
    operation op;
    int exit;

    // Strumenti per sincronizzazione e mutua esclusione
    pthread_mutex_t lock;
    pthread_cond_t cond_j;
    pthread_cond_t cond_b;
} shared;

typedef struct
{
    // dati privati
    pthread_t tid;
    unsigned int thread_n;

    // dati condivisi
    shared *sh;
} thread_data;

void thread_function(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    td->sh->bidders_n = td->sh->bidders_n - 1;

    // printf("[B%d] pronto\n", td->thread_n + 1);

    // sveglio J
    if ((err = pthread_cond_signal(&td->sh->cond_j)) != 0)
        exit_with_err("pthread_cond_signal", err);

    // printf("[B%d] sveglio J\n", td->thread_n + 1);

    if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    while (1)
    {
        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        while (td->sh->op != SEND && td->sh->exit != 1)
        {
            // printf("[B%d] bloccato\n", td->thread_n + 1);
            if ((err = pthread_cond_wait(&td->sh->cond_b, &td->sh->lock)) != 0)
                exit_with_err("pthread_cond_wait", err);
        }

        // verifico se devo terminare
        if (td->sh->exit == 1)
        {
            if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
                exit_with_err("pthread_mutex_unlock", err);
            break;
        }

        td->sh->current_offer = (rand() % td->sh->max_offer) + 1;
        td->sh->id_current_bidder = td->thread_n + 1;
        printf("[B%d] invio offerta di %d EUR\n", td->thread_n + 1, td->sh->current_offer);

        td->sh->op = RECEIVE;

        if ((err = pthread_cond_signal(&td->sh->cond_j)) != 0)
            exit_with_err("pthread_cond_signal", err);

        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);
    }
}

int parse_line(char *buffer, shared *sh)
{
    char *token;
    if ((token = strtok(buffer, ",")) == NULL)
        return -1;

    strcpy(sh->description, token);

    if ((token = strtok(NULL, ",")) == NULL)
        return -1;

    sh->min_offer = atoi(token);

    if ((token = strtok(NULL, ",")) == NULL)
        return -1;

    sh->max_offer = atoi(token);

    return 0;
}

void check_offer(int *offerte_valide, int *max_current_offer, int *id_max_current_offer, shared *sh)
{
    if (sh->current_offer >= sh->min_offer)
    {
        *offerte_valide++;
        if (sh->current_offer > *max_current_offer)
        {
            *max_current_offer = sh->current_offer;
            *id_max_current_offer = sh->id_current_bidder;
        }
    }
}

int main(int argc, char **argv)
{
    srand(time(NULL));
    if (argc < 3)
    {
        printf("Usage %s <auction-file> <num-bidders>", argv[0]);
        exit(EXIT_FAILURE);
    }

    int err;

    char *input_file = argv[1];
    int num_bidders = atoi(argv[2]);

    thread_data td[num_bidders];
    shared *sh = malloc(sizeof(shared));

    // init shared
    sh->bidders_n = num_bidders;
    sh->op = BLOCK;
    sh->exit = 0;

    // init mutex
    if ((err = pthread_mutex_init(&sh->lock, NULL)) != 0)
        exit_with_err("pthread_mutex_init", err);

    // init var condition
    if ((err = pthread_cond_init(&sh->cond_j, NULL)) != 0)
        exit_with_err("pthread_cond_init", err);

    if ((err = pthread_cond_init(&sh->cond_b, NULL)) != 0)
        exit_with_err("pthread_cond_init", err);

    // apro il file
    FILE *f;
    if ((f = fopen(input_file, "r")) == NULL)
    {
        fprintf(stderr, "File non trovato\n");
        exit(EXIT_FAILURE);
    }

    // B-i
    for (int i = 0; i < num_bidders; i++)
    {
        td[i].sh = sh;
        td[i].thread_n = i;

        if ((err = pthread_create(&td[i].tid, 0, (void *)thread_function, &td[i])) != 0)
            exit_with_err("pthread_create", err);
    }

    if ((err = pthread_mutex_lock(&sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    while (sh->bidders_n != 0)
    {
        if ((err = pthread_cond_wait(&sh->cond_j, &sh->lock)) != 0)
            exit_with_err("pthread_cond_wait", err);
    }

    printf("[J] Tutti gli offerenti sono collegati\n");

    if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    int counter_asta = 0;
    int totale_raccolto = 0;
    int aste_valide = 0;

    int offerte_valide = 0;
    int max_current_offer = -1;
    int id_max_current_bidder = -1;
    // leggo il file riga per riga
    char buffer[BUFFER_SIZE];
    while (fgets(buffer, BUFFER_SIZE, f))
    {
        offerte_valide = 0;
        max_current_offer = -1;
        id_max_current_bidder = -1;

        counter_asta++;

        if ((err = pthread_mutex_lock(&sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        if ((err = parse_line(buffer, sh)) != 0)
        {
            fprintf(stderr, "Riga non valida\n");
            exit(EXIT_FAILURE);
        }

        printf("[J] lancio asta n.%d per %s con offerta minima di %d EUR e massima di %d EUR\n", counter_asta, sh->description, sh->min_offer, sh->max_offer);

        if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        for (int i = 0; i < num_bidders; i++)
        {
            if ((err = pthread_mutex_lock(&sh->lock)) != 0)
                exit_with_err("pthread_mutex_lock", err);

            sh->op = SEND;
            if ((err = pthread_cond_signal(&sh->cond_b)) != 0)
                exit_with_err("pthread_cond_signal", err);

            if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
                exit_with_err("pthread_mutex_unlock", err);

            if ((err = pthread_mutex_lock(&sh->lock)) != 0)
                exit_with_err("pthread_mutex_lock", err);

            while (sh->op != RECEIVE)
                if ((err = pthread_cond_wait(&sh->cond_j, &sh->lock)) != 0)
                    exit_with_err("pthread_cond_wait", err);

            printf("[J] ricevuta offerta da B%d\n", sh->id_current_bidder);

            if (sh->current_offer >= sh->min_offer)
            {
                offerte_valide++;
                if (sh->current_offer > max_current_offer)
                {
                    max_current_offer = sh->current_offer;
                    id_max_current_bidder = sh->id_current_bidder;
                }
            }

            if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
                exit_with_err("pthread_mutex_unlock", err);
        }

        if ((err = pthread_mutex_lock(&sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        if (offerte_valide > 0)
        {
            aste_valide++;
            printf("[J] l'asta n.%d per %s si e' conclusa con %d offerte valide su %d; il vincitore e' B%d che si aggiudica l'oggetto per %d EUR\n\n", counter_asta, sh->description, offerte_valide, num_bidders, id_max_current_bidder, max_current_offer);
        }
        else
        {
            printf("[J] l'asta n.%d per %s si e' conclusa senza alcuna offerta valida pertanto l'oggetto non risulta assegnato\n\n", counter_asta, sh->description);
        }

        totale_raccolto = totale_raccolto + max_current_offer;

        if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);
    }

    if ((err = pthread_mutex_lock(&sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    sh->exit = 1;
    if((err = pthread_cond_broadcast(&sh->cond_b)) != 0)
        exit_with_err("pthread_cond_broadcast", err);

    if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    // join
    for (int i = 0; i < num_bidders; i++)
    {
        if ((err = pthread_join(td[i].tid, NULL)) != 0)
            exit_with_err("pthread_join", err);
    }

    // destroy
    pthread_mutex_destroy(&sh->lock);
    pthread_cond_destroy(&sh->cond_j);
    pthread_cond_destroy(&sh->cond_b);
    free(sh);

    // chiudo il file
    fclose(f);

    exit(EXIT_SUCCESS);
}