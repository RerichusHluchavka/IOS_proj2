#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>

// structure for semaphore barrier
typedef struct
{
    int num;
    int count;
    sem_t mutex;
    sem_t turnstile1;
    sem_t turnstile2;
} Barrier;

// constructor for barrier
Barrier create_barrier(int n)
{
    Barrier barrier;
    barrier.num = n;
    barrier.count = 0;
    sem_init(&barrier.mutex, 1, 1);
    sem_init(&barrier.turnstile1, 1, 0);
    sem_init(&barrier.turnstile2, 1, 0);
    return barrier;
}

// barrier wait function, waits for barrier->num processes to reach barrier
void barrier_wait(Barrier *barrier)
{
    sem_wait(&barrier->mutex);
    barrier->count++;
    if (barrier->count == barrier->num)
    {
        for (int i = 0; i < barrier->num; i++)
        {
            sem_post(&barrier->turnstile1);
        }
    }
    sem_post(&barrier->mutex);
    sem_wait(&barrier->turnstile1);

    sem_wait(&barrier->mutex);
    barrier->count--;
    if (barrier->count == 0)
    {
        for (int i = 0; i < barrier->num; i++)
        {
            sem_post(&barrier->turnstile2);
        }
    }
    sem_post(&barrier->mutex);
    sem_wait(&barrier->turnstile2);
}

// dealocate semaphores in barrier
void barrier_destroy(Barrier *barrier)
{
    sem_destroy(&barrier->mutex);
    sem_destroy(&barrier->turnstile1);
    sem_destroy(&barrier->turnstile2);
}

// structure for shared memory
typedef struct shared_mem
{
    int count;
    int molecule_count;
    int remaining_oxygen;
    int remaining_hydrogen;
    sem_t semtex;
    sem_t mutex;
    sem_t hydrogen_queue;
    sem_t oxygen_queue;
    Barrier barrier;
} shared_mem_t;

// structure for arguments
typedef struct argum
{
    int ti;
    int tb;
    int no;
    int nh;
} argum_t;

// declaring global variables
argum_t *argum;
shared_mem_t *mem;
FILE *fp;

