#include "lib/lib-misc.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>

#define RANGE 75
#define ROWS 3
#define COLS 5

typedef struct
{
    int **card;
    int num_estratto;

    int done_cinquina;
    int done_bingo;
    int vincitore;

    int exit;

    unsigned int num_card;
    unsigned int num_player;

    // strumenti sincronizzazione e mutua esclusione
    pthread_mutex_t lock;
    sem_t sem_r, sem_w;
} shared;

typedef struct
{
    // dati privati
    pthread_t tid;
    unsigned int thread_n;

    int ***cards;
    int ***check;
    unsigned int num_card;

    // dati condivisi
    shared *sh;
} thread_data;

void dealer_thread(void *arg)
{
    int err;

    thread_data *td = (thread_data *)arg;

    printf("D: ci saranno %d giocatori con %d cartelle ciascuno\n", td->thread_n, td->num_card);

    int cartelle_da_generare = td->thread_n * td->num_card;

    int numeri_estratti[RANGE + 1];

    for (int i = 0; i < cartelle_da_generare; i++)
    {
        for (int i = 0; i < RANGE + 1; i++)
        {
            numeri_estratti[i] = 0;
        }

        if ((err = sem_wait(&td->sh->sem_w)) != 0)
            exit_with_err("sem_wait", err);

        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        td->sh->card = malloc(sizeof(int *) * ROWS);
        for (int j = 0; j < ROWS; j++)
        {
            td->sh->card[j] = malloc(sizeof(int) * COLS);
            for (int k = 0; k < COLS; k++)
            {
                int x;
                do
                {
                    x = (rand() % RANGE) + 1;
                } while (numeri_estratti[x] == 1);

                numeri_estratti[x] = 1;

                td->sh->card[j][k] = x;
            }
        }

        printf("D: genero e distribuisco la card n.%d: ", i + 1);
        for (int j = 0; j < ROWS; j++)
        {
            printf("(");
            for (int k = 0; k < COLS; k++)
            {
                printf("%d", td->sh->card[j][k]);
                if (k < COLS - 1)
                    printf(", ");
            }
            if (j < ROWS - 1)
                printf(") / ");
            else
                printf(")");
        }
        printf("\n");

        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);

        if ((err = sem_post(&td->sh->sem_r)) != 0)
            exit_with_err("sem_post", err);
    }

    if ((err = sem_wait(&td->sh->sem_w)) != 0)
        exit_with_err("sem_wait", err);

    printf("D: fine della distribuzione delle card e inizio di estrazione numeri\n");

    for (int i = 0; i < RANGE + 1; i++)
    {
        numeri_estratti[i] = 0;
    }

    for (int i = 0; i < RANGE; i++)
    {
        int r;
        do
        {
            r = (rand() % RANGE) + 1;
        } while (numeri_estratti[r] == 1);

        numeri_estratti[r] = 1;

        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        td->sh->num_estratto = r;
        printf("D: estrazione del prossimo numero: %d\n", td->sh->num_estratto);

        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);

        for (int j = 0; j < td->thread_n; j++)
        {
            if ((err = sem_post(&td->sh->sem_r)) != 0)
                exit_with_err("sem_post", err);

            if ((err = sem_wait(&td->sh->sem_w)) != 0)
                exit_with_err("sem_wait", err);
        }

        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        if (td->sh->done_cinquina == 1)
        {
            printf("D: il giocatore n.%d ha vinto la cinquina con la scheda ", td->sh->vincitore);
            for (int j = 0; j < ROWS; j++)
            {
                printf("(");
                for (int k = 0; k < COLS; k++)
                {
                    printf("%d", td->sh->card[j][k]);
                    if (k < COLS - 1)
                        printf(", ");
                }
                if (j < ROWS - 1)
                    printf(") / ");
                else
                    printf(")");
            }
            printf("\n");
            td->sh->done_cinquina = -1;
        }

        if (td->sh->done_bingo == 1)
        {
            printf("D: il giocatore n.%d ha vinto il Bingo con la scheda ", td->sh->vincitore);
            for (int j = 0; j < ROWS; j++)
            {
                printf("(");
                for (int k = 0; k < COLS; k++)
                {
                    printf("%d", td->sh->card[j][k]);
                    if (k < COLS - 1)
                        printf(", ");
                }
                if (j < ROWS - 1)
                    printf(") / ");
                else
                    printf(")");
            }
            printf("\n");
            td->sh->exit = 1;

            for (int i = 0; i < td->thread_n; i++)
            {
                if ((err = sem_post(&td->sh->sem_r)) != 0)
                    exit_with_err("sem_post", err);
            }

            if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
                exit_with_err("pthread_mutex_unlock", err);
            break;
        }

        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);
    }

    printf("D: fine del gioco\n");
}

