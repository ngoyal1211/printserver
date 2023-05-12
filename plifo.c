//This code simulates a print server, specifically the producer-consumer problem and processes the last print request first using last in first out method.
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/shm.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

//print queue size
#define SIZE 20

//The struct for a print request.
struct printStruct
{
    long inTime;
    int producerNum;
    int size;
};
typedef struct printStruct printItem;

//the buffer where all the print requests will go
printItem *buffer;
int *buffer_index;
 
/* initially buffer will be empty.  full_sem
   will be initialized to buffer SIZE, which means
   SIZE number of producer threads can write to it.
   And empty_sem will be initialized to 0, so no
   consumer can read from buffer until a producer
   thread posts to empty_sem */
sem_t *full_sem;  /* when 0, buffer is empty */
sem_t *empty_sem; /* when 0, buffer is full. Kind of
                    like an index for the buffer */

//semaphore used to control the producers
sem_t *index_sem;


long *totalJobs;
int shmid, shmid1, shmid2, shmid3, shmid4, shmid5;
long totalwaitTime;
int numOfConsumers;
pthread_t *thread;

//method for a producer/process to create jobs and add them to the queue 
void *producer(int producerNum) {
    int i=0;
    int prodLoops = rand() % 20;
    struct timespec startTime;
    srand(producerNum);
    while (i++ < prodLoops) {
        int jobSize = rand() % 901 + 100;
        //sleep for one-hundreth of job size
        sleep(rand() % (jobSize/100));
        
        clock_gettime(CLOCK_REALTIME, &startTime);

	sem_wait(empty_sem); // sem=0: wait. sem>0: go and decrement it
        /* possible race condition here. After this thread wakes up,
           another thread could aqcuire mutex before this one, and add to list.
           Then the list would be full again
           and when this thread tried to insert to buffer there would be
           a buffer overflow error */

        //creates the print job
        if ((*buffer_index) < SIZE) {
	    sem_wait(index_sem);
	    buffer[(*buffer_index)].producerNum = producerNum;
	    buffer[(*buffer_index)].size = jobSize;
            buffer[(*buffer_index)].inTime = startTime.tv_nsec;
            printf("Producer %d added %d to buffer\n", producerNum, jobSize);
            (*buffer_index)++;
	    sem_post(index_sem);
        } 
        else {
            printf("Buffer overflow\n");
        }
        (*totalJobs)++;
        sem_post(full_sem); // post (increment) fullbuffer semaphore
    }
}

//method for a consumer to read the queue and dequeue a job if there is any
void *consumer(void *thread_n) {
    int thread_numb = *(int *)thread_n;
    while (1) {
        sleep(rand() % 1);
        sem_wait(full_sem);
          
          
        if ((*buffer_index) > 0) {
	    sem_wait(index_sem);
            *buffer_index = *buffer_index - 1;
            struct timespec currentTime;
            clock_gettime(CLOCK_REALTIME, &currentTime);
            long currTime = currentTime.tv_nsec;
            long waitTime = (currTime - buffer[(*buffer_index)].inTime)/1000;
            totalwaitTime = totalwaitTime + waitTime;

	    printf("Consumer %d dequeue %d, %d from buffer\n", thread_numb, buffer[(*buffer_index)].producerNum, buffer[(*buffer_index)].size);
	    sem_post(index_sem);
        } 
        else {
            printf("Buffer underflow\n");
        }
        sem_post(empty_sem); // post (increment) emptybuffer semaphore
   }
}

//method to clean all the shared memory created
void cleanUpSharedMemory()
{
	shmdt(buffer);
	shmdt(full_sem);
	shmdt(empty_sem);
        shmdt(totalJobs);
	shmdt(buffer_index);
	shmdt(index_sem);
	shmctl(shmid, IPC_RMID, NULL);
	shmctl(shmid1, IPC_RMID, NULL);
	shmctl(shmid2, IPC_RMID, NULL);
	shmctl(shmid3, IPC_RMID, NULL);
	shmctl(shmid4, IPC_RMID, NULL);
	shmctl(shmid5, IPC_RMID, NULL);
}	

//method for graceful termination if the program is interrupted using Ctrl C
//cancels all consumer threads running, destroys all semaphores, and cleans the shared memory
void sigIntHandler(int sig_num)
{
	printf("Ctrl C --- Clean exit \n");
	sleep(10);
	for(int i = 0; i < numOfConsumers; i++)
	{
	     pthread_cancel(thread[i]);
	     pthread_join(thread[i], NULL);
	}

	sem_destroy(index_sem);
	sem_destroy(full_sem);
	sem_destroy(empty_sem);

	cleanUpSharedMemory();
	exit(0);
}

