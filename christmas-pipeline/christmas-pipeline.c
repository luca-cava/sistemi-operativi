#include "lib/lib-misc.h"
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#define SIZE_NOME_BAMBINO 50
#define SIZE_NOME_REGALO 50
#define BUFFER_SIZE 100

typedef enum
{
    BLOCK,
    RESULT,
    ES,
    BN,
    EI,
    EP,
    EC
} operation;

typedef struct
{
    char nome_bambino[SIZE_NOME_BAMBINO];
    char nome_regalo[SIZE_NOME_REGALO];
    int comportamento;
    unsigned int costo_regalo;
    operation op;

    unsigned int exit;
    int all;

    // strumenti sincronizzazione e mutua esclusione
    pthread_mutex_t lock;
    pthread_cond_t cond_all;
    pthread_cond_t cond_ES;
    pthread_cond_t cond_BN;
    pthread_cond_t cond_EI;
    pthread_cond_t cond_EP;
    pthread_cond_t cond_EC;
} shared;

typedef struct
{
    // dati privati
    pthread_t tid;
    int thread_n;
    char *input_file;

    // dati condivisi
    shared *sh;
} thread_data;

typedef struct
{
    char nome_bambino[SIZE_NOME_BAMBINO];
    int comportamento;
} buono_cattivo;

typedef struct
{
    char nome_regalo[SIZE_NOME_REGALO];
    int costo_regalo;
} regalo;

void smistatore_thread(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    FILE *f;
    if ((f = fopen(td->input_file, "r")) == NULL)
    {
        fprintf(stderr, "File non trovato\n");
        exit(EXIT_FAILURE);
    }

    if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    td->sh->all = td->sh->all - 1;

    if ((err = pthread_cond_signal(&td->sh->cond_all)) != 0)
        exit_with_err("pthread_cond_signal", err);

    if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    printf("[ES%d] leggo le letterine dal file '%s'\n", td->thread_n + 1, td->input_file);

    char buffer[BUFFER_SIZE];
    while (fgets(buffer, BUFFER_SIZE, f))
    {
        char *save_ptr;
        char *nome_bambino;
        char *nome_regalo;

        nome_bambino = strtok_r(buffer, ";", &save_ptr);
        nome_regalo = strtok_r(NULL, ";", &save_ptr);
        nome_regalo[strcspn(nome_regalo, "\n")] = '\0';

        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        printf("[ES%d] il/la bambino/a '%s' desidera per Natale '%s'\n", td->thread_n + 1, nome_bambino, nome_regalo);

        while (td->sh->op != ES)
        {
            if ((err = pthread_cond_wait(&td->sh->cond_ES, &td->sh->lock)) != 0)
                exit_with_err("pthread_cond_wait", err);
        }

        td->sh->op = BN;
        strcpy(td->sh->nome_bambino, nome_bambino);
        strcpy(td->sh->nome_regalo, nome_regalo);

        // sveglio il thread BN
        if ((err = pthread_cond_signal(&td->sh->cond_BN)) != 0)
            exit_with_err("pthread_cond_signal", err);

        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);
    }

    if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    td->sh->exit = td->sh->exit - 1;

    printf("[ES%d] non ho piu' letterine da consegnare\n", td->thread_n + 1);

    if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);
}

