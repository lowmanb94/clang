#include <pthread.h>
#include <stdio.h>

int i;

void thread_func(void *arg)
{
    printf("%i\n", i);
}

int main()
{
    // initialize i
    i = 1;
     
    // launch threads
    pthread_t t0; 
    pthread_create(&t0, NULL, thread_func, NULL);
}
