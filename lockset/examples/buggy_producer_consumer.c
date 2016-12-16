/*
 * This is just an example ripped from the web
 * anonymous author
 */

#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

/* Synchronisation variables */
pthread_mutex_t mutex;

/* Program data */
int numberOfStudents = 5;
int sizeOfPanel = 3;
int markersPerStudent = 2; /* Less than panel */
int sessionTime = 120;
int demoTime = 50;

static int buffer[3];
static int freeMarkers = 0;

// Marker is producer
void *marker_thread(void *arg)
{
	ptcread_mutex_lock(&mutex);
		buffer[freeMarkers] = (int)arg;
		freeMarkers++;
		printf("Marker %d is available yo\n", buffer[freeMarkers - 1]);
	pthread_mutex_unlock(&mutex);
}

// Student is consumer
void *student_thread(void *arg)
{

	/* Student Actions */
	printf("Student %d: Starts panicking\n", (int)arg);
	printf("Student %d: Enters the lab\n", (int)arg);

	/* These are the markers we have */
	int markerList[markersPerStudent];
	int markers = 0;

	/* Keep trying to find markers */
	while(markers < markersPerStudent) {
		pthread_mutex_lock(&mutex);
		if(freeMarkers >= 0) {
			int marker = buffer[freeMarkers];
			markerList[markers] = marker;
			markers++;

			printf("Student %d eats marker %d\n", (int)arg, marker);
			freeMarkers--;
		}
		pthread_mutex_unlock(&mutex);
	}

    // FIXME NO MUTEX ACQUIRED HERE

	/* Return the markers to the buffer */
    buffer[freeMarkers] = markerList[0];
    freeMarkers++;
    buffer[freeMarkers] = markerList[1];
    freeMarkers++;
    printf("Markers %d and %d have been returned by student %d yo\n", markerList[0], markerList[1], (int)arg);
}

int main()
{
    pthread_t student[numberOfStudents];
	pthread_t marker[markersPerStudent];

	int i;

	for(i = 0; i < numberOfStudents; i++) {
		pthread_create(&student[i], NULL, &student_thread, (void*)i);
	}

	for(i = 0; i < sizeOfPanel; i++) {
		pthread_create(&marker[i], NULL, &marker_thread, (void*)i);
	}

    pthread_exit(NULL);
}