void babbo_natale_thread(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    td->sh->all = td->sh->all - 1;

    if ((err = pthread_cond_signal(&td->sh->cond_all)) != 0)
        exit_with_err("pthread_cond_signal", err);

    if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    while (1)
    {
        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        while (td->sh->op != BN && td->sh->op != BLOCK)
        {
            if ((err = pthread_cond_wait(&td->sh->cond_BN, &td->sh->lock)) != 0)
                exit_with_err("pthread_cond_wait", err);
        }

        if (td->sh->op == BLOCK)
        {
            printf("[BN] non ci sono piu' offerte da esaminare\n");

            if ((err = pthread_cond_signal(&td->sh->cond_EI)) != 0)
                exit_with_err("pthread_cond_signal", err);

            if ((err = pthread_cond_signal(&td->sh->cond_EP)) != 0)
                exit_with_err("pthread_cond_signal", err);

            if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
                exit_with_err("pthread_mutex_unlock", err);
            break;
        }

        printf("[BN] come si e' comportato il/la bambino/a '%s'?\n", td->sh->nome_bambino);
        td->sh->op = EI;

        // sveglio il thread EI
        if ((err = pthread_cond_signal(&td->sh->cond_EI)) != 0)
            exit_with_err("pthread_cond_signal", err);

        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);

        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        while (td->sh->op != BN)
        {
            if ((err = pthread_cond_wait(&td->sh->cond_BN, &td->sh->lock)) != 0)
                exit_with_err("pthread_cond_wait", err);
        }

        if (td->sh->comportamento == 0) // buono
        {
            printf("[BN] il/la bambino/a '%s' ricevera' il suo regalo '%s'\n", td->sh->nome_bambino, td->sh->nome_regalo);
            td->sh->op = EP;

            // sveglio il thread EP
            if ((err = pthread_cond_signal(&td->sh->cond_EP)) != 0)
                exit_with_err("pthread_cond_signal", err);
        }
        else if (td->sh->comportamento == 1) // cattivo
        {
            printf("[BN] il/la bambino/a '%s' non ricevera' alcun regalo quest'anno!\n", td->sh->nome_bambino);
            td->sh->op = EC;

            // sveglio il thread EC
            if ((err = pthread_cond_signal(&td->sh->cond_EC)) != 0)
                exit_with_err("pthread_cond_signal", err);
        }

        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);
    }
}

void indagatore_thread(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    FILE *f;
    if ((f = fopen(td->input_file, "r")) == NULL)
    {
        fprintf(stderr, "File non trovato\n");
        exit(EXIT_FAILURE);
    }

    int rows = 0;
    char buffer[BUFFER_SIZE];
    while (fgets(buffer, BUFFER_SIZE, f))
        rows++;

    if (rows == 0)
    {
        fprintf(stderr, "File vuoto\n");
        exit(EXIT_FAILURE);
    }
    rewind(f);

    buono_cattivo *arr = malloc(sizeof(buono_cattivo) * rows);

    int i = 0;
    while (fgets(buffer, BUFFER_SIZE, f))
    {
        char *save_ptr;
        char *token;
        token = strtok_r(buffer, ";", &save_ptr);
        strcpy(arr[i].nome_bambino, token);

        token = strtok_r(NULL, ";", &save_ptr);
        token[strcspn(token, "\n")] = '\0';

        if (strcmp(token, "buono") == 0)
            arr[i].comportamento = 0;
        else if (strcmp(token, "cattivo") == 0)
            arr[i].comportamento = 1;

        i++;
    }

    fclose(f);
    printf("[EI] ho caricato tutto\n");

    if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    td->sh->all = td->sh->all - 1;

    if ((err = pthread_cond_signal(&td->sh->cond_all)) != 0)
        exit_with_err("pthread_cond_signal", err);

    if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    while (1)
    {
        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        while (td->sh->op != EI && td->sh->op != BLOCK)
        {
            if ((err = pthread_cond_wait(&td->sh->cond_EI, &td->sh->lock)) != 0)
                exit_with_err("pthread_cond_wait", err);
        }

        if (td->sh->op == BLOCK)
        {
            if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
                exit_with_err("pthread_mutex_unlock", err);

            free(arr);
            break;
        }

        int found = 0;
        for (int i = 0; i < rows; i++)
        {
            if (strcmp(arr[i].nome_bambino, td->sh->nome_bambino) == 0)
            {
                td->sh->comportamento = arr[i].comportamento;
                found = 1;

                if (arr[i].comportamento == 0)
                    printf("[EI] il/la bambino/a '%s' e' stato buono quest'anno\n", td->sh->nome_bambino);
                else if (arr[i].comportamento == 1)
                    printf("[EI] il/la bambino/a '%s' e' stato cattivo quest'anno\n", td->sh->nome_bambino);

                break;
            }
        }

        if (found == 0)
        {
            fprintf(stderr, "Nome non trovato\n");
            exit(EXIT_FAILURE);
        }
        else if (found == 1)
        {
            td->sh->op = BN;
            // sveglio BN
            if ((err = pthread_cond_signal(&td->sh->cond_BN)) != 0)
                exit_with_err("pthread_cond_signal", err);
        }

        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);
    }
}

