#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <string.h> /* memset */
#include <unistd.h> /* close */
#include <time.h>
#include <signal.h>
#include "uthread.h"
//**************************************************************************************************//
//					MACRO  DECLARATION                                          //
//**************************************************************************************************//
#define READY 1				// Thread is READY to run
#define BLOCKED 2				// Thread is BLOCKED because mutex is not available
#define DELAYED 3				// Thread is delayed by given period of time
#define TERMINATED 4			// Thread is terminating

#define WAITING_FOR_LOCK 1
#define AVAILABLE 0
#define NOT_AVAILABLE 1

#define NUMBER_OF_THREADS 100		// Maximum number of thread can be created is static

//****************************************************************************************************//
//					MATEX STRUCTURE						      //
//****************************************************************************************************//
struct mutex{
	int state;						// Stores mutex states i.e. AVAILABLE or NOT_AVAILABLE
	int attribute;					// Mutex attribute or value
	int locked_tid;					// Thread ID which obtain the lock
	int PI_priority;				// Priority of thread which experienced priority inversion and will get its original priority back with this variable
	int PI_id;						// Thread which experienced priority inversion
	int waiting_queue;				// Counter for number of waiting threads for mutex
	int wait_queue[50];				// Array stores thread ID waiting for mutex
	int PI_flag;					// Priority inversion flag figures out which thread had priority inversion
};

struct mutex mutex_t[20];			// Maximum number of mutex locks
sigset_t set;						// sigset_t struct for SIGNAL MASKING in order to take care of race condition

//****************************************************************************************************//
//					TCB QUEUE for individual thread 												  //
//****************************************************************************************************//
typedef struct tcb
{
	int state;						// THread state i.e. READY, BLOCKED, DELAYED, TERMINATED
	ucontext_t context;				// contex of thread
	int priority;					// Thread priority
	void *(*funct_name)(void*);		// Function name which thread is supposed to execute
	void* argument;					// Arguments for function
	void* return_value;				// Return value
	int join_request;				// Flag for raising join request
	ucontext_t return_context;		// Return contex while switching 
	timer_t timerid;				// Stores timer id when timer for respective thread is created
	struct sigevent *sevp;			// signal event structure
	int lock_state;					// Variable which stores value if thread has locked muex or not

}TCB_t;

//*****************************************************************************************************//
//					Gloabal variables declaration
//*****************************************************************************************************//


TCB_t tcb_array[100];		//TCB array maximum number of TCB is 100
int tid_counter = 0;		// Thread ID counter
int stack_size = 8192;		// Stack size
int current_tid = 0;		// curretn thread id with READY state
int lock_id=0;				// Mutex lock index
int enable_srp;				// srp enable Flag
int resource_tb_n;			// Number of element in resource table
resource_entry_t *resource_tb;
//	int n=0;


//*****************************************************************************************************//
//					Start wrapper is being called while makecontex() thread_create. 
//*****************************************************************************************************//
void start_wrapper()
{
	sigprocmask(SIG_UNBLOCK, &set, NULL);		// Mask signal
	void *ptr;									//pointer of return value
	ptr = tcb_array[current_tid].funct_name(tcb_array[current_tid].argument);
	uthread_exit(ptr);							// uthread exit is called
}


