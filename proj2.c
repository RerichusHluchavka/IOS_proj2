#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>

typedef struct{
    int num;
    int count;
    sem_t mutex;
    sem_t turnstile1;
    sem_t turnstile2;
} Barrier;


Barrier create_barrier(int n){
    Barrier *barrier = malloc(sizeof(Barrier));
    barrier->num = n;
    barrier->count = 0;
    sem_init(&barrier->mutex, 1, 1);
    sem_init(&barrier->turnstile1, 1, 0);
    sem_init(&barrier->turnstile2, 1, 0);
    return *barrier;
}
   /* def __init__ ( self , n ):
    self .n = n
    self . count = 0
    self . mutex = Semaphore (1)
    self . turnstile = Semaphore (0)
    self . turnstile2 = Semaphore (0)


    def phase1 ( self ):
    self . mutex . wait ()
    self . count += 1

    if self . count == self .n:
    self . turnstile . signal ( self . n)
    self . mutex . signal ()
    self . turnstile . wait ()

    def phase2 ( self ):
    self . mutex . wait ()
    self . count -= 1
    if self . count == 0:
    self . turnstile2 . signal ( self .n)
    self . mutex . signal ()
    self . turnstile2 . wait ()

    def wait ( self ):
    self . phase1 ()
    self . phase2 ()*/



void barrier_wait(Barrier *barrier){
    sem_wait(&barrier->mutex);
    barrier->count++;
    if(barrier->count == barrier->num){
        for (int i = 0; i < barrier->num; i++)
        {
            sem_post(&barrier->turnstile1);
        }
    }
    sem_post(&barrier->mutex);
    sem_wait(&barrier->turnstile1);
    
    sem_wait(&barrier->mutex);
    barrier->count--;
    if(barrier->count == 0){
        for (int i = 0; i < barrier->num; i++)
        {
            sem_post(&barrier->turnstile2);
        }
        
    }
    sem_post(&barrier->mutex);
    sem_wait(&barrier->turnstile2);
}


typedef struct shared_mem
{
    int count;
    int oxy_count;
    int hydro_count;
    sem_t semtex;
    sem_t mutex;
    sem_t oxygen_sem;
    sem_t hydrogen_sem;
    sem_t hydrogen_queue;
    sem_t oxygen_queue;
    Barrier barrier;
}shared_mem_t;

typedef struct argum
{
    int ti;
    int tb;
    int no;
    int nh;
}argum_t;

argum_t *argum;
shared_mem_t *mem;
FILE *fp;

void create_sh_memory(){
    mem = (shared_mem_t *)mmap(NULL, sizeof(shared_mem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0);
    mem->count = 1;
    mem->oxy_count = 1;
    mem->hydro_count = 1;
    sem_init(&mem->oxygen_sem, 1, 1);
    sem_init(&mem->hydrogen_sem, 1, 1);
    sem_init(&mem->mutex, 1, 1);
    sem_init(&mem->hydrogen_queue, 1, 2);
    sem_init(&mem->oxygen_queue, 1, 1);
    mem->barrier = create_barrier(3);
}

void set_arguments(char *argv[]){
    argum = (argum_t *)malloc(sizeof(argum_t));
    argum->ti = atoi(argv[3]);
    argum->tb = atoi(argv[4]);
    argum->no = atoi(argv[1]);
    argum->nh = atoi(argv[2]);
}
//checks arguments
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

    if (0>atoi(argv[3]) || atoi(argv[3])>1000)
    {
        fprintf(stderr, "TI cant be less than zero or greater than 1000\n");
        exit(1);
    }

    if (0>atoi(argv[4]) || atoi(argv[4])>1000)
    {
        fprintf(stderr, "TB cant be less than zero or greater than 1000\n");
        exit(1);
    }
    set_arguments(argv);
}


void write_into_file(char *mol, int id,char *message){
    fprintf(fp, "%d: %s %d: %s\n", mem->count++,mol, id, message);
    fflush(fp);
}

void create_oxygen(int ti)
{
    pid_t pid = fork();

    if (pid == 0)
    {
            srand(time(NULL)^getpid());

            sem_wait(&mem->mutex);
            int idO = mem->oxy_count;
            write_into_file("O", idO, "started");
            mem->oxy_count++;
            sem_post(&mem->mutex);

            usleep(rand() % ti * 1000);

            sem_wait(&mem->mutex);
            write_into_file("O", idO, "going to queue");

            sem_post(&mem->mutex);





            sem_wait(&mem->oxygen_queue);

            barrier_wait(&mem->barrier);



            sem_post(&mem->oxygen_queue);




        _exit(0);
    }
}

void create_hydrogen(int ti)
{
    pid_t pid = fork();

    if (pid == 0)
    {
            srand(time(NULL)^getpid());
            
            sem_wait(&mem->mutex);
            int idH = mem->hydro_count;
            write_into_file("H", idH, "started");
            mem->hydro_count++;
            sem_post(&mem->mutex);

            usleep(rand() % ti * 1000);
            sem_wait(&mem->mutex);
            write_into_file("H", idH, "going to queue");

            sem_post(&mem->mutex);



            sem_wait(&mem->hydrogen_queue);
            

            barrier_wait(&mem->barrier);


            sem_post(&mem->hydrogen_queue);




        _exit(0);
    }
}

//free shared memory
void sh_free()
{
    munmap(mem, sizeof(shared_mem_t));
}

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

    for (int i = 0; i < argum->no; i++)
    {
        create_oxygen(argum->ti);
    }

    for (int i = 0; i < argum->nh; i++)
    {
        create_hydrogen(argum->ti);
    }

    for (int i = 0; i < argum->nh + argum->no; i++)
    {
        waitpid(-1, NULL, 0);
    }

    fclose(fp);
    sh_free();
    exit(0);
}
