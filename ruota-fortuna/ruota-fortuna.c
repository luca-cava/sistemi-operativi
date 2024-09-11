#include "lib/lib-misc.h"
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>

#define LINE 100
#define ALPHABET 26

typedef struct
{
    char frase_da_scoprire[LINE];
    int lettere_chiamate[ALPHABET];
    int *punteggi;
    char lettera;
    int fine_partita;
    int id_turno;
    int start;

    // strumenti per la sincronizzazione e la mutua esclusione
    pthread_mutex_t lock;
    pthread_cond_t cond_start;
    pthread_cond_t *cond_player;
} shared;

typedef struct
{
    // dati privati
    pthread_t tid;
    unsigned thread_n;

    // dati condivisi
    shared *sh;
} thread_data;

void thread_function(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    // ottengo il lock della struttura dati condivisa
    if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    printf("[G%u] avviato e pronto\n", td->thread_n + 1);

    td->sh->start = td->sh->start - 1;

    // sveglio il thread M
    if ((err = pthread_cond_signal(&td->sh->cond_start)) != 0)
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
        while (td->sh->id_turno != td->thread_n && td->sh->fine_partita != 1)
        {
            //printf("G%u non e' il mio turno perche' id_turno vale %d\n", td->thread_n + 1, td->sh->id_turno);
            if ((err = pthread_cond_wait(&td->sh->cond_player[td->thread_n], &td->sh->lock)) != 0)
                exit_with_err("pthread_cond_wait", err);
        }

        // printf("G%u e' il mio turno\n", td->thread_n + 1);

        // verifico se devo terminare
        if (td->sh->fine_partita == 1)
        {
            if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
                exit_with_err("pthread_mutex_unlock", err);
            //printf("G%u termino\n", td->thread_n + 1);
            break;
        }

        int r;
        do
        {
            r = rand() % 26;
        } while (td->sh->lettere_chiamate[r] == 1);

        td->sh->lettere_chiamate[r] = 1;

        // int r = rand()%26;

        td->sh->lettera = r + 'a';
        printf("[G%u] scelgo la lettera '%c'\n", td->thread_n + 1, td->sh->lettera);

        // sveglio il thread M
        td->sh->id_turno = -1;
        if ((err = pthread_cond_signal(&td->sh->cond_start)) != 0)
            exit_with_err("pthread_cond_signal", err);

        // rilascio il lock
        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);
    }
}