void produttore_thread(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    FILE *f;
    if ((f = fopen(td->input_file, "r")) == NULL)
    {
        fprintf(stderr, "File non trovato\n");
        exit(EXIT_FAILURE);
    }

    int rows = 0;
    char buffer[BUFFER_SIZE];
    while (fgets(buffer, BUFFER_SIZE, f))
        rows++;

    if (rows == 0)
    {
        fprintf(stderr, "File vuoto\n");
        exit(EXIT_FAILURE);
    }
    rewind(f);

    regalo *arr = malloc(sizeof(regalo) * rows);

    int i = 0;
    while (fgets(buffer, BUFFER_SIZE, f))
    {
        char *save_ptr;
        char *token;

        token = strtok_r(buffer, ";", &save_ptr);
        strcpy(arr[i].nome_regalo, token);

        token = strtok_r(NULL, ";", &save_ptr);
        arr[i].costo_regalo = atoi(token);

        i++;
    }
    fclose(f);
    printf("[EP] ho caricato tutto\n");

    if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    td->sh->all = td->sh->all - 1;

    if ((err = pthread_cond_signal(&td->sh->cond_all)) != 0)
        exit_with_err("pthread_cond_signal", err);

    if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    while (1)
    {
        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        while (td->sh->op != EP && td->sh->op != BLOCK)
        {
            if ((err = pthread_cond_wait(&td->sh->cond_EP, &td->sh->lock)) != 0)
                exit_with_err("pthread_cond_wait", err);
        }

        if (td->sh->op == BLOCK)
        {
            td->sh->op = RESULT;

            if ((err = pthread_cond_signal(&td->sh->cond_EC)) != 0)
                exit_with_err("pthread_cond_signal", err);

            if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
                exit_with_err("pthread_mutex_unlock", err);

            free(arr);
            break;
        }

        int found = 0;
        for (int i = 0; i < rows; i++)
        {
            if (strcmp(arr[i].nome_regalo, td->sh->nome_regalo) == 0)
            {
                td->sh->costo_regalo = arr[i].costo_regalo;
                found = 1;

                printf("[EP] creo il regalo '%s' per il/la bambino/a '%s' al costo di %d EUR\n", td->sh->nome_regalo, td->sh->nome_bambino, td->sh->costo_regalo);

                break;
            }
        }

        if (found == 0)
        {
            fprintf(stderr, "Regalo non trovato\n");
            exit(EXIT_FAILURE);
        }
        else if (found == 1)
        {
            td->sh->op = EC;
            // sveglio il thread EC
            if ((err = pthread_cond_signal(&td->sh->cond_EC)) != 0)
                exit_with_err("pthread_cond_signal", err);
        }

        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);
    }
}

