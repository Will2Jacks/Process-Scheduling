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

void timer_exit(int signo)
{
    if(signo == SIGINT)
    {
        shmdt(ready_queue);
        shmdt(pcb_store);
        shmdt(time_array);

        printf("The timer is exiting\n");
        exit(0);
    }
}

int num_processes = 0;

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

    signal(SIGINT,timer_exit);

    semaphore_lock(semid_sem_timer);
        time_array[4] = getpid();
    semaphore_unlock(semid_sem_timer);

    semaphore_lock(semid_sem_SYNC);

        semaphore_lock(semid_sem_timer);
            time_array[0] = 0;
        semaphore_unlock(semid_sem_timer);

    semaphore_unlock(semid_sem_SYNC);

    while(1)
    {
        usleep((DELTA + delta) * 1000);

        semaphore_lock(semid_sem_timer);
            time_array[0]++;

            int current_time = time_array[0];
            int next_interrupt_time = time_array[2];
            int current_running_process = time_array[1];

            if(current_running_process != -1 && current_time == next_interrupt_time)
            {
                int process_PID = 0;
                semaphore_lock(semid_sem_PCB);
                    process_PID = pcb_store[current_running_process].pid;
                semaphore_unlock(semid_sem_PCB);

                kill(process_PID,SIGUSR1);
            }
        semaphore_unlock(semid_sem_timer);

        semaphore_lock(semid_sem_SYNC);
            usleep(delta * 1000);
        semaphore_unlock(semid_sem_SYNC);
    }

}