void scheduler(){
	int i, j, k, n=0;
	int prev = current_tid;
	int scheduler_tid = 0;
	int scheduler_priority = 101;

	for(i=0; i<NUMBER_OF_THREADS; i++){
		if(tcb_array[i].state == READY){	
			int priority_ceiling = 101;
//			if(enable_srp == 1){

// ********Switch 1 checks for srp enable and gets maximmum priority and thread ID

			switch(enable_srp){
				case 1:
					for(j = 0; j< resource_tb_n; j++){
						if(mutex_t[(int)(resource_tb[j].resource)].state == NOT_AVAILABLE && mutex_t[(int)(resource_tb[j].resource)].locked_tid != i){
							for(k=0; k<resource_tb[j].n; k++){
								if(tcb_array[(int)(resource_tb[j].thread_array[k])].priority < priority_ceiling){
									priority_ceiling = tcb_array[(int)(resource_tb[j].thread_array[k])].priority;
			
									scheduler_tid = i;
								}
							}
						}
					}
					if(tcb_array[i].priority < priority_ceiling && tcb_array[i].priority < scheduler_priority){
						scheduler_priority = tcb_array[i].priority;
						scheduler_tid = i;
					}
//					printf("THread priority is %d %d \n",tcb_array[i].priority, scheduler_priority);
				break;
//			else{
//********** Switch 0 means srp not enable and getes maximum priority thread ID	
				case 0:
					if(tcb_array[i].priority < scheduler_priority){
					scheduler_priority = tcb_array[i].priority;
					scheduler_tid = i;
//					printf("THread priority is %d %d %d %d %d\n",tcb_array[i].priority, tcb_array[1].state,tcb_array[2].state, tcb_array[3].state, current_tid);
					}
					break;
				default:
					printf("Switch case failed\n");
			
			}



			
		}
	}
	if(scheduler_tid == 0) {
//		printf("SLEEPS HERE current id is %d %d %d %d\n",current_tid, tcb_array[1].state,tcb_array[2].state,tcb_array[3].state);
		sigprocmask(SIG_UNBLOCK, &set, NULL);
//		if(tcb_array[current_tid].state != READY)
		sleep(-1);
	}
	else{
		if(scheduler_tid != current_tid ){

			current_tid = scheduler_tid;
//			printf("NOW THread came here %d\n", prev);
			if(prev !=0) swapcontext(&tcb_array[prev].context, &tcb_array[current_tid].context);
			else setcontext(&(tcb_array[current_tid].context));
		}
	}
//	printf("HELLO\n");
}


//******************************************************************************************//
//					SRP ENABLE FUNCTION
//******************************************************************************************//

int uthread_srp_enable(resource_entry_t *resource_table, int n){
		sigprocmask(SIG_BLOCK, &set, NULL);
		resource_tb_n = n;
		resource_tb = resource_table;
		enable_srp = 1;
		sigprocmask(SIG_UNBLOCK, &set, NULL);
//		printf("SRP ENABLED\n");
		return 0;
}


//*******************************************************************************************//
//					HANDLER----------when signal is raised it calls schedule
//*******************************************************************************************//

void handler(int sig_num, siginfo_t *siginfo, void *context){
//	printf("Handler is called %d and sign info %d \n", sig_num, siginfo->si_value.sival_int);
	tcb_array[siginfo->si_value.sival_int].state = READY;
	scheduler();
}

//*******************************************************************************************//
//					Signal handler function
//*******************************************************************************************//

void signal_function(){
		struct sigaction ha;			// sigaction declaration
		memset (&ha, 0, sizeof (ha));	// memory set for sigaction
		

		sigemptyset(&set);
		sigaddset(&set, SIGRTMIN);		

		
		ha.sa_flags = SA_SIGINFO;		// set sigaction flag

		ha.sa_sigaction = &handler;		// assignning handler function
		sigaction(SIGRTMIN, &ha, NULL);	
}


//********************************************************************************************//
//					UTHREAD CREATE------- Creates stack and allocates 
//********************************************************************************************//

int uthread_create(uthread_t *tid, void *(*start)(void *), void *args, int priority){
	 
	void *stack = malloc(8192); 	//allocate stack
	tid_counter++;					// TCB array counter index
	*tid = tid_counter;				// return thread ID
	memset(&(tcb_array[*tid].context), '\0', sizeof(tcb_array[*tid].context)); 
	getcontext(&(tcb_array[*tid].context));		// get context for given
	tcb_array[*tid].context.uc_stack.ss_sp = stack;		//put stack pointer
	tcb_array[*tid].context.uc_stack.ss_size = (size_t)stack_size;		//store stack size
	makecontext(&(tcb_array[*tid].context),(void (*)())&start_wrapper, 0);
	tcb_array[*tid].argument = args;
	tcb_array[*tid].priority = priority;
	tcb_array[*tid].funct_name = start;
	tcb_array[*tid].state = READY;		// Set created thread to READY
	tcb_array[*tid].sevp = malloc(sizeof(struct sigevent)); 	// sig event declaration

	if(tid_counter == 1){
		signal_function();

	}
return 0;
}


