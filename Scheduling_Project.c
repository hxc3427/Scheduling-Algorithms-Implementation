/*
 ============================================================================
 Name        : Scheduling_Project.c
 Author      : Harshdeep
 Version     :
 Copyright   : Your copyright notice
 Description : Scheduling Algorithm in C
 ============================================================================
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

#include <time.h>
#include <sys/trace.h>
#include <sys/neutrino.h>
#include <sys/netmgr.h>
#include <sys/siginfo.h>

pthread_mutex_t count_mutex;

int time_keep = 0;
bool end_thread = false;
pthread_mutex_t timekeeper_mutex;
pthread_cond_t timeskeeper_condition;

#define timekeeper_period 10000 //in nanoseconds
#define max_time 400
#define scheduling_scheme 3

pthread_cond_t count_threshold;



int no_of_iters;


typedef struct {
    ///adding all the required stuff///
    pthread_t thread;
    int thread_id;
    int execution_time;
    int deadline;
    int period;
    int cycles;
    bool finish;
    int prio;
    int nxt_deadline;
    int slack_time;
    int last_time_slot;
}thread_info_t;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void *time_keeper(void* empty){
    //set priority
    pthread_setschedprio(pthread_self(), 11);

    //initialize timer
    struct timespec timekeeper_out;
    //value will change when timer interrupts occur
    timekeeper_out.tv_nsec=timekeeper_period;

    /* Initialize mutex and condition variable objects */
    pthread_mutex_init(&timekeeper_mutex, NULL);
    pthread_cond_init (&timeskeeper_condition, NULL);

    pthread_mutex_lock(&timekeeper_mutex);

    int wait;

    while(end_thread == false) {

        sleepon_t *pl;

        _sleepon_init( &pl, 0);
        _sleepon_lock(pl);

        _sleepon_wait( pl,&wait,5000000);
        _sleepon_unlock(pl);
        _sleepon_destroy(pl);


        TraceEvent(_NTO_TRACE_INSERTSUSEREVENT, 103, 1, 11);


        //start time keep
        time_keep++;
        if(time_keep==max_time){
            end_thread= true;
        }

        pthread_cond_signal(&count_threshold);

    }

    pthread_mutex_unlock(&timekeeper_mutex);
    pthread_exit(NULL);

}

int get_time_keeper(void) {
    return time_keep;
}

void reset_time_slice(void) {
    time_keep = 0;
}


////////////////////////IDLE THREAD////////////////////////////////////////////////////////////////////////////////////////////////
void *idle(void *info) {
    pthread_setschedprio(pthread_self(), 2);
    while(end_thread == false);
    pthread_exit(NULL);
}
//////////////////////////PROCESS THREAD//////////////////////////////////////////////////////////////////////////////////////////////
void *process_thread(void *info){

    thread_info_t *my_info= (thread_info_t*) info;
    my_info= my_info->thread_id;

    while (end_thread==false){

        struct sched_param param;
        int policy;

        //setting priority of all task threads high and then dropping them. this will ensures that all threads are in front of the queue
        pthread_getschedparam(my_info->thread,&policy,&param);

        TraceEvent(_NTO_TRACE_INSERTSUSEREVENT, 104, 1, 11);

        my_info->cycles=0;

        int x;
        for(x=0;x<my_info->execution_time;x++){
            //wait for 0.01 seconds
            nanospin_ns(5000000);
            (my_info->cycles)++;
        }
        TraceEvent(_NTO_TRACE_INSERTSUSEREVENT, 105, 1, 11);

        my_info->finish=true;
        //unlocking thread by giving signal
        pthread_cond_signal(&count_threshold);
    }
    pthread_exit(NULL);
}