int main(int argc, char **argv)
{
    int err;
    srand(time(NULL));

    if (argc < 3)
    {
        printf("Usage %s <n-numero-giocatori> <m-numero-partite> <file-con-frasi>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int player = atoi(argv[1]);
    int game = atoi(argv[2]);
    ;

    char *input_file = argv[3];

    // apro il file
    FILE *f = fopen(input_file, "r");
    if (f == NULL)
        exit_with_err("fopen", err);

    int rows = 0;
    char buffer[LINE];
    while (fgets(buffer, LINE, f))
        rows++;

    if (rows < game)
    {
        fprintf(stderr, "Numero di frasi non valido\n");
        exit(EXIT_FAILURE);
    }

    printf("[M] lette %d possibili frasi da indovinare per %d partite\n", rows, game);

    thread_data td[player];
    shared *sh = malloc(sizeof(shared));

    // init shared
    memset(sh->lettere_chiamate, 0, ALPHABET);
    sh->punteggi = calloc(player, sizeof(int));
    sh->start = player;
    sh->id_turno = -1;
    sh->fine_partita = 0;
    sh->cond_player = malloc(sizeof(pthread_cond_t) * player);

    // init mutex
    if ((err = pthread_mutex_init(&sh->lock, 0)) != 0)
        exit_with_err("pthread_mutex_init", err);

    // init var cond
    if ((err = pthread_cond_init(&sh->cond_start, 0)) != 0)
        exit_with_err("pthread_cond_init", err);

    for (int i = 0; i < player; i++)
        if ((err = pthread_cond_init(&sh->cond_player[i], 0)) != 0)
            exit_with_err("pthread_cond_init", err);

    // G-i
    for (int i = 0; i < player; i++)
    {
        td[i].sh = sh;
        td[i].thread_n = i;

        if ((err = pthread_create(&td[i].tid, 0, (void *)thread_function, &td[i])) != 0)
            exit_with_err("pthread_create", err);
    }

    // ottengo il lock
    if ((err = pthread_mutex_lock(&sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    // verifico le condizioni di operabilità
    while (sh->start != 0)
    {
        if ((err = pthread_cond_wait(&sh->cond_start, &sh->lock)) != 0)
            exit_with_err("pthread_cond_wait", err);
    }

    // rilascio il lock
    if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    int check[rows];
    memset(check, 0, rows);
    int partita_corrente = 0;
    int r;

    while (partita_corrente < game)
    {
        rewind(f);

        do
        {
            r = rand() % rows;
        } while (check[r] == 1);

        check[r] = 1;

        for (int i = 0; i <= r; i++)
            fgets(buffer, LINE, f);

        buffer[strcspn(buffer, "\n")] = '\0';

        printf("[M] scelta la frase '%s'\n", buffer);
        int num_caratteri = 0;

        // ottengo il lock
        if ((err = pthread_mutex_lock(&sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

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

        // rilascio il lock
        if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);

        unsigned j = 0;
        while (num_caratteri != 0)
        {
            if ((err = pthread_mutex_lock(&sh->lock)) != 0)
                exit_with_err("pthread_mutex_lock", err);

            printf("[M] adesso è il turno di G%u\n", j + 1);
            sh->id_turno = j;

            if ((err = pthread_cond_signal(&sh->cond_player[j])) != 0)
                exit_with_err("pthread_cond_signal", err);

            if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
                exit_with_err("pthread_mutex_unlock", err);

            if ((err = pthread_mutex_lock(&sh->lock)) != 0)
                exit_with_err("pthread_mutex_lock", err);

            while (sh->id_turno != -1)
            {
                if ((err = pthread_cond_wait(&sh->cond_start, &sh->lock)) != 0)
                    exit_with_err("pthread_cond_wait", err);
            }

            // controllo lettera
            int c = 0;
            for (int i = 0; i < strlen(buffer); i++)
            {
                if (tolower(buffer[i]) == sh->lettera)
                {
                    c++;
                    sh->frase_da_scoprire[i] = buffer[i];
                }
            }

            if (c == 0)
            {
                printf("[M] Nessuna occorrenza per '%c'\n", sh->lettera);
                printf("[M] tabellone: %s\n", sh->frase_da_scoprire);
                j++;
                if (j >= player)
                    j = 0;
            }
            else
            {
                int p = (1 + rand() % 4) * 100;
                int point = p * c;
                sh->punteggi[j] = sh->punteggi[j] + point;
                printf("[M] ci sono %d occorrenze di %c; assegnati %dx%d=%d punti a G%u\n", c, sh->lettera, p, c, point, j + 1);
                printf("[M] tabellone: %s\n", sh->frase_da_scoprire);
                num_caratteri = num_caratteri - c;
                //printf("[M] caratteri rimasti: %d\n", num_caratteri);
            }

            if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
                exit_with_err("pthread_mutex_unlock", err);
        }

        if ((err = pthread_mutex_lock(&sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        printf("[M] frase completata; punteggi attuali:");
        for (int i = 0; i < player; i++)
            printf(" G%u:%d", i + 1, sh->punteggi[i]);
        printf("\n");
        for(int i = 0; i < ALPHABET; i++)
            sh->lettere_chiamate[i] = 0;
        
        if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
                exit_with_err("pthread_mutex_unlock", err);

        partita_corrente++;
    }

    if ((err = pthread_mutex_lock(&sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    sh->fine_partita = 1;

    //risveglio eventuali thread player
    int max = 0;
    int win;
    for(int i = 0; i < player;i++)
    {
        //sh->id_turno = i;
        if(sh->punteggi[i] > max) 
        {
            max = sh->punteggi[i];
            win = i+1;
        }
        if((err = pthread_cond_signal(&sh->cond_player[i])) != 0)
            exit_with_err("pthread_cond_wait", err);
    }

    if ((err = pthread_mutex_unlock(&sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);
    
    // join
    for (int i = 0; i < player; i++)
    {
        if ((err = pthread_join(td[i].tid, NULL)) != 0)
            exit_with_err("pthread_join", err);
    }

    
    printf("[M] questa era l'ultima partita: il vincitore e' G%d\n", win);


    // destroy
    pthread_mutex_destroy(&sh->lock);
    for (int i = 0; i < player; i++)
        pthread_cond_destroy(&sh->cond_player[i]);

    free(sh->cond_player);
    free(sh->punteggi);
    free(sh);
}