//*********************************************************************************************//
//						MUTEX INITIATE
//*********************************************************************************************//

int uthread_mutex_init(uthread_mutex_t *mutex, const int attr){
	mutex_t[lock_id].state = AVAILABLE;
	mutex_t[lock_id].attribute = attr;
	*mutex = lock_id;
	lock_id++;
	mutex_t[*mutex].waiting_queue = 0;
	mutex_t[*mutex].wait_queue[*mutex] = 0;
	mutex_t[*mutex].PI_flag = 0;
//	printf("IN INIT\n");
	return 0;
}


//********************************************************************************************//
//						UTHREAD MUTEX LOCK
//********************************************************************************************//

int uthread_mutex_lock(uthread_mutex_t *mutex){

	sigprocmask(SIG_BLOCK,&set,NULL);
	int o;
	here:
//**************IF lock is availble then make it NOT_AVAILABLE*****
//	printf("IN LOCK  %d ID\n", current_tid);
	if(mutex_t[*mutex].state == AVAILABLE){
		mutex_t[*mutex].state = NOT_AVAILABLE;
		mutex_t[*mutex].locked_tid = current_tid;

		if(mutex_t[*mutex].waiting_queue != 0) mutex_t[*mutex].waiting_queue--;
//	printf("IN LOCK  %d ID IN IF %d\n", current_tid, (int)*mutex);
	}
	else{

//*****************************IF checks for Priority inheritance**********
//		mutex_t[*mutex].queue_count++;
//	printf("IN LOCK  %d ID IN ELSE %d \n", current_tid, (int) *mutex);
		if(mutex_t[*mutex].attribute == UTHREAD_MUTEX_ATTR_PI && enable_srp == 0){
			if(tcb_array[current_tid].priority < tcb_array[mutex_t[*mutex].locked_tid].priority){
				if(mutex_t[*mutex].PI_flag == 0) {
					mutex_t[*mutex].PI_priority = tcb_array[mutex_t[*mutex].locked_tid].priority;
					mutex_t[*mutex].PI_flag = 1;	// set PI_flag = 1 which shows priority inheritacne happened on thread
//					printf("PI_FLAG and PI_priority value %d\n",mutex_t[*mutex].PI_priority);
				}
				tcb_array[mutex_t[*mutex].locked_tid].priority = tcb_array[current_tid].priority;
				mutex_t[*mutex].PI_id = mutex_t[*mutex].locked_tid;
			}
		}
		tcb_array[current_tid].state = BLOCKED; 	// set thread to BLOCKED, increment waiting queue and counter
		o = mutex_t[*mutex].waiting_queue;
		mutex_t[*mutex].wait_queue[o] = current_tid;
		mutex_t[*mutex].waiting_queue++;
//		printf("%d and %d for lock %d \n", mutex_t[*mutex].wait_queue[o], current_tid, (int )*mutex);		
//				sigprocmask(SIG_BLOCK, &set, NULL);	
		scheduler();
					
		goto here; // go to here: if there are no thread to run


	}
	sigprocmask(SIG_UNBLOCK, &set, NULL);	// UNmask the signal
return 0;   
}

//*********************************************************************************************//
//						MUTEX UNLOCK
//*********************************************************************************************//


