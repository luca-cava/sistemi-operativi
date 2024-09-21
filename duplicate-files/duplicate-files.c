#include "lib/lib-misc.h"
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BUFFER_SIZE 1024
#define MAX_RECORD 10

typedef struct record
{
    char buffer[BUFFER_SIZE];
    char *filename;
    unsigned long long dimensione_totale_file;
    unsigned long long offset;
    unsigned long long dimensione_effettiva_buffer;
    unsigned int thread_n;

    // eventuale flag
} record;

typedef struct
{
    record stack[MAX_RECORD];
    int top;

    int reader_i;
    int reader_num;

    // strumenti sincronizzazione e mutua esclusione
    pthread_mutex_t lock;
    sem_t full;
    sem_t empty;
} shared;

typedef struct
{
    // dati privati
    pthread_t tid;
    unsigned int thread_n;
    char *input_file;

    // dati condivisi
    shared *sh;
} thread_data;

void reader_thread(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    int fd;
    if ((fd = open(td->input_file, O_RDONLY)) == -1)
        exit_with_sys_err(td->input_file);

    struct stat sb;
    if (fstat(fd, &sb) == -1)
        exit_with_sys_err("fstat");

    if (!S_ISREG(sb.st_mode))
        exit_with_err_msg("%s non e' un file\n", td->input_file);

    printf("[READER-%d] lettura del file '%s' di %llu byte\n", td->thread_n + 1, td->input_file, (unsigned long long)sb.st_size);

    char *p;
    if ((p = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED)
        exit_with_err_msg("mmap");

    if (close(fd) == -1)
        exit_with_sys_err("close");

    size_t dimensione_rimanente = sb.st_size;
    unsigned long long offset = 0;
    char buffer[BUFFER_SIZE];
    for (size_t i = 0; i < sb.st_size; i += BUFFER_SIZE)
    {
        memset(buffer, '\0', BUFFER_SIZE);

        size_t bytes_da_leggere;
        if (dimensione_rimanente >= BUFFER_SIZE)
            bytes_da_leggere = BUFFER_SIZE;
        else
            bytes_da_leggere = dimensione_rimanente;

        printf("[READER-%d] lettura del blocco di offset %llu di %llu byte\n", td->thread_n + 1, offset, (unsigned long long)bytes_da_leggere);

        memcpy(buffer, p + i, bytes_da_leggere);

        // Creo record
        struct record r = {"", "", (unsigned long long)sb.st_size, offset, (unsigned long long)bytes_da_leggere, td->thread_n};
        strcpy(r.buffer, buffer);

        r.filename = td->input_file;

        if ((err = sem_wait(&td->sh->empty)))
            exit_with_err("sem_wait", err);

        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        td->sh->top++;
        td->sh->stack[td->sh->top] = r;

        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);

        if ((err = sem_post(&td->sh->full)))
            exit_with_err("sem_post", err);

        offset = offset + (unsigned long long)bytes_da_leggere;
        dimensione_rimanente = dimensione_rimanente - bytes_da_leggere;
    }

    printf("[READER-%d] lettura del file '%s' completata\n", td->thread_n + 1, td->input_file);

    if (munmap(p, sb.st_size) == -1)
        exit_with_sys_err("munmap");
}

