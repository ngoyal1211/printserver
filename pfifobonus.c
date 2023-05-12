//This code simulates a print server, specifially the producer-consumer problem and processes the first print request first using first in first out method.
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

//The struct for a print queue
struct printStruct
{
    long inTime;
    int producerNum;
    int size;
    sem_t sem;
};
typedef struct printStruct printItem;

//buffer where all print requests will go
printItem *buffer;

//use two separate indeces for fifo
//Read index is a global variable and write index is on shared memory
int readIndex;
int *writeIndex;
 
/* initially buffer will be empty.  full_sem
   will be initialized to buffer SIZE, which means
   SIZE number of producer threads can write to it.
   And empty_sem will be initialized to 0, so no
   consumer can read from buffer until a producer
   thread posts to empty_sem */
sem_t *full_sem;  /* when 0, buffer is empty */
sem_t *empty_sem; /* when 0, buffer is full. Kind of
                    like an index for the buffer */


long *totalJobs;
int shmid, shmid1, shmid2, shmid3, shmid4;
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
        int jobSize = rand() % (1000 - 10 + 1) + 10;
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
        if ((*writeIndex) < SIZE) {

	    sem_wait(&(buffer[(*writeIndex)].sem));
	    buffer[(*writeIndex)].producerNum = producerNum;
	    buffer[(*writeIndex)].size = jobSize;
            buffer[(*writeIndex)].inTime = startTime.tv_nsec;
            printf("Producer %d added %d to buffer\n", producerNum, jobSize);
	    sem_post(&(buffer[(*writeIndex)].sem));
            *writeIndex = (((*writeIndex)+1)%SIZE);
        } 
        else {
            printf("Buffer overflow\n");
        }
        (*totalJobs)++;
        sem_post(full_sem); // post (increment) fullbuffer semaphore
    }
}
 
//method for consumer threads to go through the queue and dequeue any jobs in the queue
void *consumer(void *thread_n) {
    int thread_numb = *(int *)thread_n;
    while (1) {
        sleep(rand() % 1);
        sem_wait(full_sem);
          // there could be race condition here, that could cause
          // buffer underflow error
        if ((readIndex) > -1) {
	    sem_wait(&(buffer[readIndex].sem));
            struct timespec currentTime;
            clock_gettime(CLOCK_REALTIME, &currentTime);
            long currTime = currentTime.tv_nsec;
            long waitTime = (currTime - buffer[(readIndex)].inTime)/1000;
            totalwaitTime = totalwaitTime + waitTime;

	    printf("Consumer %d dequeue %d, %d from buffer\n", thread_numb, buffer[(readIndex)].producerNum, buffer[(readIndex)].size);
	    sem_post(&(buffer[readIndex].sem));
            readIndex = ((readIndex + 1)%SIZE);
        } 
        else {
            printf("Buffer underflow\n");
        }
        sem_post(empty_sem); // post (increment) emptybuffer semaphore
   }
}

//method to clean up the shared memory
void cleanUpSharedMemory()
{
	shmdt(buffer);
	shmdt(full_sem);
	shmdt(empty_sem);
        shmdt(totalJobs);
	shmdt(writeIndex);
	shmctl(shmid, IPC_RMID, NULL);
	shmctl(shmid1, IPC_RMID, NULL);
	shmctl(shmid2, IPC_RMID, NULL);
	shmctl(shmid3, IPC_RMID, NULL);
	shmctl(shmid4, IPC_RMID, NULL);
}

//method for graceful termination if program is interrupted when Ctrl C is called
//cancels all consumer threads, destroys all semaphores, and cleans up the shared memory
void sigIntHandler(int sig_num)
{
	printf("Ctrl C --- Clean exit \n");
	sleep(10);
	for(int i = 0; i < numOfConsumers; i++)
	{
	     pthread_cancel(thread[i]);
	     pthread_join(thread[i], NULL);
	}


        for (int i = 0; i < SIZE; i++)
	{
	     sem_destroy(&(buffer[i].sem));
	}	     
	sem_destroy(empty_sem);
	sem_destroy(full_sem);

	cleanUpSharedMemory();
	exit(0);
}

int main(int argc, char **argv) {
    //checks to make sure the number of producers and the number of consumers was passed in 
    if (argc != 3)
    {
       printf("Must input number of producers and number of consumers");
       exit(0);
    }
    int numOfProducers = atoi(argv[1]);
    int numOfConsumers = atoi(argv[2]);
    //used to calculate average waiting time of the program
    totalwaitTime = 0;
    readIndex = 0;

    struct timespec start;
    clock_gettime(CLOCK_REALTIME, &start); 

 
    //creates all the shared memory

    shmid = shmget(IPC_PRIVATE, sizeof(printItem)*SIZE, IPC_CREAT | 0666);
    buffer = (printItem*)shmat(shmid, NULL, 0);

    shmid1 = shmget(IPC_PRIVATE, sizeof(sem_t), IPC_CREAT | 0666);
    full_sem = (sem_t*)shmat(shmid1, NULL, 0);

    shmid3 = shmget(IPC_PRIVATE, sizeof(sem_t), IPC_CREAT | 0666);
    empty_sem = (sem_t*)shmat(shmid3, NULL, 0);

    shmid2 = shmget(IPC_PRIVATE, sizeof(long), IPC_CREAT | 0666);
    totalJobs = (long*)shmat(shmid2, NULL, 0);
    *totalJobs = 0;

    shmid4 = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    writeIndex = (int*)shmat(shmid4, NULL, 0);
    *writeIndex = 0;


    // set mutex to work with multi-processes
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

    //initializes the individual semaphores for each queue element
    for (int i = 0; i < SIZE; i++)
    {
	    sem_init(&(buffer[i].sem),
			    1,
			    1);
    }

    //creates all the consumer threads
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
    for (int i=0; i<numOfProducers; i++)
    {
        if ((pids[i] = fork()) == 0)
        {
            producer(i);
            exit(100 + i);
        }
    }
 
    // adding signal handler only for main process
    signal(SIGINT, sigIntHandler);
 
    int stat;
    // Waits for all child processes to complete.
    for (int i=0; i<numOfProducers; i++)
    {
        waitpid(pids[i], &stat, 0);
    }

    // wait for all consumer threads to complete printing and then terminate
    // all producers have finsihed and now we wait for all consumers to to finish consuming all the print jobs the stops all the threads, destroys all the semaphores, and cleans up all the shared memory
    while (1)
    {
	 if (readIndex == *writeIndex)
	 {
             for (int i = 0; i < numOfConsumers; i++)
             {
	         pthread_cancel(thread[i]);
                pthread_join(thread[i], NULL);
	     }
 
	    for (int i = 0; i < SIZE; i++)
	    {
		  sem_destroy(&(buffer[i].sem));
	    }
            sem_destroy(full_sem);
            sem_destroy(empty_sem);

            struct timespec end;
            clock_gettime(CLOCK_REALTIME, &end);
            printf("\n\nTotal Execution time: %ld microseconds, Average Wait Time: %ld microseconds \n", ((end.tv_nsec - start.tv_nsec)/1000), (totalwaitTime/(*totalJobs)));

            cleanUpSharedMemory();
	    break;
	     
	 }
    }
    return 0;
}
