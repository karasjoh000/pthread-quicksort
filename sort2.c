#include "sort.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>

#define SORT_THRESHOLD      4//40

typedef struct _sortParams {
    char** array;
    int left;
    int right;
    struct _sortParams* next;
    int workerid;
} SortParams;

typedef struct _task_queue {
	SortParams *head;
	SortParams *tail;
} TaskQueue;

typedef struct _worker_status {
	int id;
	bool isWorking;
} WorkerStatus;

static pthread_cond_t workers;
static pthread_cond_t manager;
static pthread_mutex_t m_maximumThreads, m_queueTask, m_workerstat, m_managerwait;
static TaskQueue *tasks;
static int *threadid;
static int maximumThreads;  /* maximum # of threads to be used */
static WorkerStatus* workerstat;
pthread_t *threads;


static void initTaskQueue() {
	tasks = (TaskQueue*) malloc(sizeof(TaskQueue));
	tasks->head = tasks->tail = NULL;
	return;
}

static void insertIntoTasks(SortParams* new) {
	if(!tasks->head) {
		tasks->head = new;
		tasks->head->next = NULL;
		return;
	}
	new->next = tasks->head->next;
	tasks->head->next = new;
	return;
}

static SortParams *createTaskNode( int left, int right, char** array) {
	SortParams *temp = (SortParams*) malloc(sizeof(SortParams));
	temp->array = array;
	temp->left = left;
	temp->right = right;
	temp->next = NULL;
	return temp;
}

static SortParams *getTask() {
	if (!tasks->head) return NULL;
	SortParams *temp = tasks->head;
	tasks->head = temp->next;
	return temp;
}

static bool isQueueEmpty() {
	if(!tasks->head) return true;
	else return false;
}

/* This is an implementation of insert sort, which although it is */
/* n-squared, is faster at sorting short lists than quick sort,   */
/* due to its lack of recursive procedure call overhead.          */

static void insertSort(char** array, int left, int right, SortParams* params) {
    int i, j;
    for (i = left + 1; i <= right; i++) {
        char* pivot = array[i];
        j = i - 1;
        while (j >= left && (strcmp(array[j],pivot) > 0)) {
            array[j + 1] = array[j];
            j--;
        }
        array[j + 1] = pivot;
    }
    pthread_mutex_lock(&m_workerstat);
    workerstat[params->workerid].isWorking = false;
    pthread_mutex_unlock(&m_workerstat);
    if(left + 1 <= right) {
    	pthread_cond_signal(&manager);
    }
}

/* Recursive quick sort, but with a provision to use */
/* insert sort when the range gets small.            */

static void quickSort(void* p) {
    SortParams* params = (SortParams*) p;
    char** array = params->array;
    int left = params->left;
    int right = params->right;
    int i = left, j = right;

    if (j - i > SORT_THRESHOLD) {           /* if the sort range is substantial, use quick sort */

        int m = (i + j) >> 1;               /* pick pivot as median of         */
        char* temp, *pivot;                 /* first, last and middle elements */
        if (strcmp(array[i],array[m]) > 0) {
            temp = array[i]; array[i] = array[m]; array[m] = temp;
        }
        if (strcmp(array[m],array[j]) > 0) {
            temp = array[m]; array[m] = array[j]; array[j] = temp;
            if (strcmp(array[i],array[m]) > 0) {
                temp = array[i]; array[i] = array[m]; array[m] = temp;
            }
        }
        pivot = array[m];

        for (;;) {
            while (strcmp(array[i],pivot) < 0) i++; /* move i down to first element greater than or equal to pivot */
            while (strcmp(array[j],pivot) > 0) j--; /* move j up to first element less than or equal to pivot      */
            if (i < j) {
                char* temp = array[i];      /* if i and j have not passed each other */
                array[i++] = array[j];      /* swap their respective elements and    */
                array[j--] = temp;          /* advance both i and j                  */
            } else if (i == j) {
                i++; j--;
            } else break;                   /* if i > j, this partitioning is done  */
        }

	SortParams *first = createTaskNode(left, j, array);
	pthread_mutex_lock(&m_queueTask);
	insertIntoTasks(first);
	assert(!isQueueEmpty());
	pthread_mutex_unlock(&m_queueTask);
	pthread_cond_signal(&workers);

	SortParams *second = createTaskNode(i, right, array);
	pthread_mutex_lock(&m_queueTask);
	insertIntoTasks(second);
	assert(!isQueueEmpty());
	pthread_mutex_unlock(&m_queueTask);
	pthread_cond_signal(&workers);

	pthread_mutex_lock(&m_workerstat);
	workerstat[params->workerid].isWorking = false;
	pthread_mutex_unlock(&m_workerstat);

	free(params);

} else insertSort(array,i,j, params);           /* for a small range use insert sort */
}