int main(int argc, char **argv) {
    //checks to make sure number of producers and number of consumers was passed in
    if (argc != 3)
    {
       printf("Must input number of producers and number of consumers");
       exit(0);
    }
    int numOfProducers = atoi(argv[1]);
    int numOfConsumers = atoi(argv[2]);
    //used to calculate average wait time of program
    totalwaitTime = 0;

    struct timespec start;
    clock_gettime(CLOCK_REALTIME, &start); 
 
    //creates all shared memory

    shmid = shmget(IPC_PRIVATE, sizeof(printItem)*SIZE, IPC_CREAT | 0666);
    buffer = (printItem*)shmat(shmid, NULL, 0);

    shmid1 = shmget(IPC_PRIVATE, sizeof(sem_t), IPC_CREAT | 0666);
    full_sem = (sem_t*)shmat(shmid1, NULL, 0);

    shmid4 = shmget(IPC_PRIVATE, sizeof(sem_t), IPC_CREAT | 0666);
    empty_sem = (sem_t*)shmat(shmid4, NULL, 0);

    shmid2 = shmget(IPC_PRIVATE, sizeof(long), IPC_CREAT | 0666);
    totalJobs = (long*)shmat(shmid2, NULL, 0);
    *totalJobs = 0;

    shmid3 = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    buffer_index = (int*)shmat(shmid3, NULL, 0);
    *buffer_index = 0;

    shmid5 = shmget(IPC_PRIVATE, sizeof(sem_t), IPC_CREAT | 0666);
    index_sem = (sem_t*)shmat(shmid5, NULL, 0);


    sem_init(empty_sem, // sem_t *sem
             1, // int pshared. 0 = shared between threads of process,  1 = shared between processes
             SIZE); // unsigned int value. Initial value
    sem_init(full_sem,
             1,
             0);
    /* empty_sem is initialized to buffer size because SIZE number of
       producers can add one element to buffer each. They will wait
       semaphore each time, which will decrement semaphore value.
       full_sem is initialized to 0, because buffer starts empty and
       consumer cannot take any element from it. They will have to wait
       until producer posts to that semaphore (increments semaphore
       value) */
    sem_init(index_sem,
		    1,
		    1);



    //creates threads for consumers
    pthread_t consumerThreads[numOfConsumers];
    thread = (pthread_t*)(&consumerThreads);
    int thread_numb[numOfConsumers];
    for (int i = 0; i < numOfConsumers; i++) {        
        thread_numb[i] = i;
        // playing a bit with thread and thread_numb pointers...
        pthread_create(&thread[i], // pthread_t *t
                       NULL, // const pthread_attr_t *attr
                       consumer, // void *(*start_routine) (void *)
                       &thread_numb[i]);  // void *arg
    }
 
    //create processes for print job producers
    pid_t pids[numOfProducers];
    for (int i = 0; i < numOfProducers; i++)
    {
        if ((pids[i] = fork()) == 0)
        {
            producer(i);
            exit(100 + i);
        }
    }

   //When program is interrupted with Ctrl C, only main process will process signal and proceed to graceful termination  
    signal(SIGINT, sigIntHandler);

    int stat;
    // Waits for all child processes to complete.
    for (int i=0; i<numOfProducers; i++)
    {
        waitpid(pids[i], &stat, 0);
    }

    // wait for all consumer threads to complete printing and then terminates
    //all producers have finished and now we wait for all consumers to finish consuming all the print jobs then stops all the threads, destroys all semaphores and cleans all shared memory
    while (1)
    {
	 if (*buffer_index == 0)
	 {
             for (int i = 0; i < numOfConsumers; i++)
             {
	         pthread_cancel(thread[i]);
                 pthread_join(thread[i], NULL);
	     }
 
            sem_destroy(full_sem);
            sem_destroy(empty_sem);
	    sem_destroy(index_sem);

            struct timespec end;
            clock_gettime(CLOCK_REALTIME, &end);
            printf("\n\nTotal Execution time: %ld microseconds, Average Wait Time: %ld microseconds \n", ((end.tv_nsec - start.tv_nsec)/1000), (totalwaitTime/(*totalJobs)));

            cleanUpSharedMemory();
	    break;
	     
	 }
    }
    return 0;
}
