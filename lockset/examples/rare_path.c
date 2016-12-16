#include <pthread.h>

int i;

void thread_func(void *arg)
{
    i++;
}

int main(int argc, char **argv)
{
    // launch threads
    pthread_t t0; 
    pthread_create(&t0, NULL, thread_func, NULL);

    // rarely taken path 
    // (test case not likely to reveal)
    if (argc == 7531)
        i++;
}