/* user interface routine to set the number of threads sortT is permitted to use */

void setSortThreads(int count) {
    maximumThreads = count;
}

/* user callable sort procedure, sorts array of count strings, beginning at address array */

static void worker (void *p) {
	int id = *((int*) p);
	while (true) {
		pthread_mutex_lock(&m_queueTask);
		if (isQueueEmpty()) {
			pthread_cond_wait(&workers, &m_queueTask);
		}
		if (!isQueueEmpty()){
			SortParams *temp = getTask();
			pthread_mutex_unlock(&m_queueTask);
			temp->workerid = id;
			pthread_mutex_lock(&m_workerstat);
			workerstat[id].isWorking = true;
			pthread_mutex_unlock(&m_workerstat);
			quickSort(temp);
		}
		else pthread_mutex_unlock(&m_queueTask);
	}
}

static void dispatch(){
	threads = (pthread_t*) malloc(sizeof(pthread_t) * maximumThreads);

	for (int i = 0; i < maximumThreads; i++ ) {
		threadid[i] = i;
		pthread_create(&threads[i], NULL, (void*) worker, &threadid[i]);
	}
	return;
}

static bool areWorkersDone() {
	for (int i = 0; i < maximumThreads; i++) {
		if (workerstat[i].isWorking) return false;
	}
	return true;
}

static void releaseMem() {
	free(workerstat);
	free(tasks);
	free(threadid);
	free(threads);
}

static void cancelThreads() {
	for (int i = 0; i < maximumThreads; i++)
		pthread_cancel(threads[i]);
	return;
}

static void manageWorkers() {
	while (true) {
		pthread_mutex_lock(&m_managerwait);
		pthread_cond_wait(&manager, &m_managerwait);
		pthread_mutex_lock(&m_workerstat);
		if (areWorkersDone()) {
			pthread_mutex_unlock(&m_workerstat);
			cancelThreads();
			releaseMem();
			return;
		}
		pthread_mutex_unlock(&m_workerstat);

	}

}

static void initWorkerStatus() {
	workerstat = (WorkerStatus*) malloc(sizeof(WorkerStatus) * maximumThreads);
	for (int i = 0; i < maximumThreads; i++) {
		workerstat[i].id = i;
		workerstat[i].isWorking = false;
	}
}

void sortThreaded(char** array, unsigned int count) {

    pthread_mutex_init(&m_maximumThreads, NULL);
    pthread_mutex_init(&m_queueTask, NULL);
    pthread_mutex_init(&m_workerstat, NULL);

    pthread_cond_init(&workers, NULL);
    pthread_cond_init(&manager, NULL);

    threadid = malloc( sizeof(int) * maximumThreads);

    SortParams *parameters = createTaskNode( 0, count-1, array );

    initTaskQueue();

    insertIntoTasks(parameters);

    assert(!isQueueEmpty());

    initWorkerStatus();

    dispatch();

    pthread_cond_signal(&workers);

    manageWorkers();
}