/////////////////////////////SCHEDULAR THREAD///////////////////////////////////////////////////////////////////////////////////////////
void *schedular(void *array_for_thread_info_t){

    thread_info_t * all_the_threads= (thread_info_t *) array_for_thread_info_t;

    int policy;
    struct sched_param param;
    pthread_getschedparam(pthread_self(), &policy, &param);
    ///setting high priority (10) for other threads priority will be low
    param.sched_priority = 10;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    pthread_getschedparam(pthread_self(),&policy, &param);

    ///wait for timer to trip,then synchronize
    pthread_cond_wait(&count_threshold, &count_mutex);

    pthread_mutex_lock(&count_mutex);

    // Wait for the time slice to trip so we can synchronize
    pthread_cond_wait(&count_threshold, &count_mutex);


    //call function to reset timer
    reset_time_slice();

    //call function to calculate next thread
    calculate_next_thread(all_the_threads);

   //call next thread
    run_next_thread (all_the_threads);

    pthread_cond_wait(&count_threshold, &count_mutex);
    //reset timer

    int i;
    for(i = 0;i<3;i++){
       all_the_threads[i].finish=false;
    }

    while(end_thread==false){

        TraceEvent(_NTO_TRACE_INSERTSUSEREVENT, 100, 1, 11);
        //check for missed deadlines
        deadline_misses(all_the_threads);

        //check for new periods
        new_periods (all_the_threads);

        //Calculate next thread
        calculate_next_thread(all_the_threads);

        //run next thread
        run_next_thread (all_the_threads);

        no_of_iters++;
        TraceEvent(_NTO_TRACE_INSERTSUSEREVENT, 101, 1, 11);


        pthread_cond_wait(&count_threshold, &count_mutex);

        int x;
        for (x=0; x<3; x++) {
            struct sched_param param;
            int policy;
            pthread_getschedparam(all_the_threads[x].thread, &policy,&param);
       }
    }

    int x;
    for (x=0; x<3; x++) {
        all_the_threads[x].prio = 1;
        //run next thread
        run_next_thread(all_the_threads);

        //run next thread
        run_next_thread (all_the_threads);

        pthread_mutex_unlock(&count_mutex);
        pthread_exit(NULL);
    }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void deadline_misses(thread_info_t * all_the_threads){
    int i;
    for(i=0;i<3;i++){
        if(all_the_threads[i].finish){
            if(time_keep%all_the_threads[i].deadline==0){
                if(all_the_threads[i].last_time_slot!= time_keep){
                    TraceEvent(_NTO_TRACE_INSERTSUSEREVENT, 106, 1, 11);
                }
            }
        }
    }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void new_periods(thread_info_t * all_the_threads){
    int i;
    for(i=0;i<3;i++){
        if (all_the_threads[i].finish == true) {
            if(time_keep%all_the_threads[i].deadline ==0) {
                if (all_the_threads[i].last_time_slot != time_keep) {
                    all_the_threads[i].last_time_slot = time_keep;
                    all_the_threads[i].finish = false;
                }
            }
        }
    }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void run_next_thread (thread_info_t * all_the_threads){
    int i;
       for(i=0;i<3;i++){
           //setting high priority and then dropping so that all the threads are in front of queue
           pthread_setschedprio(all_the_threads[i].thread, 8);
           pthread_setschedprio(all_the_threads[i].thread, all_the_threads[i].prio);
       }
       for(i=0;i<3;i++){
           struct sched_param param;
           int policy;
           pthread_getschedparam(all_the_threads[i].thread, &policy,&param);
       }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void calculate_next_thread(thread_info_t * all_the_threads){
    int x,counter,temp,y;

    int thread_with_lowest_deadline=0;
    int lowest_deadline = 9999999;
    int thread_with_earliest_deadline=0;
    int earliest_deadline=99999;
    int thread_with_least_slack_time=0;
    int least_slack_time=0;
    bool exit = true;

    switch(scheduling_scheme){

        case 1:
            thread_with_lowest_deadline=99;
            for(x=0;x<3;x++){
                //reseting priorities of all the threads
                all_the_threads[x].prio=1;
                if(all_the_threads[x].finish==false && all_the_threads[x].deadline < lowest_deadline){
                    lowest_deadline = all_the_threads[x].deadline;
                    thread_with_lowest_deadline=x;
                }
            }
            if(thread_with_lowest_deadline !=99){
               all_the_threads[thread_with_lowest_deadline].prio=8;
            }
            break;

        case 2:
            ///EDF
            counter= get_time_keeper();

            //Calculating next deadline of all the threads
            for(y=0;y<3;y++){
                temp = all_the_threads[y].deadline;
                exit = false;
                while(!exit){
                    if(temp>counter){
                        if(all_the_threads[y].finish == true){
                            temp+=all_the_threads[y].deadline;
                        }
                       all_the_threads[y].nxt_deadline=temp;
                       exit = true;
                    }
                    temp+=all_the_threads[y].deadline;
                }
            }

            if(thread_with_earliest_deadline !=99){
                all_the_threads[thread_with_earliest_deadline].prio=8;
            }
            break;

        case 3:
        //LST
        //calculating LST
        for(y=0;y<3;y++){
            all_the_threads[y].slack_time=all_the_threads[y].execution_time - (all_the_threads[y].cycles/40);
        }
        thread_with_least_slack_time=99;
        for(y=0;y<3;y++){
            //reseting priorities of all the threads
            all_the_threads[y].prio=1;
            if(all_the_threads[y].finish==false && all_the_threads[y].deadline< least_slack_time){
                least_slack_time= all_the_threads[y].slack_time;
                thread_with_least_slack_time=y;
            }
        }
        if(thread_with_least_slack_time !=99){
            all_the_threads[thread_with_least_slack_time].prio=8;
        }
        break;
        default:
        break;
    }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main(void) {

    pthread_t scheduler_thread, idle_thread,time_keeper_thread;
    thread_info_t Thread_run[3]= {{0}};
///////////////////////////////////////////////////////////////////////////////////////
    //(1,7,7);(2,5,5);(1,8,8)
    ///process1
    Thread_run[0].thread_id = 0;
    Thread_run[0].execution_time = 1;
    Thread_run[0].deadline = 7;
    Thread_run[0].period = 7;
    Thread_run[0].finish = true;

    ///process2
    Thread_run[1].thread_id = 0;
    Thread_run[1].execution_time = 2;
    Thread_run[1].deadline = 5;
    Thread_run[1].period = 5;
    Thread_run[1].finish = true;

    ///process3
    Thread_run[2].thread_id = 0;
    Thread_run[2].execution_time = 1;
    Thread_run[2].deadline = 8;
    Thread_run[2].period = 8;
    Thread_run[2].finish = true;
///////////////////////////////////////////////////////////////////////////////////////

    /* Initialize mutex and condition variable objects */
    pthread_mutex_init(&count_mutex, NULL);
    pthread_cond_init (&count_threshold, NULL);

    //CREATING PROCESS THREADS
    pthread_create(&Thread_run[0].thread,NULL,process_thread,&Thread_run[0]);
    pthread_create(&Thread_run[1].thread,NULL,process_thread,&Thread_run[1]);
    pthread_create(&Thread_run[2].thread,NULL,process_thread,&Thread_run[2]);

    pthread_create(&scheduler_thread,NULL,schedular,Thread_run);
    pthread_create(&idle_thread,NULL,idle,NULL);
    pthread_create(&time_keeper_thread,NULL,time_keeper, NULL);

    int i;
    for(i=0;i<3;i++){
        pthread_join(Thread_run[i].thread,NULL);
    }

    /* Clean up and exit */
    pthread_mutex_destroy(&count_mutex);
    pthread_cond_destroy(&count_threshold);
    pthread_exit(NULL);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
