#include <pthread.h>

void thread_func(void *arg)
{
    // all threads writing to same location in 2D array
    // this could be a common typo
    ((long**)arg)[0] = 1;
}

int main()
{
    // dynamically initialze 2D int array
    int **arr = malloc(2*sizeof(int*));
    arr[0]    = malloc(sizeof(int));
    arr[1]    = malloc(sizeof(int));

    pthread_t t0;
    pthread_t t1;
    pthread_create(&t0, NULL, thread_func, (void*)arr);
    pthread_create(&t1, NULL, thread_func, (void*)arr);
}
