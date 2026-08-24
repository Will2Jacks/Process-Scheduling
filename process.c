#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/sem.h>
#include <errno.h>

#define MEM_SIZE 4096
#define READY 0
#define RUNNING 1
#define IO 2
#define EXITED 3

#define DELTA 100
#define delta 10

struct pcb{
    int id;
    pid_t pid;
    int priority;
    int state;
};

union semun{
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

int* ready_queue;
struct pcb* pcb_store;
int* time_array;

int shmid_RQ = -1;
int shmid_PCB = -1;
int shmid_timer = -1;

int semid_sem_RQ = -1;
int semid_sem_PCB = -1;
int semid_sem_timer = -1;
int semid_sem_SYNC = -1;

void semaphore_lock(int semid) 
{
    struct sembuf sb;
    sb.sem_num = 0;
    sb.sem_op = -1;
    sb.sem_flg = 0;

    while (semop(semid, &sb, 1) == -1) 
    {
        if (errno == EINTR) 
        {
            continue; 
        }
        perror("Lock failed");
        exit(1);
    }
}

void semaphore_unlock(int semid) 
{
    struct sembuf sb;
    sb.sem_num = 0;
    sb.sem_op = 1;
    sb.sem_flg = 0;

    while (semop(semid, &sb, 1) == -1) 
    {
        if (errno == EINTR) 
        {
            continue; 
        }
        perror("Unlock failed");
        exit(1);
    }
}

void semaphore_wait_for_zero(int semid)
{
    struct sembuf sb;
    sb.sem_num = 0;
    sb.sem_op = 0;
    sb.sem_flg = 0;

    while (semop(semid, &sb, 1) == -1) 
    {
        if (errno == EINTR) 
        {
            continue; 
        }
        perror("Waiting failed");
        exit(1);
    }
}

int readN()
{
    FILE* fp = fopen("input.txt","r");
    char buffer[MEM_SIZE];

    int num_processes = 0;

    while(fgets(buffer,sizeof(buffer),fp))
    {
        int arrival_time;
        int priority;
        int cpu_burst[11];
        int io_burst[10];

        sscanf(buffer,"%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d",&arrival_time,&priority,&cpu_burst[0],&io_burst[0],&cpu_burst[1],&io_burst[1],&cpu_burst[2],&io_burst[2],&cpu_burst[3],&io_burst[3],&cpu_burst[4],&io_burst[4],&cpu_burst[5],&io_burst[5],&cpu_burst[6],&io_burst[6],&cpu_burst[7],&io_burst[7],&cpu_burst[8],&io_burst[8],&cpu_burst[9],&io_burst[9],&cpu_burst[10]);

        if(arrival_time == -1) break;
        num_processes++;
    }

    fclose(fp);
    return num_processes;
}

int num_processes = 0;
int process_state = -1;
int local_time = 0;

int interrupted = 0;
int id = -1;
int arrival_time = -1;
int priority = -1;
int* cpu_burst;
int* io_burst;

void initialise_burst_arrays()
{
    cpu_burst = (int*)malloc(11 * sizeof(int));
    io_burst = (int*)malloc(10 * sizeof(int));
}

void check_interrupt(int signo)
{
    if(signo == SIGUSR1)
    {
        interrupted = 1;
    }
}

void schedule_next()
{
    semaphore_lock(semid_sem_RQ);
    semaphore_lock(semid_sem_PCB);
    semaphore_lock(semid_sem_timer);

    int front_of_queue = ready_queue[num_processes + 1];
    int back_of_queue = ready_queue[num_processes + 2];

    if(front_of_queue == back_of_queue)
    {
        time_array[1] = -1;
        time_array[2] = -1;
        printf("[%d] CPU goes idle\n", time_array[0]);
    }
    else
    {
        int next_process = ready_queue[front_of_queue];
        pcb_store[next_process].state = RUNNING;
        time_array[1] = next_process;

        front_of_queue = (front_of_queue + 1) % (num_processes + 1);
        ready_queue[num_processes + 1] = front_of_queue;

        int time_quantum = 0;
        if(pcb_store[next_process].priority == 0) time_quantum = 10;
        else if(pcb_store[next_process].priority == 1) time_quantum = 5;
        else time_quantum = 2;

        int global_time = time_array[0];
        int next_interrupt_time = global_time + time_quantum;
        time_array[2] = next_interrupt_time;

        printf("[%d] Process %d: Going from READY to RUNNING with next interrupt time = %d\n", time_array[0], next_process, time_array[2]);
    }

    semaphore_unlock(semid_sem_timer);
    semaphore_unlock(semid_sem_PCB);
    semaphore_unlock(semid_sem_RQ);
}

int main(int argc,char* argv[])
{
    key_t key_shm_RQ = ftok(".",65);
    if(key_shm_RQ == -1)
    {
        perror("Creating key for Ready Queue failed");
        exit(1);
    }

    key_t key_shm_PCB = ftok(".",66);
    if(key_shm_PCB == -1)
    {
        perror("Creating key for PCB's failed");
        exit(1);
    }

    key_t key_shm_timer = ftok(".",67);
    if(key_shm_timer == -1)
    {
        perror("Creating key for Timer failed");
        exit(1);
    }

    shmid_RQ = shmget(key_shm_RQ,MEM_SIZE,0666);
    if(shmid_RQ == -1)
    {
        perror("shmget for Ready Queue failed");
        exit(1);
    }

    shmid_PCB = shmget(key_shm_PCB,MEM_SIZE,0666);
    if(shmid_PCB == -1)
    {
        perror("shmget for PCB's failed");
        exit(1);
    }

    shmid_timer = shmget(key_shm_timer,MEM_SIZE,0666);
    if(shmid_timer == -1)
    {
        perror("shmget for timer failed");
        exit(1);
    }

    ready_queue = (int*)shmat(shmid_RQ,NULL,0);
    if(ready_queue == (int*)-1)
    {
        perror("Ready queue shmat failed");
        exit(1);
    }

    pcb_store = (struct pcb*)shmat(shmid_PCB,NULL,0);
    if(pcb_store == (struct pcb*)-1)
    {
        perror("PCB store shmat failed");
        exit(1);
    }

    time_array = (int*)shmat(shmid_timer,NULL,0);
    if(time_array == (int*)-1)
    {
        perror("Time array shmat failed");
        exit(1);
    }

    num_processes = readN();

    key_t key_sem_SYNC = ftok(".",71);
    if(key_sem_SYNC == -1)
    {
        perror("Semaphore for SYNC failed");
        exit(1);
    }

    key_t key_sem_PCB = ftok(".",69);
    if(key_sem_PCB == -1)
    {
        perror("Semaphore for PCB failed");
        exit(1);
    }

    key_t key_sem_timer = ftok(".",70);
    if(key_sem_timer == -1)
    {
        perror("Semaphore for timer failed");
        exit(1);
    }

    key_t key_sem_RQ = ftok(".",68);
    if(key_sem_RQ == -1)
    {
        perror("Semaphore for Ready queue failed");
        exit(1);
    }

    semid_sem_SYNC = semget(key_sem_SYNC,1,0666);
    if(semid_sem_SYNC == -1)
    {
        perror("semget for SYNC failed");
        exit(1);
    }

    semid_sem_PCB = semget(key_sem_PCB,1,0666);
    if(semid_sem_PCB == -1)
    {
        perror("semget for PCB failed");
        exit(1);
    }

    semid_sem_timer = semget(key_sem_timer,1,0666);
    if(semid_sem_timer == -1)
    {
        perror("semget for timer failed");
        exit(1);
    }

    semid_sem_RQ = semget(key_sem_RQ,1,0666);
    if(semid_sem_RQ == -1)
    {
        perror("semget for Ready queue failed");
        exit(1);
    }

    initialise_burst_arrays();
    signal(SIGUSR1,check_interrupt);

    id = atoi(argv[1]);
    arrival_time = atoi(argv[2]);
    priority = atoi(argv[3]);

    for(int i = 0;i < 11;i++)
    {
        cpu_burst[i] = atoi(argv[2 * i + 4]);
    }
    for(int i = 0;i < 10;i++)
    {
        io_burst[i] = atoi(argv[2 * i + 5]);
    }

    semaphore_lock(semid_sem_PCB);
        pcb_store[id].id = id;
        pcb_store[id].pid = getpid();
        pcb_store[id].priority = priority;
        pcb_store[id].state = READY;
    semaphore_unlock(semid_sem_PCB);

    semaphore_lock(semid_sem_RQ);
        int front_of_queue = ready_queue[num_processes + 1];
        int back_of_queue = ready_queue[num_processes + 2];

        ready_queue[back_of_queue] = id;
        back_of_queue = (back_of_queue + 1) % (num_processes + 1);
        ready_queue[num_processes + 2] = back_of_queue;
    semaphore_unlock(semid_sem_RQ);

    int current_process = -1;
    semaphore_lock(semid_sem_timer);
        current_process = time_array[1];
    semaphore_unlock(semid_sem_timer);

    if(current_process == -1)
    {
        schedule_next();
    }

    if(id == 0)
    {
        semaphore_unlock(semid_sem_SYNC);
    }

    int burst_index = 0;
    int time_remaining = cpu_burst[0];

    while(1)
    {
        usleep(DELTA * 1000);
        semaphore_wait_for_zero(semid_sem_SYNC);

        semaphore_lock(semid_sem_timer);
            local_time = time_array[0];
        semaphore_unlock(semid_sem_timer);

        semaphore_lock(semid_sem_PCB);
            process_state = pcb_store[id].state;
        semaphore_unlock(semid_sem_PCB);

        if(process_state == EXITED)
        {
            break;
        }
        else if(process_state == IO)
        {
            time_remaining--;

            if(time_remaining == 0)
            {
                printf("[%d] Process %d: IO burst %d complete\n", local_time, id, burst_index + 1);
                burst_index++;
                time_remaining = cpu_burst[burst_index];

                semaphore_lock(semid_sem_PCB);
                    pcb_store[id].state = READY;
                semaphore_unlock(semid_sem_PCB);

                semaphore_lock(semid_sem_RQ);
                    int front_of_queue = ready_queue[num_processes + 1];
                    int back_of_queue = ready_queue[num_processes + 2];

                    ready_queue[back_of_queue] = id;
                    back_of_queue = (back_of_queue + 1) % (num_processes + 1);
                    ready_queue[num_processes + 2] = back_of_queue;
                semaphore_unlock(semid_sem_RQ);

                current_process = -1;
                semaphore_lock(semid_sem_timer);
                    current_process = time_array[1];
                semaphore_unlock(semid_sem_timer);

                if(current_process == -1)
                {
                    schedule_next();
                }
            }
        }
        else if(process_state == RUNNING)
        {
            time_remaining--;

            if(time_remaining == 0)
            {
                printf("[%d] Process %d: CPU burst %d complete\n", local_time, id, burst_index + 1);
                if(burst_index == 10)
                {
                    semaphore_lock(semid_sem_PCB);
                        pcb_store[id].state = EXITED;
                    semaphore_unlock(semid_sem_PCB);
                }
                else
                {
                    time_remaining = io_burst[burst_index];

                    semaphore_lock(semid_sem_PCB);
                        pcb_store[id].state = IO;
                    semaphore_unlock(semid_sem_PCB);
                }

                schedule_next();
            }
            else if(interrupted == 1)
            {
                printf("[%d] Process %d: Interrupted\n", local_time, id);
                interrupted = 0;

                semaphore_lock(semid_sem_PCB);
                    pcb_store[id].state = READY;
                semaphore_unlock(semid_sem_PCB);

                semaphore_lock(semid_sem_RQ);
                    int front_of_queue = ready_queue[num_processes + 1];
                    int back_of_queue = ready_queue[num_processes + 2];

                    ready_queue[back_of_queue] = id;
                    back_of_queue = (back_of_queue + 1) % (num_processes + 1);
                    ready_queue[num_processes + 2] = back_of_queue;
                semaphore_unlock(semid_sem_RQ);

                schedule_next();
            }
        }
    }

    shmdt(ready_queue);
    shmdt(pcb_store);
    shmdt(time_array);

    return 0;
}