// constructor for shared memory
void create_sh_memory()
{
    mem = (shared_mem_t *)mmap(NULL, sizeof(shared_mem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0);
    mem->count = 1;
    mem->molecule_count = 1;
    mem->remaining_oxygen = argum->no;
    mem->remaining_hydrogen = argum->nh;
    sem_init(&mem->mutex, 1, 1);
    sem_init(&mem->hydrogen_queue, 1, 2);
    sem_init(&mem->oxygen_queue, 1, 1);
    mem->barrier = create_barrier(3);
}

// constructtor for arguments structure
void set_arguments(char *argv[])
{
    argum = (argum_t *)malloc(sizeof(argum_t));
    argum->ti = atoi(argv[3]);
    argum->tb = atoi(argv[4]);
    argum->no = atoi(argv[1]);
    argum->nh = atoi(argv[2]);
}

// checks arguments if they are valid calls argument_struct constructor
void check_arg(int argc, char *argv[])
{
    if (argc != 5)
    {
        fprintf(stderr, "Wrong number of arguments\n");
        exit(1);
    }
    if (atoi(argv[1]) < 0)
    {
        fprintf(stderr, "NO cant be less than zero\n");
        exit(1);
    }

    if (atoi(argv[2]) < 0)
    {
        fprintf(stderr, "NH cant be less than zero\n");
        exit(1);
    }

    if (0 > atoi(argv[3]) || atoi(argv[3]) > 1000)
    {
        fprintf(stderr, "TI cant be less than zero or greater than 1000\n");
        exit(1);
    }

    if (0 > atoi(argv[4]) || atoi(argv[4]) > 1000)
    {
        fprintf(stderr, "TB cant be less than zero or greater than 1000\n");
        exit(1);
    }
    set_arguments(argv);
}

// deallocate non_shared memory
void free_non_shared()
{
    free(argum);
    fclose(fp);
}

// deallocate shared memory
void free_shared()
{
    sem_destroy(&mem->mutex);
    sem_destroy(&mem->hydrogen_queue);
    sem_destroy(&mem->oxygen_queue);
    barrier_destroy(&mem->barrier);
    munmap(mem, sizeof(shared_mem_t));
}

// function for writing messages in out file
void write_into_file(char atom, int id, char *message)
{
    fprintf(fp, "%d: %c %d: %s\n", mem->count++, atom, id, message);
    fflush(fp);
}

// checks if there is enough hydrogen for molecule
void oxygen_check(int idO)
{
    if (mem->remaining_hydrogen <= 1)
    {
        write_into_file('O', idO, "not enough H");
        mem->remaining_oxygen--;
        sem_post(&mem->mutex);
        sem_post(&mem->oxygen_queue);
        free_non_shared();
        _exit(0);
    }
}

// checks if there is enough hydrogen and oxygen for molecule
void hydrogen_check(int idH)
{
    if (mem->remaining_oxygen < 1 || mem->remaining_hydrogen <= 1)
    {
        write_into_file('H', idH, "not enough O or H");
        mem->remaining_hydrogen--;
        sem_post(&mem->mutex);
        sem_post(&mem->hydrogen_queue);
        free_non_shared();
        _exit(0);
    }
}

// start creating atom
void start_atom(char atom, int id)
{
    srand(time(NULL) ^ getpid());
    sem_wait(&mem->mutex);
    write_into_file(atom, id, "started");
    sem_post(&mem->mutex);
    usleep(rand() % argum->ti * 1000);
}

//creates molecule from 2 hydrogen atoms and 1 oxygen atom
void create_molecule(char atom, int id)
{
    sem_wait(&mem->mutex);
    sem_post(&mem->mutex);
    char buffer[100];
    sprintf(buffer, "creating molecule %d", mem->molecule_count);
    write_into_file(atom, id, buffer);
    if (atom == 'O')
    {
        usleep(rand() % argum->tb * 1000);
    }
    barrier_wait(&mem->barrier);
    sprintf(buffer, "molecule %d created", mem->molecule_count);
    write_into_file(atom, id, buffer);
}

//queue for oxygen atoms
void oxygen_queue(int idO)
{
    sem_wait(&mem->oxygen_queue);
    sem_wait(&mem->mutex);
    oxygen_check(idO);
    sem_post(&mem->mutex);
    barrier_wait(&mem->barrier);
    mem->remaining_oxygen--;
    create_molecule('O', idO);
    barrier_wait(&mem->barrier);
    sem_wait(&mem->mutex);
    mem->molecule_count++;
    sem_post(&mem->mutex);
    sem_post(&mem->oxygen_queue);
}

//queue for hydrogen atoms
void hydrogen_queue(int idH)
{
    sem_wait(&mem->hydrogen_queue);
    sem_wait(&mem->mutex);
    hydrogen_check(idH);
    sem_post(&mem->mutex);

    barrier_wait(&mem->barrier);
    mem->remaining_hydrogen--;
    create_molecule('H', idH);

    barrier_wait(&mem->barrier);
    sem_post(&mem->hydrogen_queue);
}

//creates new process of oxygen atom
void oxygen(int idO)
{
    pid_t pid = fork();

    if (pid == 0)
    {
        start_atom('O', idO);

        sem_wait(&mem->mutex);
        write_into_file('O', idO, "going to queue");
        sem_post(&mem->mutex);

        oxygen_queue(idO);

        free_non_shared();
        _exit(0);
    }
}

//creates new process of hydrogen atom
void hydrogen(int idH)
{
    pid_t pid = fork();

    if (pid == 0)
    {
        srand(time(NULL) ^ getpid());

        start_atom('H', idH);
        sem_wait(&mem->mutex);
        write_into_file('H', idH, "going to queue");

        sem_post(&mem->mutex);
        hydrogen_queue(idH);
        free_non_shared();
        _exit(0);
    }
}

//opens out file
void open_file()
{
    fp = fopen("proj2.out", "w");
    if (fp == NULL)
    {
        fprintf(stderr, "Error opening file\n");
        exit(1);
    }
}

int main(int argc, char *argv[])
{
    srand(time(NULL));
    check_arg(argc, argv);
    create_sh_memory();
    open_file();

    for (int i = 1; i < argum->no + 1; i++)
    {
        oxygen(i);
    }

    for (int i = 1; i < argum->nh + 1; i++)
    {
        hydrogen(i);
    }

    for (int i = 0; i < argum->nh + argum->no; i++)
    {
        waitpid(-1, NULL, 0);
    }
    free_non_shared();
    free_shared();
    exit(0);
}