void contabile_thread(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    int num_lettere_ricevute = 0;
    int num_bambini_buoni = 0;
    int num_bambini_cattivi = 0;
    int costo_totale_produzione = 0;

    if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    td->sh->all = td->sh->all - 1;

    if ((err = pthread_cond_signal(&td->sh->cond_all)) != 0)
        exit_with_err("pthread_cond_signal", err);

    if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    while (1)
    {
        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        while (td->sh->op != EC && td->sh->op != RESULT)
        {
            if ((err = pthread_cond_wait(&td->sh->cond_EC, &td->sh->lock)) != 0)
                exit_with_err("pthread_cond_wait", err);
        }

        if (td->sh->op == RESULT)
        {
            printf("[EC] quest'anno abbiamo ricevuto %d richieste da %d bambini buoni e da %d cattivi con un costo totale di produzione di %d EUR\n", num_lettere_ricevute, num_bambini_buoni, num_bambini_cattivi, costo_totale_produzione);
            if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
                exit_with_err("pthread_mutex_unlock", err);
            break;
        }

        num_lettere_ricevute++;
        if (td->sh->comportamento == 0) // buono
        {
            num_bambini_buoni++;
            costo_totale_produzione = costo_totale_produzione + td->sh->costo_regalo;

            printf("[EC] aggiornate le statistiche dei bambini buoni (%d) e dei costi totali (%d EUR)\n", num_bambini_buoni, costo_totale_produzione);
        }
        else if (td->sh->comportamento == 1) // cattivo
        {
            num_bambini_cattivi++;
            printf("[EC] aggiornate le statistiche dei bambini cattivi (%d)\n", num_bambini_cattivi);
        }

        if (td->sh->exit == 0)
        {
            td->sh->op = BLOCK;

            if ((err = pthread_cond_signal(&td->sh->cond_BN)) != 0)
                exit_with_err("pthread_cond_wait", err);
        }
        else
        {
            td->sh->op = ES;

            // sveglio i thread ES
            if ((err = pthread_cond_broadcast(&td->sh->cond_ES)) != 0)
                exit_with_err("pthread_cond_broadcast", err);
        }

        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);
    }
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        printf("Usage %s <presents-file> <good-bads-file> <letters-file-1> [... letters-file-n]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int err;

    int num_thread = (argc - 3) + 4;
    thread_data td[num_thread];
    shared *sh = malloc(sizeof(shared));

    // init shared
    sh->op = -1;
    sh->all = num_thread;
    sh->exit = argc - 3;

    // init mutex
    if ((err = pthread_mutex_init(&sh->lock, NULL)) != 0)
        exit_with_err("pthread_mutex_init", err);

    // init var cond
    if ((err = pthread_cond_init(&sh->cond_all, NULL)) != 0)
        exit_with_err("pthread_cond_init", err);

    if ((err = pthread_cond_init(&sh->cond_ES, NULL)) != 0)
        exit_with_err("pthread_cond_init", err);

    if ((err = pthread_cond_init(&sh->cond_BN, NULL)) != 0)
        exit_with_err("pthread_cond_init", err);

    if ((err = pthread_cond_init(&sh->cond_EI, NULL)) != 0)
        exit_with_err("pthread_cond_init", err);

    if ((err = pthread_cond_init(&sh->cond_EP, NULL)) != 0)
        exit_with_err("pthread_cond_init", err);

    if ((err = pthread_cond_init(&sh->cond_EC, NULL)) != 0)
        exit_with_err("pthread_cond_init", err);

    // ES-i
    for (int i = 0; i < argc - 3; i++)
    {
        td[i].sh = sh;
        td[i].thread_n = i;
        td[i].input_file = argv[i + 3];

        if ((err = pthread_create(&td[i].tid, 0, (void *)smistatore_thread, &td[i])) != 0)
            exit_with_err("pthread_create", err);
    }

    // BN
    td[argc - 3].sh = sh;
    if ((err = pthread_create(&td[argc - 3].tid, 0, (void *)babbo_natale_thread, &td[argc - 3])) != 0)
        exit_with_err("pthread_create", err);

    // EI
    td[argc - 3 + 1].sh = sh;
    td[argc - 3 + 1].input_file = argv[2];
    if ((err = pthread_create(&td[argc - 3 + 1].tid, 0, (void *)indagatore_thread, &td[argc - 3 + 1])) != 0)
        exit_with_err("pthread_create", err);

    // EP
    td[argc - 3 + 2].sh = sh;
    td[argc - 3 + 2].input_file = argv[1];
    if ((err = pthread_create(&td[argc - 3 + 2].tid, 0, (void *)produttore_thread, &td[argc - 3 + 2])) != 0)
        exit_with_err("pthread_create", err);

    // EC
    td[argc - 3 + 3].sh = sh;
    if ((err = pthread_create(&td[argc - 3 + 3].tid, 0, (void *)contabile_thread, &td[argc - 3 + 3])) != 0)
        exit_with_err("pthread_create", err);

    if ((err = pthread_mutex_lock(&sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    while (sh->all != 0)
    {
        if ((err = pthread_cond_wait(&sh->cond_all, &sh->lock)) != 0)
            exit_with_err("pthread_cond_wait", err);
    }

    printf("[MAIN] tutti i thread creati sono pronti\n");

    if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    if ((err = pthread_mutex_lock(&sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    sh->op = ES;

    if ((err = pthread_cond_broadcast(&sh->cond_ES)) != 0)
        exit_with_err("pthread_cond_broadcast", err);

    if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    // join
    for (int i = 0; i < num_thread; i++)
    {
        if ((err = pthread_join(td[i].tid, NULL)) != 0)
            exit_with_err("pthread_join", err);
    }

    // destroy
    pthread_mutex_destroy(&sh->lock);
    pthread_cond_destroy(&sh->cond_all);
    pthread_cond_destroy(&sh->cond_ES);
    pthread_cond_destroy(&sh->cond_BN);
    pthread_cond_destroy(&sh->cond_EI);
    pthread_cond_destroy(&sh->cond_EP);
    pthread_cond_destroy(&sh->cond_EC);
    free(sh);

    exit(EXIT_SUCCESS);
}