/*
   John Karasev
   CS 360 Systems Programming
   WSUV Spring 2018
   -----------------------------------------------------
   Assignment #8:
   Quicksort using a "thread pool".
*/

#include "sort.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#define SORT_THRESHOLD      40   // cannot be zero otherwise insertSort will not be called and
// no one will signal parent to check if finished. (see insertSort)

typedef struct _sortParams {     // used to store what parts of the array
	char** array;                // need to be sorted. Also stores the threadid
	int left;                    // so thread knows what worker status flag to set
	int right;                   // false after sorting.
	struct _sortParams* next;    // next is here so it can be in the stack.
	int workerid;
} SortParams;

typedef struct _task_stack {     // the stack struct. Can store more info if
	SortParams *head;        // needed.
} TaskStack;

typedef struct _worker_status {  // indicates what workers are working and
	int id;                  // who are idle. This is used when manager
	bool isWorking;          // needs to check if sorting is done.
} WorkerStatus;

static pthread_cond_t workers;   // used for worker threads.
static pthread_cond_t manager;   // used for manager thread (parent).
static pthread_mutex_t m_stackTask,     // for accessing stack
       m_workerstat,    // for accessing worker status array
       m_signalmanager, // to prevent all threads calling to manager at same time.
       m_managerwait;   // a seperate mutex when manager is waiting
// ( manager cannot wait on m_workerstat
// because it can be locked by one of the
// workers after a singal is sent to manager).

static TaskStack *tasks;         // pointer to tasks stack.
static int *threadid;            // pointer to array of thread ids.
static int maximumThreads;       // maximum # of threads to be used
static WorkerStatus* workerstat;
pthread_t *threads;              // pointer to array of worker threads.


static void initTaskStack() {    // allocates space for stack pointer and sets to NULL.
	tasks = ( TaskStack* ) malloc( sizeof( TaskStack ) );
	tasks->head = NULL;
	return;
}
// inserts new task into stack
static void insertIntoTasks( SortParams* new ) {
	if( !tasks->head ) {
		tasks->head = new;
		tasks->head->next = NULL;
		return;
	}
	new->next = tasks->head->next;
	tasks->head->next = new;
	return;
}
// creates a task node to be inserted into stack.
static SortParams *createTaskNode( int left, int right, char** array ) {
	SortParams *temp = ( SortParams* ) malloc( sizeof( SortParams ) );
	temp->array = array;
	temp->left = left;
	temp->right = right;
	temp->next = NULL;
	return temp;
}

static SortParams *getTask() {      // reteives a task for the worker (if any).
	if ( !tasks->head ) return NULL;
	SortParams *temp = tasks->head;
	tasks->head = temp->next;
	return temp;
}

static bool isStackEmpty() {        // true if stack empty else false.
	if( !tasks->head ) return true;
	else return false;
}

/* This is an implementation of insert sort, which although it is */
/* n-squared, is faster at sorting short lists than quick sort,   */
/* due to its lack of recursive procedure call overhead.          */

static void insertSort( char** array, int left, int right, SortParams* params ) {
	int i, j;
	for ( i = left + 1; i <= right; i++ ) {
		char* pivot = array[i];
		j = i - 1;
		while ( j >= left && ( strcmp( array[j], pivot ) > 0 ) ) {
			array[j + 1] = array[j];
			j--;
		}
		array[j + 1] = pivot;
	}
	pthread_mutex_lock( &m_workerstat );    // set work status to false.
	workerstat[params->workerid].isWorking = false;
	pthread_mutex_unlock( &m_workerstat );
	// since it is the last stage in sorting try to singal parent to check if all workers are done.

	pthread_mutex_lock( &m_signalmanager ); //restrict to one signal at a time.

	pthread_cond_signal( &manager );

	pthread_mutex_lock( &m_managerwait );  // only will lock if not all wokers are done
	pthread_mutex_unlock( &m_managerwait ); // release right away and give other threads access to signal.
	pthread_mutex_unlock( &m_signalmanager );


}

/* Recursive quick sort, but with a provision to use */
/* insert sort when the range gets small.            */

static void quickSort( void* p ) {
	SortParams* params = ( SortParams* ) p;
	char** array = params->array;
	int left = params->left;
	int right = params->right;
	int i = left, j = right;

	if ( j - i > SORT_THRESHOLD ) {         /* if the sort range is substantial, use quick sort */

		int m = ( i + j ) >> 1;             /* pick pivot as median of         */
		char* temp, *pivot;                 /* first, last and middle elements */
		if ( strcmp( array[i], array[m] ) > 0 ) {
			temp = array[i];
			array[i] = array[m];
			array[m] = temp;
		}
		if ( strcmp( array[m], array[j] ) > 0 ) {
			temp = array[m];
			array[m] = array[j];
			array[j] = temp;
			if ( strcmp( array[i], array[m] ) > 0 ) {
				temp = array[i];
				array[i] = array[m];
				array[m] = temp;
			}
		}
		pivot = array[m];

		for ( ;; ) {
			while ( strcmp( array[i], pivot ) < 0 ) i++; /* move i down to first element greater than or equal to pivot */
			while ( strcmp( array[j], pivot ) > 0 ) j--; /* move j up to first element less than or equal to pivot      */
			if ( i < j ) {
				char* temp = array[i];      /* if i and j have not passed each other */
				array[i++] = array[j];      /* swap their respective elements and    */
				array[j--] = temp;          /* advance both i and j                  */
			} else if ( i == j ) {
				i++;
				j--;
			} else break;                   /* if i > j, this partitioning is done  */
		}

		SortParams *first = createTaskNode( left, j, array );
		pthread_mutex_lock( &m_stackTask ); // wait for access to task stack
		insertIntoTasks( first );
		pthread_mutex_unlock( &m_stackTask );
		pthread_cond_signal( &workers );    // signal a worker to start working

		SortParams *second = createTaskNode( i, right, array );
		pthread_mutex_lock( &m_stackTask );
		insertIntoTasks( second );
		pthread_mutex_unlock( &m_stackTask );
		pthread_cond_signal( &workers );
		// wait for access to worker status
		pthread_mutex_lock( &m_workerstat ); // then set to false and leave.
		workerstat[params->workerid].isWorking = false;
		pthread_mutex_unlock( &m_workerstat );

	} else insertSort( array, i, j, params ); /* for a small range use insert sort */

	free( params );

}



