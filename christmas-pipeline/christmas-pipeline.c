#include "lib/lib-misc.h"
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#define NAME 50
#define BUFFER_SIZE 100

typedef enum
{
    ES,
    BN,
    EI,
    EP,
    EC
} turno;

typedef struct
{
    char nome_bambino[NAME];
    char nome_regalo[NAME];
    int comportamento;
    int costo_regalo;

    turno turn;
    int all;

    // strumenti per la sincronizzazione e la mutua esclusione
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
    int ruolo;
    char *input_file;

    // dati condivisi
    shared *sh;
} thread_data;

typedef struct
{
    char nome[NAME];
    int comportamento;
} buono_cattivo;

typedef struct
{
    char nome[NAME];
    int costo;
} regalo;

void smistatore_thread(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    FILE *f;
    char buffer[BUFFER_SIZE];

    // apro il file in sola lettura
    if ((f = fopen(td->input_file, "r")) == NULL)
        exit_with_err("fopen", err);

    printf("[ES%d] leggo le letterine dal file '%s'\n", td->thread_n + 1, td->input_file);

    while (fgets(buffer, BUFFER_SIZE, f))
    {
        // ottengo il lock
        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        while (td->sh->turn != ES)
        {
            if ((err = pthread_cond_wait(&td->sh->cond_ES, &td->sh->lock)) != 0)
                exit_with_err("pthread_cond_wait", err);
        }

        char *rest = buffer;
        strcpy(td->sh->nome_bambino, strtok_r(buffer, ";", &rest));

        char *temp = strtok_r(NULL, ";", &rest);
        temp[strcspn(temp, "\n")] = '\0';
        strcpy(td->sh->nome_regalo, temp);

        printf("[ES%d] il bambino '%s' per Natale desidera '%s'\n", td->thread_n + 1, td->sh->nome_bambino, td->sh->nome_regalo);

        td->sh->turn = BN;
        if((err = pthread_cond_signal(&td->sh->cond_BN)) != 0)
            exit_with_err("pthread_cond_signal", err);

        // rilascio il lock
        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);
    }
}

void babbonatale_thread(void *arg)
{
    int err;
    thread_data *td=(thread_data *)arg;

    while(1)
    {
        // ottengo il lock
        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        while(td->sh->turn != BN)
        {
            printf("[BN] block\n");
            if((err = pthread_cond_wait(&td->sh->cond_BN, &td->sh->lock)) != 0)
                exit_with_err("pthread_cond_wait", err);
            printf("[BN] controllo\n");
        }

        printf("[BN] come si è comportato il bambino '%s'?\n", td->sh->nome_bambino);

        // rilascio il lock
        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);
    }
}

void indagatore_thread(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    FILE *f;
    char buffer[BUFFER_SIZE];

    // apro il file in sola lettura
    if ((f = fopen(td->input_file, "r")) == NULL)
        exit_with_err("fopen", err);

    int rows = 0;
    while (fgets(buffer, BUFFER_SIZE, f))
        rows++;

    if (rows == 0)
    {
        fprintf(stderr, "File vuoto\n");
        exit(EXIT_FAILURE);
    }

    rewind(f);
    buono_cattivo *arr = malloc(sizeof(buono_cattivo) * rows);

    // leggo il file riga per riga
    int i = 0;
    while (fgets(buffer, BUFFER_SIZE, f))
    {
        char *rest = buffer;
        strcpy(arr[i].nome, strtok_r(buffer, ";", &rest));

        char *temp = strtok_r(NULL, ";", &rest);
        temp[strcspn(temp, "\n")] = '\0';

        if (strcmp(temp, "cattivo") == 0)
        {
            arr[i].comportamento = 0;
        }
        else if (strcmp(temp, "buono") == 0)
        {
            arr[i].comportamento = 1;
        }
        // printf("[EI] ho letto %s ; %d\n", arr[i].nome, arr[i].comportamento);
        i++;
    }

    // ottengo il lock della struttura dati condivisa
    if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    td->sh->all = td->sh->all - 1;

    if ((err = pthread_cond_signal(&td->sh->cond_all)) != 0)
        exit_with_err("pthread_cond_signal", err);

    // ottengo il lock della struttura dati condivisa
    if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    // chiudo il file
    fclose(f);

    free(arr);
}