int uthread_mutex_unlock(uthread_mutex_t *mutex){
	sigprocmask(SIG_BLOCK, &set, NULL);
//	printf("IN UNLOCK %d ID is %d \n", current_tid,(int )*mutex);	
	int i;
	int highest_priority =100;
	int thread_id;


	if(mutex_t[*mutex].attribute == UTHREAD_MUTEX_ATTR_PI){
		if(mutex_t[*mutex].PI_id == current_tid){
			tcb_array[mutex_t[*mutex].PI_id].priority = mutex_t[*mutex].PI_priority;
			mutex_t[*mutex].PI_flag = 0;
//		printf("IN UNLOCK %d ID IN IF %d\n", current_tid, (int) *mutex);
		}

	}
		
	mutex_t[*mutex].state = AVAILABLE;		// release lock


		if(mutex_t[*mutex].waiting_queue != 0){
//			printf("Value of waiting queue is %d \n",mutex_t[*mutex].waiting_queue);
			for(i=0; i<mutex_t[*mutex].waiting_queue; i++){
					if(tcb_array[mutex_t[*mutex].wait_queue[i]].priority < highest_priority && tcb_array[mutex_t[*mutex].wait_queue[i]].state == BLOCKED){
						highest_priority = tcb_array[mutex_t[*mutex].wait_queue[i]].priority;
						thread_id = i;
//						printf("value of thread id got %d \n",thread_id);
					}
				}
				tcb_array[mutex_t[*mutex].wait_queue[thread_id]].state = READY;
//				printf("NEXT LOCK ID %d and stste %d\n", thread_id,mutex_t[*mutex].wait_queue[thread_id]);
				mutex_t[*mutex].wait_queue[thread_id] = 0;
				mutex_t[*mutex].waiting_queue--;
//					sigprocmask(SIG_BLOCK, &set, NULL);	
				scheduler();
					sigprocmask(SIG_UNBLOCK, &set, NULL);	
		}
	
	return 0;
}

//***************************************************************************************//
//					GETTIME WRAPPER FUNCTION
//***************************************************************************************//

int uthread_gettime(struct timespec *tp){
	clock_gettime(CLOCK_MONOTONIC, tp);
//	printf("TV_SEC is %ld \n", tp->tv_sec);
	return 0;
}

//*********************************************************************************************//
//					NANOSLEEP
//*********************************************************************************************//


int uthread_abstime_nanosleep(const struct timespec *request){
	
	tcb_array[current_tid].sevp->sigev_notify = SIGEV_SIGNAL;
	tcb_array[current_tid].sevp->sigev_signo = SIGRTMIN;
	tcb_array[current_tid].sevp->sigev_value.sival_int = current_tid;
	

	timer_create(CLOCK_MONOTONIC, tcb_array[current_tid].sevp, &(tcb_array[current_tid].timerid)); // Timer create

	struct itimerspec *new_value = malloc(sizeof(struct itimerspec));		// memory allocation to timersepc

	new_value->it_interval.tv_sec = 0;		// set interval values to 0			
	new_value->it_interval.tv_nsec = 0;

	new_value->it_value.tv_sec = request->tv_sec;	// set values from arguement
	new_value->it_value.tv_nsec = request->tv_nsec;

	timer_settime(tcb_array[current_tid].timerid,TIMER_ABSTIME, new_value, NULL);	// set timer


	tcb_array[current_tid].state = DELAYED;		// set thread state to DELAYED
	sigprocmask(SIG_BLOCK, &set, NULL);	
	scheduler();								// call scheduler
	sigprocmask(SIG_UNBLOCK, &set, NULL);
	return 0;

}


//*********************************************************************************************//
//					THREAD JOIN
//*********************************************************************************************//



int uthread_join(uthread_t tid, void *(*value_ptr)){
	tcb_array[tid].join_request = 1;				// main calls this function and set join_request =1
	getcontext(&(tcb_array[tid].return_context));	// thread comes here when it finishes execution

	if(tcb_array[tid].state == TERMINATED){
	*value_ptr = tcb_array[tid].return_value;
		return 0;
	}
	else{
			sigprocmask(SIG_BLOCK, &set, NULL);			// if TERMINATED is not set then call scheduler
		scheduler();
//			sigprocmask(SIG_UNBLOCK, &set, NULL);
	}
	*value_ptr = tcb_array[tid].return_value;
	return 0;
}

//*********************************************************************************************//
//					THREAD EXIT CALLED FROM START WRAPPER
//*********************************************************************************************//



void uthread_exit(void *value_ptr){
	tcb_array[current_tid].state = TERMINATED;					// chnage thread state to terminate
	tcb_array[current_tid].return_value = value_ptr;			// store pointer to return value of tcb_array
	if(tcb_array[current_tid].join_request){					// if main() has already made request for join or not
		setcontext(&(tcb_array[current_tid].return_context));	// if so setcontext with return context
	}
	else{
			sigprocmask(SIG_BLOCK, &set, NULL);					// if not then call scheduler
		scheduler();
//			sigprocmask(SIG_UNBLOCK, &set, NULL);	
	}

}