void writer_thread(void *arg)
{
    int err;
    thread_data *td = (thread_data *)arg;

    int reader_num = 0;
    int file_duplicati = 0;

    if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_lock", err);

    reader_num = td->sh->reader_num;

    if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
        exit_with_err("pthread_mutex_unlock", err);

    unsigned long long dimensione_duplicata_file[reader_num];
    int check_open_file[reader_num];
    for (int i = 0; i < reader_num; i++)
    {
        dimensione_duplicata_file[i] = 0;
        check_open_file[i] = 0;
    }

    if (mkdir(td->input_file, 0777) == -1)
        printf("[WRITER] la cartella esiste gia'\n");
    else
        printf("[WRITER] creo la cartella\n");

    while (1)
    {
        if ((err = sem_wait(&td->sh->full)))
            exit_with_err("sem_wait", err);

        if ((err = pthread_mutex_lock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_lock", err);

        int fd;
        struct record r = td->sh->stack[td->sh->top];
        td->sh->top--;
        char file[BUFFER_SIZE];

        char temp[BUFFER_SIZE];
        strcpy(temp, r.filename);
        char *token;
        char *last_token;

        token = strtok(temp, "/");
        while (token != NULL)
        {
            last_token = token;
            token = strtok(NULL, "/");
        }

        strcpy(file, td->input_file);
        strcat(file, "/");
        strcat(file, last_token);

        if (check_open_file[r.thread_n] == 0)
        {
            check_open_file[r.thread_n] = 1;

            if ((fd = open(file, O_RDWR | O_CREAT | O_TRUNC, 0777)) == -1)
                exit_with_sys_err(file);

            printf("[WRITER] creazione del file '%s' di dimensione %llu byte\n", file, r.dimensione_totale_file);

            if (ftruncate(fd, r.dimensione_totale_file) == -1)
                exit_with_sys_err("ftruncate");
        }
        else if (check_open_file[r.thread_n] == 1)
        {
            if ((fd = open(file, O_RDWR, 0777)) == -1)
                exit_with_sys_err(file);
        }

        char *p;
        if ((p = mmap(NULL, r.dimensione_totale_file, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED)
            exit_with_err_msg("mmap writer");

        if (close(fd) == -1)
            exit_with_sys_err("close");

        memcpy(p + r.offset, r.buffer, r.dimensione_effettiva_buffer);
        if (msync(p, r.dimensione_totale_file, MS_SYNC) == -1)
            exit_with_sys_err("msync");

        printf("[WRITER] scrittura del blocco di offset %llu di %llu byte sul file '%s'\n", r.offset, r.dimensione_effettiva_buffer, file);

        if (munmap(p, r.dimensione_totale_file) == -1)
            exit_with_sys_err("munmap");

        dimensione_duplicata_file[r.thread_n] = dimensione_duplicata_file[r.thread_n] + r.dimensione_effettiva_buffer;
        if (dimensione_duplicata_file[r.thread_n] == r.dimensione_totale_file)
        {
            file_duplicati++;
            printf("[WRITER] scrittura del file '%s' completata\n", file);
        }

        if ((err = pthread_mutex_unlock(&td->sh->lock)) != 0)
            exit_with_err("pthread_mutex_unlock", err);

        if ((err = sem_post(&td->sh->empty)))
            exit_with_err("sem_post", err);

        if (file_duplicati == reader_num)
            break;
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        printf("Usage %s <file-1> <file-2> ... <file-n> <destination-dir>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    printf("[MAIN] duplicazione di %d file\n", argc - 2);

    int err;

    thread_data td[argc - 1];
    shared *sh = malloc(sizeof(shared));

    // init shared
    sh->reader_num = argc - 2;
    sh->top = -1;

    // init mutex
    if ((err = pthread_mutex_init(&sh->lock, NULL)) != 0)
        exit_with_err("pthread_mutex_init", err);

    // init sem
    if ((err = sem_init(&sh->full, 0, 0)) != 0)
        exit_with_err("sem_init", err);

    if ((err = sem_init(&sh->empty, 0, MAX_RECORD)) != 0)
        exit_with_err("sem_init", err);

    // reader-i
    for (int i = 0; i < argc - 2; i++)
    {
        td[i].sh = sh;
        td[i].thread_n = i;
        td[i].input_file = argv[i + 1];

        if ((err = pthread_create(&td[i].tid, 0, (void *)reader_thread, &td[i])) != 0)
            exit_with_err("pthread_create", err);
    }

    // writer
    td[argc - 2].sh = sh;
    td[argc - 2].input_file = argv[argc - 1];
    if ((err = pthread_create(&td[argc - 2].tid, 0, (void *)writer_thread, &td[argc - 2])) != 0)
        exit_with_err("pthread_create", err);

    // join
    for (int i = 0; i < argc - 1; i++)
    {
        if ((err = pthread_join(td[i].tid, NULL)) != 0)
            exit_with_err("pthread_join", err);
    }

    // destroy
    pthread_mutex_destroy(&sh->lock);
    sem_destroy(&sh->empty);
    sem_destroy(&sh->full);

    free(sh);

    printf("[MAIN] duplicazione dei %d file completata\n", argc - 2);

    exit(EXIT_SUCCESS);
}