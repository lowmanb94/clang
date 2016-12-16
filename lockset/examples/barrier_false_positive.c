#include <pthread.h>

int i;

pthread_barrier_t b;

void thread_func(void *arg)
{
    int tid = (unsigned*)arg;

    if (tid == 0)
        i++; 

   // barrier wait
   pthread_barrier_wait(&b);
   
    // thread 0 not access i at the same time
    if (tid == 1)
        i++;
}

int main()
{
    pthread_barrier_init(&b, NULL, 2);

    // launch threads
    pthread_t t0; 
    pthread_t t1; 
    pthread_create(&t0, NULL, thread_func, (void*)0);
    pthread_create(&t1, NULL, thread_func, (void*)1);
}