/* user interface routine to set the number of threads sortT is permitted to use */

void setSortThreads( int count ) {
	maximumThreads = count;
}

/* user callable sort procedure, sorts array of count strings, beginning at address array */

static void worker ( void *p ) {
	int id = *( ( int* ) p );
	while ( true ) {
		pthread_mutex_lock( &m_stackTask );
		if ( isStackEmpty() ) {                     // if no tasks wait for a job.
			pthread_cond_wait( &workers, &m_stackTask );
		}
		if ( !isStackEmpty() ) {                    // if stack not empty start working.

			SortParams *temp = getTask();

			pthread_mutex_lock( &m_workerstat ); // note that stack was locked first, see manageWorkers.
			pthread_mutex_unlock( &m_stackTask ); // lock status before unlocking stack because parent might get a hold of it,
			// Which can result in parent cancelling threads before they are finished.


			temp->workerid = id;                // set the worker id to later set working flag to false
			workerstat[id].isWorking = true;    // set to true before going to sort.
			pthread_mutex_unlock( &m_workerstat );
			quickSort( temp );
		}
		else pthread_mutex_unlock( &m_stackTask );  // otherwise go wait.
	}
}

static void dispatch() {
	// borrow space for number of
	// threads specified by maximumThreads.
	threads = ( pthread_t* ) malloc( sizeof( pthread_t ) * maximumThreads );

	for ( int i = 0; i < maximumThreads; i++ ) {        // send workers to their routine
		threadid[i] = i;
		pthread_create( &threads[i], NULL, ( void* ) worker, &threadid[i] );
	}
	return;
}

static bool areWorkersDone() {
	if ( !isStackEmpty() ) return false;
	for ( int i = 0; i < maximumThreads; i++ ) {        // when manager is signaled,
		if ( workerstat[i].isWorking ) return false; // it checks if all workers are
	}						    // are idle (if not finished,
	return true;                                        // at least one worker will be
}							    // sorting) and if stack is empty.


static void releaseMem() {				    // release all memory that was borrowed.
	free( workerstat );
	free( tasks );
	free( threadid );
	free( threads );
}

static void cancelThreads() {				    //after sorting is finished cancel all threads.
	for ( int i = 0; i < maximumThreads; i++ )
		pthread_cancel( threads[i] );
	return;
}

static void manageWorkers() {                     // parent thread routine that checks weather sorting is finished.
	while ( true ) {
		pthread_mutex_lock( &m_managerwait );
		pthread_cond_wait( &manager, &m_managerwait );

		pthread_mutex_lock( &m_stackTask ); // note here that stack locked first
		pthread_mutex_lock( &m_workerstat ); // then workerstat. Same in worker routine.
		// if not consistent can result in dead lock.

		if ( areWorkersDone() ) {
			cancelThreads();
			releaseMem();
			return;
		}

		pthread_mutex_unlock( &m_stackTask );
		pthread_mutex_unlock( &m_workerstat );
		pthread_mutex_unlock( &m_managerwait ); // when unlocked, worker will lock and let others singal.
		// see insert sort.
	}

}

static void initWorkerStatus() {  // set working flags to false for each worker thread.
	workerstat = ( WorkerStatus* ) malloc( sizeof( WorkerStatus ) * maximumThreads );
	for ( int i = 0; i < maximumThreads; i++ ) {
		workerstat[i].id = i;
		workerstat[i].isWorking = false;
	}
}

void sortThreaded( char** array, unsigned int count ) {

	pthread_mutex_init( &m_stackTask, NULL );     //init mutex vars
	pthread_mutex_init( &m_workerstat, NULL );
	pthread_mutex_init( &m_signalmanager, NULL );

	pthread_cond_init( &workers, NULL );          //init cond vars (thread pools)
	pthread_cond_init( &manager, NULL );

	threadid = malloc( sizeof( int ) * maximumThreads ); // to pass into worker thread function worker().

	SortParams *parameters = createTaskNode( 0, count - 1, array ); //create the first task node

	initTaskStack();

	insertIntoTasks( parameters );

	assert( !isStackEmpty() );

	initWorkerStatus();  //set worker status to false

	dispatch();   // send worker threads to worker() function

	pthread_cond_signal( &workers ); //signal workers to check Task Stack.

	manageWorkers();  // will check when workers will be done.
}