int check_cinquina(int i, int **matrix)
{
    for (int j = 0; j < COLS; j++)
    {
        if (matrix[i][j] == 0)
            return 0;
    }

    return 1;
}

int check_bingo(int **matrix)
{
    for (int i = 0; i < ROWS; i++)
    {
        for (int j = 0; j < COLS; j++)
        {
            if (matrix[i][j] == 0)
                return 0;
        }
    }
    return 1;
}

void print_card(int **matrix)
{
    for (int j = 0; j < ROWS; j++)
    {
        printf("(");
        for (int k = 0; k < COLS; k++)
        {
            printf("%d", matrix[j][k]);
            if (k < COLS - 1)
                printf(", ");
        }
        if (j < ROWS - 1)
            printf(") / ");
        else
            printf(")");
    }
    printf("\n");
}

void player_thread(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    for (int i = 0; i < td->num_card; i++)
    {
        for (int j = 0; j < ROWS; j++)
        {
            for (int k = 0; k < COLS; k++)
            {
                td->check[i][j][k] = 0;
            }
        }
    }

    for (int i = 0; i < td->num_card; i++)
    {
        if ((err = sem_wait(&td->sh->sem_r)) != 0)
            exit_with_err("sem_wait", err);

        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        td->cards[i] = td->sh->card;

        printf("P%d: ricevuta card ", td->thread_n + 1);
        for (int j = 0; j < ROWS; j++)
        {
            printf("(");
            for (int k = 0; k < COLS; k++)
            {
                printf("%d", td->cards[i][j][k]);
                if (k < COLS - 1)
                    printf(", ");
            }
            if (j < ROWS - 1)
                printf(") / ");
            else
                printf(")");
        }
        printf("\n");

        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);

        if ((err = sem_post(&td->sh->sem_w)) != 0)
            exit_with_err("sem_post", err);
    }

    int num_cinquine = 0;

    while (1)
    {
        /*if((err = sem_wait(&td->sh->sem_r)) != 0)
            exit_with_err("sem_wait", err);*/

        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        if (td->sh->exit == 1)
        {
            if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
                exit_with_err("pthread_mutex_unlock", err);

            break;
        }

        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);

        if ((err = sem_wait(&td->sh->sem_r)) != 0)
            exit_with_err("sem_wait", err);

        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        int found = 0;
        // printf("P%d controllo card\n", td->thread_n+1);
        for (int i = 0; i < td->num_card; i++)
        {
            for (int j = 0; j < ROWS; j++)
            {
                for (int k = 0; k < COLS; k++)
                {
                    if (td->cards[i][j][k] == td->sh->num_estratto)
                    {
                        found = 1;
                        td->check[i][j][k] = 1;
                        if (td->sh->done_cinquina == 0)
                        {
                            if (check_cinquina(j, td->check[i]) == 1)
                            {
                                td->sh->card = td->cards[i];
                                td->sh->vincitore = td->thread_n + 1;
                                printf("P%d: card con cinquina: ", td->thread_n + 1);
                                print_card(td->cards[i]);
                                //print_card(td->check[i]);
                                td->sh->done_cinquina = 1;
                                num_cinquine++;
                                break;
                            }
                        }
                        else
                        {
                            if (check_bingo(td->check[i]) == 1)
                            {
                                td->sh->card = td->cards[i];
                                td->sh->vincitore = td->thread_n + 1;
                                printf("P%d: card con Bingo: ", td->thread_n + 1);
                                print_card(td->cards[i]);
                                //print_card(td->check[i]);
                                td->sh->done_bingo = 1;
                                break;
                            }
                        }
                        break;
                    }
                }
                if (td->sh->done_cinquina == 1 || found == 1 || td->sh->done_bingo == 1)
                    break;
            }
            if (td->sh->done_bingo)
                break;
        }

        if (td->sh->done_bingo)
        {
            if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
                exit_with_err("pthread_mutex_unlock", err);

            if ((err = sem_post(&td->sh->sem_w)) != 0)
                exit_with_err("sem_post", err);
            break;
        }

        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);

        if ((err = sem_post(&td->sh->sem_w)) != 0)
            exit_with_err("sem_post", err);
    }

    // printf("P%d fine\n", td->thread_n + 1);
}

