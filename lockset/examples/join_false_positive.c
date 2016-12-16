#include <pthread.h>

int i;

void thread_func(void *arg)
{
   i++; 
}

int main()
{
   // launch thread
   pthread_t t; 
   pthread_create(&t, NULL, thread_func, NULL);

   // join on thread 
   pthread_join(t, NULL);

   // access i after join
   // will cause false positive
   i++;
}
