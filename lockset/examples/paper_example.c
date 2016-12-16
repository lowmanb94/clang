#include <pthread.h>

int i;

pthread_mutex_t m0;
pthread_mutex_t m1;

void thread_func_0(void *arg)
{
    // protect i with m0
    pthread_mutex_lock(&m0);
        i++; 
    pthread_mutex_unlock(&m0);
}

void thread_func_1(void *arg)
{
    // protect i with m1
    pthread_mutex_lock(&m1);
        i++; 
    pthread_mutex_unlock(&m1);
}

int main()
{
    // launch threads
    pthread_t t0; 
    pthread_t t1; 
    pthread_create(&t0, NULL, thread_func_0, (void*)0);
    pthread_create(&t1, NULL, thread_func_1, (void*)1);
}
