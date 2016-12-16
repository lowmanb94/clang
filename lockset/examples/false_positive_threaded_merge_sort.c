/*
 * C Jordan Mincey cjm9fw
 * Project 1
 * Fall 2016 Operating Systems
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define NON_VALUE 5000
#define MAIN 0
#define WORKER 1
#define STK_SIZE (256*1024)

/* contains global array input information */
struct nums {
   int size;//size of array that was inputted
   int thread_count;
   short *p;//array of input values
};

/* For first eight entries in the array, we would have {0, 8} */
struct thread_data {
   int start;//first entry included in segment
   int finish;//first entry outside segment
};

/* Prototypes */
void *start(void *arg);
void sort(struct thread_data *my_data);
void read_file(const char *file);
void output_result();
int next_power_of_two(int x);
int number_of_iterations();

/* global variables */
struct nums data;
pthread_t *threads;

int turn;
int active_threads;
int next_region;
int region_size = 2;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t main_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t worker_cond = PTHREAD_COND_INITIALIZER;
pthread_attr_t attr;

int main(int argc, char *argv[]){
   int i;
   if(argc != 2){
      fprintf(stderr, "USAGE: %s input_file\n", argv[0]);
      exit(1);
   }

   turn = MAIN;

   read_file(argv[1]);
   //printf("finished read file\n");
   
   threads = malloc(data.thread_count * sizeof(pthread_t));
   if(threads == NULL){
      fprintf(stderr, "Something went wrong allocating threads\n");
      exit(1);
   }
   
   active_threads = data.thread_count;
   next_region = data.size;
   
   pthread_attr_init(&attr);
   pthread_attr_setstacksize(&attr,STK_SIZE);

   turn = WORKER;

   //release the thread pull
   for(i=0; i<data.thread_count; i++)
      pthread_create(&threads[i], &attr, start, NULL);
   
   while(1){
      pthread_mutex_lock(&mutex);
      while(turn == WORKER)//wait while workers go
         pthread_cond_wait(&main_cond, &mutex);
      next_region = data.size;
      region_size *= 2;
      active_threads = data.size/region_size;
      
      if(region_size > data.size)
         break;
      
      pthread_mutex_unlock(&mutex);

      turn = WORKER;

      //wake up some workers
      for(i=0; i<data.size/region_size; i++){
         pthread_cond_signal(&worker_cond);
      }
   }
   output_result();
   
   //cleanup
   for(i=0; i<data.thread_count; i++)
      pthread_cancel(threads[i]);
   
   free(threads);
   free(data.p);
   pthread_mutex_destroy(&mutex);
   pthread_cond_destroy(&worker_cond);
   pthread_cond_destroy(&main_cond);
   pthread_attr_destroy(&attr);
}

/* functions threads will use */
void *start(void *arg){
   struct thread_data my_data;

   //printf("starting\n");
   
   pthread_mutex_lock(&mutex);
   while(1){
      //find my area
      next_region -= region_size;
      my_data.start = next_region;
      my_data.finish = my_data.start + region_size;
      pthread_mutex_unlock(&mutex);

      //sort the region
      sort(&my_data);

      //printf("finished\n");
      
      pthread_mutex_lock(&mutex);
      --active_threads;
      if(active_threads <= 0){
         //tell main
         turn = MAIN;
         //printf("starting next round\n");
         pthread_mutex_unlock(&mutex);
         pthread_cond_signal(&main_cond);
      }
      do {//wait for the main thread
        
         /* ##############################
          *
          * Lockset cannot reason about the 
          * condition variable
          *
          * This causes the false positive 
          * on region_size in line 124
          *
         * ############################### */

         pthread_cond_wait(&worker_cond, &mutex);
      } while(turn == MAIN || next_region <= 0);
   }
}

/* function to do sorting each thread will use */
void sort(struct thread_data *my_data){
   const int length = my_data->finish - my_data->start,
             halfway = length/2 + my_data->start;

   //first and second are indexes we are comparing
   int first = my_data->start, second = halfway, i;
   short *temp = (short *)malloc(sizeof(short) * length);
   if(temp == NULL){
      fprintf(stderr, "Something went wrong allocating temp\n");
      exit(1);
   }
   
   for(i=0; i<length; i++){
      if((data.p[first] < data.p[second] && first < halfway) ||
           (second >= my_data->finish)){
         temp[i] = data.p[first++];
      } else {
         temp[i] = data.p[second++];
      }
   }

   //copy to the real location
   for(i=0; i<length; i++){
      data.p[my_data->start + i] = temp[i];
   }
   
   free(temp);
}

/* Reads in input file and stores it in an array */
void read_file(const char *file){
   int input[4096];
   int i, count=0;
   struct nums loc;
   FILE *fp = fopen(file, "r");
   if(fp == NULL){
      fprintf(stderr, "Could not open file '%s'\n", file);
      exit(1);
   }

   while(fscanf(fp, "%d", &input[count]) > 0) count++;
   fclose(fp);
   
   if(count <= 0 || count > 4096){
      perror("Invalid input\n");
      exit(1);
   }

   data.size = next_power_of_two(count);
   data.thread_count = data.size/2;
   
   data.p = malloc(data.size * sizeof(int));
   if(data.p == NULL){
      fprintf(stderr, "Something went wrong allocating p\n");
      exit(1);
   }
   for(i=0; i<data.size; i++){
      data.p[i] = (i<count) ? (short)input[i] : NON_VALUE;
   }
}

/* prints out the data in the specified format */
void output_result(){
   int i, count;
   for(i=0; i<data.size; i++){
      if(data.p[i] == NON_VALUE){
         printf("\n");
         break;
      } else if(i+1 == data.size ||
              data.p[i+1] == NON_VALUE){
         printf("%d\n", data.p[i]);
         break;
      } else if((i+1) % 10 == 0)
         printf("%d\n", data.p[i]);
      else
         printf("%d ", data.p[i]);
   } 
}

/* return next power of two based on input */
int next_power_of_two(int x){
   int i=2;
   while(i<x) i *= 2;
   return i;
}
