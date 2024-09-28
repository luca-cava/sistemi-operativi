#include "lib/lib-misc.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>

#define ALPHABET 26
#define BUFFER_SIZE 512

typedef struct
{
    char frase_da_scoprire[BUFFER_SIZE];
    int lettere_chiamate[ALPHABET];
    int *punteggi;
    char lettera_prescelta;

    int exit;

    // strumenti sincronizzazione e mutua esclusione
    pthread_mutex_t lock;
    sem_t sem_m;
    sem_t *sem_g;
} shared;

typedef struct
{
    // dati privati
    pthread_t tid;
    int thread_n;

    // dati condivisi
    shared *sh;
} thread_data;

void player_thread_function(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    printf("[G%d] avviato e pronto\n", td->thread_n + 1);
    if ((err = sem_post(&td->sh->sem_m)) != 0)
        exit_with_err("sem_post", err);

    while (1)
    {
        if ((err = sem_wait(&td->sh->sem_g[td->thread_n])) != 0)
            exit_with_err("sem_wait", err);

        if (td->sh->exit == 1)
            break;

        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        int r;
        do
        {
            r = rand() % 26;
        } while (td->sh->lettere_chiamate[r] == 1);
        td->sh->lettere_chiamate[r] = 1;

        td->sh->lettera_prescelta = r + 'a';
        printf("[G%d] scelgo la lettera '%c'\n", td->thread_n + 1, td->sh->lettera_prescelta);

        if ((err = sem_post(&td->sh->sem_m)) != 0)
            exit_with_err("sem_post", err);

        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);
    }
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        printf("Usage: %s <n-numero-giocatori> <m-numero-partite> <file-con-frasi>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    srand(time(NULL));

    int err;

    int numero_giocatori = atoi(argv[1]);
    int numero_partite = atoi(argv[2]);
    char *input_file = argv[3];

    FILE *f;
    if ((f = fopen(input_file, "r")) == NULL)
    {
        printf("Errore nell'apertura del file\n");
        exit(EXIT_FAILURE);
    }

    char buffer[BUFFER_SIZE];
    int rows = 0;
    while (fgets(buffer, BUFFER_SIZE, f))
        rows++;

    if (rows == 0)
    {
        printf("File vuoto\n");
        exit(EXIT_FAILURE);
    }
    else if (rows < numero_partite)
    {
        printf("Il numero di frasi nel file non è sufficiente a gestire il numero di partite richieste\n");
        exit(EXIT_FAILURE);
    }

    printf("[M] lette %d possibili frasi da indovinare per %d partite\n", rows, numero_partite);

    thread_data td[numero_giocatori];
    shared *sh = malloc(sizeof(shared));

    // init shared
    sh->punteggi = malloc(sizeof(int) * numero_giocatori);
    sh->sem_g = malloc(sizeof(sem_t) * numero_giocatori);

    for (int i = 0; i < ALPHABET; i++)
        sh->lettere_chiamate[i] = 0;

    for (int i = 0; i < numero_giocatori; i++)
        sh->punteggi[i] = 0;

    sh->exit = 0;

    // init mutex
    if ((err = pthread_mutex_init(&sh->lock, NULL)) != 0)
        exit_with_err("pthread_mutex_init", err);

    // init semaphore
    for (int i = 0; i < numero_giocatori; i++)
        if ((err = sem_init(&sh->sem_g[i], 0, 0)) != 0)
            exit_with_err("sem_init", err);

    if ((err = sem_init(&sh->sem_m, 0, 0)) != 0)
        exit_with_err("sem_init", err);

    // G-i
    for (int i = 0; i < numero_giocatori; i++)
    {
        td[i].sh = sh;
        td[i].thread_n = i;

        if ((err = pthread_create(&td[i].tid, 0, (void *)player_thread_function, &td[i])) != 0)
            exit_with_err("pthread_create", err);
    }

    for (int i = 0; i < numero_giocatori; i++)
        if ((err = sem_wait(&sh->sem_m)) != 0)
            exit_with_err("sem_wait", err);

    printf("[M] tutti i giocatori sono pronti!\n");

    int frasi_selezionate[rows];
    for (int i = 0; i < rows; i++)
        frasi_selezionate[i] = 0;

    int partita_corrente = 0;

    while (partita_corrente < numero_partite)
    {
        rewind(f);
        int r;
        do
        {
            r = rand() % rows;
        } while (frasi_selezionate[r] == 1);
        frasi_selezionate[r] = 1;

        for (int i = 0; i <= r; i++)
            fgets(buffer, BUFFER_SIZE, f);

        buffer[strcspn(buffer, "\n")] = '\0';

        printf("[M] scelta la frase '%s' per la partita n.%d\n", buffer, partita_corrente + 1);
        int num_caratteri = 0;

        if ((err = pthread_mutex_lock(&sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        for(int i = 0; i < BUFFER_SIZE; i++)
            sh->frase_da_scoprire[i] = '\0';

        int k = 0;
        while (buffer[k] != '\0')
        {
            if (isalpha(buffer[k]))
            {
                sh->frase_da_scoprire[k] = '#';
                num_caratteri++;
            }
            else
            {
                sh->frase_da_scoprire[k] = buffer[k];
            }
            k++;
        }

        printf("[M] tabellone: %s\n", sh->frase_da_scoprire);

        if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);

        int turno = 0;
        while (num_caratteri != 0)
        {
            printf("[M] adesso è il turno di G%d\n", turno + 1);

            if ((err = sem_post(&sh->sem_g[turno])) != 0)
                exit_with_err("sem_post", err);

            if ((err = sem_wait(&sh->sem_m)) != 0)
                exit_with_err("sem_wait", err);

            if ((err = pthread_mutex_lock(&sh->lock)) != 0)
                exit_with_err("pthread_mutex_lock", err);

            // controllo lettera
            int c = 0;
            for (int i = 0; i < strlen(buffer); i++)
            {
                if (tolower(buffer[i]) == sh->lettera_prescelta)
                {
                    sh->frase_da_scoprire[i] = buffer[i];
                    c++;
                }
            }

            if (c == 0)
            {
                printf("[M] Nessuna occorrenza per '%c'\n", sh->lettera_prescelta);
                printf("[M] tabellone: %s\n", sh->frase_da_scoprire);
                if (++turno >= numero_giocatori)
                    turno = 0;
            }
            else
            {
                int p = ((rand() % 4) + 1) * 100;
                sh->punteggi[turno] = sh->punteggi[turno] + (p * c);
                printf("[M] ci sono %d occorrenze di '%c'; assegnati %dx%d=%d punti a G%d\n", c, sh->lettera_prescelta, p, c, (p * c), turno + 1);
                printf("[M] tabellone: %s\n", sh->frase_da_scoprire);
                num_caratteri = num_caratteri - c;
            }

            if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
                exit_with_err("pthread_mutex_unlock", err);
        }

        if ((err = pthread_mutex_lock(&sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        printf("[M] frase completata; punteggi attuali:");

        for (int i = 0; i < numero_giocatori; i++)
            printf(" G%d:%d", i + 1, sh->punteggi[i]);

        printf("\n");

        for (int i = 0; i < ALPHABET; i++)
            sh->lettere_chiamate[i] = 0;

        if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);

        partita_corrente++;
    }

    fclose(f);

    if ((err = pthread_mutex_lock(&sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    sh->exit = 1;
    int max = 0;
    int win = -1;
    for (int i = 0; i < numero_giocatori; i++)
    {
        if (sh->punteggi[i] > max)
        {
            max = sh->punteggi[i];
            win = i + 1;
        }

        if ((err = sem_post(&sh->sem_g[i])) != 0)
            exit_with_err("sem_post", err);
    }

    if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    // join
    for (int i = 0; i < numero_giocatori; i++)
        if ((err = pthread_join(td[i].tid, NULL)) != 0)
            exit_with_err("pthread_join", err);

    printf("[M] questa era l'ultima partita: il vincitore e' G%d\n", win);

    // destroy
    pthread_mutex_destroy(&sh->lock);

    for (int i = 0; i < numero_giocatori; i++)
        sem_destroy(&sh->sem_g[i]);
    free(sh->sem_g);
    free(sh->punteggi);

    sem_destroy(&sh->sem_m);

    free(sh);

    exit(EXIT_FAILURE);
}