void produttore_thread(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    FILE *f;
    char buffer[BUFFER_SIZE];

    // apro il file in sola lettura
    if ((f = fopen(td->input_file, "r")) == NULL)
        exit_with_err("fopen", err);

    int rows = 0;
    while (fgets(buffer, BUFFER_SIZE, f))
        rows++;

    if (rows == 0)
    {
        fprintf(stderr, "File vuoto\n");
        exit(EXIT_FAILURE);
    }

    rewind(f);
    regalo *vet = malloc(sizeof(regalo) * rows);

    int i = 0;
    while (fgets(buffer, BUFFER_SIZE, f))
    {
        char *rest = buffer;
        strcpy(vet[i].nome, strtok_r(buffer, ";", &rest));

        char *temp = strtok_r(NULL, ";", &rest);
        temp[strcspn(temp, "\n")] = '\0';

        vet[i].costo = atoi(temp);

        // printf("[EP] ho letto %s ; %d\n", vet[i].nome, vet[i].costo);

        i++;
    }

    // ottengo il lock della struttura dati condivisa
    if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    td->sh->all = td->sh->all - 1;

    if ((err = pthread_cond_signal(&td->sh->cond_all)) != 0)
        exit_with_err("pthread_cond_signal", err);

    // ottengo il lock della struttura dati condivisa
    if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    // chiudo il file
    fclose(f);

    free(vet);
}

void contabile_thread(void *arg)
{
    int err;
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        printf("Usage %s <presents-file> <goods-bads-file> <letters-file-1> [... letters-file-n]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int err;

    thread_data td[4 + argc - 3];
    shared *sh = malloc(sizeof(shared));

    sh->all = 2;
    sh->turn = -1;

    // init mutex
    if ((err = pthread_mutex_init(&sh->lock, NULL)) != 0)
        exit_with_err("pthread_mutex_init", err);

    // init var cond
    if ((err = pthread_cond_init(&sh->cond_all, 0)) != 0)
        exit_with_err("pthread_cond_init", err);

    if ((err = pthread_cond_init(&sh->cond_ES, 0)) != 0)
        exit_with_err("pthread_cond_init", err);

    if ((err = pthread_cond_init(&sh->cond_BN, 0)) != 0)
        exit_with_err("pthread_cond_init", err);

    if ((err = pthread_cond_init(&sh->cond_EI, 0)) != 0)
        exit_with_err("pthread_cond_init", err);

    if ((err = pthread_cond_init(&sh->cond_EP, 0)) != 0)
        exit_with_err("pthread_cond_init", err);

    if ((err = pthread_cond_init(&sh->cond_EC, 0)) != 0)
        exit_with_err("pthread_cond_init", err);

    // ES-i
    for (int i = 0; i < argc - 3; i++)
    {
        td[i].sh = sh;
        td[i].ruolo = ES;
        td[i].thread_n = i;
        td[i].input_file = argv[argc + i - 3];

        if ((err = pthread_create(&td[i].tid, 0, (void *)smistatore_thread, &td[i])) != 0)
            exit_with_err("pthread_create", err);
    }

    // BN
    td[argc - 3].sh = sh;
    td[argc - 3].ruolo = BN;

    if ((err = pthread_create(&td[argc - 3].tid, 0, (void *)babbonatale_thread, &td[argc - 3])) != 0)
        exit_with_err("pthread_create", err);

    // EI
    td[argc - 3 + 1].sh = sh;
    td[argc - 3 + 1].ruolo = EI;
    td[argc - 3 + 1].input_file = argv[2];

    if ((err = pthread_create(&td[argc - 3 + 1].tid, 0, (void *)indagatore_thread, &td[argc - 3 + 1])) != 0)
        exit_with_err("pthread_create", err);

    // EP
    td[argc - 3 + 2].sh = sh;
    td[argc - 3 + 2].ruolo = EP;
    td[argc - 3 + 2].input_file = argv[1];

    if ((err = pthread_create(&td[argc - 3 + 2].tid, 0, (void *)produttore_thread, &td[argc - 3 + 2])) != 0)
        exit_with_err("pthread_create", err);

    // EC
    td[argc - 3 + 3].sh = sh;
    td[argc - 3 + 3].ruolo = EC;

    if ((err = pthread_create(&td[argc - 3 + 3].tid, 0, (void *)contabile_thread, &td[argc - 3 + 3])) != 0)
        exit_with_err("pthread_create", err);

    // ottengo il lock
    if ((err = pthread_mutex_lock(&sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    // verifico le condizioni di operabilità
    while (sh->all != 0)
        if ((err = pthread_cond_wait(&sh->cond_all, &sh->lock)) != 0)
            exit_with_err("pthread_cond_wait", err);

    sh->turn = ES;
    if ((err = pthread_cond_broadcast(&sh->cond_ES)) != 0)
        exit_with_err("pthread_cond_broadcast", err);

    // rilascio il lock
    if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    // join
    for (int i = 0; i < 4 + argc - 3; i++)
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

    printf("OK\n");
}