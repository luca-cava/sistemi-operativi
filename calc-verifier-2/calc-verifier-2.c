#include "lib/lib-misc.h"
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define LINE 256

typedef struct
{
    long long operando_1;
    long long operando_2;
    long long risultato;
    int id_richiedente;

    int exit;

    // strumenti sincronizzazione e mutua esclusione
    pthread_mutex_t read;
    pthread_mutex_t write;
    sem_t *sem_calc;
    sem_t sem_add;
    sem_t sem_sub;
    sem_t sem_mul;
} shared;

typedef struct
{
    // dati privati
    pthread_t tid;
    int thread_n;
    char *input_file;

    int success;

    // dati condivisi
    shared *sh;
} thread_data;

void calc_thread_function(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    long long risultato_atteso;
    long long totale;

    long pos;
    int ch;

    FILE *f;
    char buffer[LINE];

    if ((f = fopen(td->input_file, "r")) == NULL)
        exit_with_sys_err(td->input_file);

    if (fseek(f, 0, SEEK_END) != 0)
        exit_with_err("fseek", err);

    pos = ftell(f);
    if (pos == 0)
    {
        printf("File vuoto\n");
        fclose(f);
        exit(EXIT_FAILURE);
    }

    while (pos > 0)
    {
        fseek(f, --pos, SEEK_SET);
        ch = fgetc(f);

        if (ch == '\n')
        {
            pos++;
            break;
        }
    }

    if (pos == 0)
        fseek(f, 0, SEEK_SET);

    if (fgets(buffer, LINE, f) != NULL)
    {
        buffer[strcspn(buffer, "\n")] = '\0';
        risultato_atteso = atoll(buffer);
    }
    else
    {
        printf("Errore nella lettura dell'ultima riga\n");
        exit(EXIT_FAILURE);
    }

    rewind(f);

    if (fgets(buffer, LINE, f) != NULL)
    {
        buffer[strcspn(buffer, "\n")] = '\0';
        totale = atoll(buffer);
    }

    // printf("[CALC-%d] totale %lld risultato finale %lld\n", td->thread_n+1,totale, risultato_atteso);

    printf("[CALC-%d] file da verificare: '%s' \n", td->thread_n + 1, td->input_file);
    printf("[CALC-%d] valore iniziale della computazione: %lld\n", td->thread_n + 1, totale);

    // leggo il file riga per riga
    while (fgets(buffer, LINE, f) != NULL)
    {
        char *op;
        char *temp;
        char *saveptr;
        char buffer_copy[LINE];

        buffer[strcspn(buffer, "\n")] = '\0';
        strcpy(buffer_copy, buffer);

        op = strtok_r(buffer, " ", &saveptr);
        if ((temp = strtok_r(NULL, " ", &saveptr)) == NULL)
            break;
        else
            printf("[CALC-%d] prossima operazione '%s'\n", td->thread_n + 1, buffer_copy);

        if ((err = pthread_mutex_lock(&td->sh->read)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        if ((err = pthread_mutex_lock(&td->sh->write)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        td->sh->operando_1 = totale;
        td->sh->operando_2 = atoll(temp);
        td->sh->id_richiedente = td->thread_n;

        if (*op == '+')
        {
            if ((err = sem_post(&td->sh->sem_add)) != 0)
                exit_with_err("sem_post", err);
        }
        else if (*op == '-')
        {
            if ((err = sem_post(&td->sh->sem_sub)) != 0)
                exit_with_err("sem_post", err);
        }
        else if (*op == 'x')
        {
            if ((err = sem_post(&td->sh->sem_mul)) != 0)
                exit_with_err("sem_post", err);
        }
        else
        {
            printf("Formato non valido\n");
            exit(EXIT_FAILURE);
        }

        if ((err = pthread_mutex_unlock(&td->sh->write)) != 0)
            exit_with_err("pthread_mutex_unlock", err);

        if ((err = sem_wait(&td->sh->sem_calc[td->thread_n])) != 0)
            exit_with_err("sem_wait", err);

        totale = td->sh->risultato;
        printf("[CALC-%d] risultato ricevuto: %lld\n", td->thread_n + 1, totale);

        if ((err = pthread_mutex_unlock(&td->sh->read)) != 0)
            exit_with_err("pthread_mutex_unlock", err);
    }

    if (totale == risultato_atteso)
    {
        printf("[CALC-%d] computazione terminata in modo corretto: %lld\n", td->thread_n + 1, totale);
        td->success = 1;
    }
    else
    {
        printf("[CALC-%d] computazione terminata in modo NON corretto: %lld\n", td->thread_n + 1, totale);
        td->success = 0;
    }

    fclose(f);
}

void add_thread_function(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    while (1)
    {
        if ((err = sem_wait(&td->sh->sem_add)) != 0)
            exit_with_err("sem_wait", err);

        if ((err = pthread_mutex_lock(&td->sh->write)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        if (td->sh->exit == 1)
        {
            if ((err = pthread_mutex_unlock(&td->sh->write)) != 0)
                exit_with_err("pthread_mutex_unlock", err);

            break;
        }

        td->sh->risultato = td->sh->operando_1 + td->sh->operando_2;
        printf("[ADD] calcolo effettuato: %lld + %lld = %lld\n", td->sh->operando_1, td->sh->operando_2, td->sh->risultato);

        if ((err = sem_post(&td->sh->sem_calc[td->sh->id_richiedente])) != 0)
            exit_with_err("sem_post", err);

        if ((err = pthread_mutex_unlock(&td->sh->write)) != 0)
            exit_with_err("pthread_mutex_unlock", err);
    }
}

void sub_thread_function(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    while (1)
    {
        if ((err = sem_wait(&td->sh->sem_sub)) != 0)
            exit_with_err("sem_wait", err);

        if ((err = pthread_mutex_lock(&td->sh->write)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        if (td->sh->exit == 1)
        {
            if ((err = pthread_mutex_unlock(&td->sh->write)) != 0)
                exit_with_err("pthread_mutex_unlock", err);

            break;
        }

        td->sh->risultato = td->sh->operando_1 - td->sh->operando_2;
        printf("[SUB] calcolo effettuato: %lld - %lld = %lld\n", td->sh->operando_1, td->sh->operando_2, td->sh->risultato);

        if ((err = sem_post(&td->sh->sem_calc[td->sh->id_richiedente])) != 0)
            exit_with_err("sem_post", err);

        if ((err = pthread_mutex_unlock(&td->sh->write)) != 0)
            exit_with_err("pthread_mutex_unlock", err);
    }
}

void mul_thread_function(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    while (1)
    {
        if ((err = sem_wait(&td->sh->sem_mul)) != 0)
            exit_with_err("sem_wait", err);

        if ((err = pthread_mutex_lock(&td->sh->write)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        if (td->sh->exit == 1)
        {
            if ((err = pthread_mutex_unlock(&td->sh->write)) != 0)
                exit_with_err("pthread_mutex_unlock", err);

            break;
        }

        td->sh->risultato = td->sh->operando_1 * td->sh->operando_2;
        printf("[ADD] calcolo effettuato: %lld * %lld = %lld\n", td->sh->operando_1, td->sh->operando_2, td->sh->risultato);

        if ((err = sem_post(&td->sh->sem_calc[td->sh->id_richiedente])) != 0)
            exit_with_err("sem_post", err);

        if ((err = pthread_mutex_unlock(&td->sh->write)) != 0)
            exit_with_err("pthread_mutex_unlock", err);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: %s <calc-file-1> <calc-file-2> ... <calc-file-n>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int err;

    thread_data td[3 + argc - 1];
    shared *sh = malloc(sizeof(shared));

    // init shared
    sh->sem_calc = malloc(sizeof(sem_t) * (argc - 1));
    sh->exit = 0;

    // init mutex
    if ((err = pthread_mutex_init(&sh->read, NULL)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    if ((err = pthread_mutex_init(&sh->write, NULL)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    // init semaphore
    for (int i = 0; i < argc - 1; i++)
        if ((err = sem_init(&sh->sem_calc[i], 0, 0)) != 0)
            exit_with_err("sem_init", err);

    if ((err = sem_init(&sh->sem_add, 0, 0)) != 0)
        exit_with_err("sem_init", err);

    if ((err = sem_init(&sh->sem_sub, 0, 0)) != 0)
        exit_with_err("sem_init", err);

    if ((err = sem_init(&sh->sem_mul, 0, 0)) != 0)
        exit_with_err("sem_init", err);

    // calc-i
    for (int i = 0; i < argc - 1; i++)
    {
        td[i].sh = sh;
        td[i].thread_n = i;
        td[i].input_file = argv[i + 1];

        if ((err = pthread_create(&td[i].tid, 0, (void *)calc_thread_function, &td[i])) != 0)
            exit_with_err("pthread_create", err);
    }

    // add
    td[argc - 1].sh = sh;
    if ((err = pthread_create(&td[argc - 1].tid, 0, (void *)add_thread_function, &td[argc - 1])) != 0)
        exit_with_err("pthread_create", err);

    // sub
    td[argc].sh = sh;
    if ((err = pthread_create(&td[argc].tid, 0, (void *)sub_thread_function, &td[argc])) != 0)
        exit_with_err("pthread_create", err);

    // mul
    td[argc + 1].sh = sh;
    if ((err = pthread_create(&td[argc + 1].tid, 0, (void *)mul_thread_function, &td[argc + 1])) != 0)
        exit_with_err("pthread_create", err);

    // join
    int counter = 0;
    for (int i = 0; i < argc - 1; i++)
    {
        if ((err = pthread_join(td[i].tid, NULL)) != 0)
            exit_with_err("pthread_join", err);

        if (td[i].success == 1)
            counter++;
    }

    if ((err = pthread_mutex_lock(&sh->write)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    sh->exit = 1;

    if ((err = pthread_mutex_unlock(&sh->write)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    if ((err = sem_post(&sh->sem_add)) != 0)
        exit_with_err("sem_post", err);

    if ((err = sem_post(&sh->sem_sub)) != 0)
        exit_with_err("sem_post", err);

    if ((err = sem_post(&sh->sem_mul)) != 0)
        exit_with_err("sem_post", err);

    printf("[MAIN] verfiche completate con successo %d/%d\n", counter, argc - 1);

    // destroy
    pthread_mutex_destroy(&sh->read);
    pthread_mutex_destroy(&sh->write);

    for (int i = 0; i < argc - 1; i++)
        sem_destroy(&sh->sem_calc[i]);

    sem_destroy(&sh->sem_add);
    sem_destroy(&sh->sem_sub);
    sem_destroy(&sh->sem_mul);
    free(sh->sem_calc);
    free(sh);

    exit(EXIT_SUCCESS);
}