int main(int argc, char **argv)
{
    srand(time(NULL));

    if (argc < 3)
    {
        printf("Usage: %s <n> <m>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int err;

    unsigned int num_player = atoi(argv[1]);
    unsigned int num_card = atoi(argv[2]);

    thread_data td[num_player + 1];
    shared *sh = malloc(sizeof(shared));

    // init shared
    sh->done_cinquina = 0;
    sh->done_bingo = 0;
    sh->vincitore = -1;
    sh->exit = 0;

    // init mutex
    if ((err = pthread_mutex_init(&sh->lock, NULL)) != 0)
        exit_with_err("pthread_mutex_init", err);

    // init semaphore
    if ((err = sem_init(&sh->sem_w, 0, 1)) != 0)
        exit_with_err("sem_init", err);

    if ((err = sem_init(&sh->sem_r, 0, 0)) != 0)
        exit_with_err("sem_init", err);

    // Dealer
    td[num_player].sh = sh;
    td[num_player].thread_n = num_player;
    td[num_player].num_card = num_card;
    if ((err = pthread_create(&td[num_player].tid, 0, (void *)dealer_thread, &td[num_player])) != 0)
        exit_with_err("pthread_create", err);

    // P-i
    for (int i = 0; i < num_player; i++)
    {
        td[i].sh = sh;
        td[i].thread_n = i;
        td[i].num_card = num_card;

        td[i].cards = malloc(sizeof(int **) * num_card);
        td[i].check = malloc(sizeof(int **) * num_card);
        for (int j = 0; j < num_card; j++)
        {
            td[i].cards[j] = malloc(sizeof(int *) * ROWS);
            td[i].check[j] = malloc(sizeof(int *) * ROWS);
            for (int k = 0; k < ROWS; k++)
            {
                td[i].cards[j][k] = malloc(sizeof(int) * COLS);
                td[i].check[j][k] = malloc(sizeof(int) * COLS);
            }
        }

        if ((err = pthread_create(&td[i].tid, 0, (void *)player_thread, &td[i])) != 0)
            exit_with_err("pthread_create", err);
    }

    // join
    for (int i = 0; i < num_player + 1; i++)
    {
        if ((err = pthread_join(td[i].tid, NULL)) != 0)
            exit_with_err("pthread_join", err);
    }

    // destroy
    for (int i = 0; i < num_player; i++)
    {
        for (int j = 0; j < num_card; j++)
        {
            for (int k = 0; k < ROWS; k++)
            {
                free(td[i].cards[j][k]);
                free(td[i].check[j][k]);
            }
            free(td[i].cards[j]);
            free(td[i].check[j]);
        }
        free(td[i].cards);
        free(td[i].check);
    }
    pthread_mutex_destroy(&sh->lock);
    sem_destroy(&sh->sem_r);
    sem_destroy(&sh->sem_w);
    free(sh);

    exit(EXIT_SUCCESS);
}