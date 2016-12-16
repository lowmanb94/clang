#include <pthread.h>

int arr[2];

void thread_func(void *arg)
{
   // always write to same location
   arr[ ((int)arg) % 2 ] = 0;
}

int main(int argc, char **argv)
{
    // launch threads
    pthread_t t0; 
    pthread_t t1; 
    pthread_create(&t0, NULL, thread_func, (void*)argc);
    pthread_create(&t1, NULL, thread_func, (void*)argc);
}
