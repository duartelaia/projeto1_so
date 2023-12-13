#include <stdio.h>
#include <stdlib.h>
#include <time.h>
// novos 
// imports novos a partir daqui 
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "eventlist.h"
#include "operations.h"
#include "inputAux.h"

static struct EventList* event_list = NULL;
static unsigned int state_access_delay_ms = 0;
/// Calculates a timespec from a delay in milliseconds.
/// @param delay_ms Delay in milliseconds.
/// @return Timespec with the given delay.
static struct timespec delay_to_timespec(unsigned int delay_ms) {
  return (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
}

/// Gets the event with the given ID from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event_id The ID of the event to get.
/// @return Pointer to the event if found, NULL otherwise.
static struct Event* get_event_with_delay(unsigned int event_id) {
  struct timespec delay = delay_to_timespec(state_access_delay_ms);
  nanosleep(&delay, NULL);  // Should not be removed

  return get_event(event_list, event_id);
}

/// Gets the seat with the given index from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event Event to get the seat from.
/// @param index Index of the seat to get.
/// @return Pointer to the seat.
static unsigned int* get_seat_with_delay(struct Event* event, size_t index) {
  struct timespec delay = delay_to_timespec(state_access_delay_ms);
  nanosleep(&delay, NULL);  // Should not be removed

  return &event->data[index].value;
}

/// Gets the index of a seat.
/// @note This function assumes that the seat exists.
/// @param event Event to get the seat index from.
/// @param row Row of the seat.
/// @param col Column of the seat.
/// @return Index of the seat.
static size_t seat_index(struct Event* event, size_t row, size_t col) { return (row - 1) * event->cols + col - 1; }

int writeToFile(int fd, char * buffer){
  ssize_t bytes_written = write(fd, buffer, strlen(buffer));
  if (bytes_written < 0){
    fprintf(stderr, "write error: %s\n", strerror(errno));
    return -1;
  }
  return 0;
}

void swap(size_t *x1, size_t *x2) {
  size_t temp = *x1;
  *x1 = *x2;
  *x2 = temp;
}

void sortReserves(size_t comparisonArr[], size_t otherArr[], size_t n) {
  for (size_t i = 0; i < n-1; i++) {
    for (size_t j = 0; j < n-i-1; j++) {
      if (comparisonArr[j] > comparisonArr[j+1]) {
        swap(&comparisonArr[j], &comparisonArr[j+1]);
        swap(&otherArr[j], &otherArr[j+1]);
      }
    }
  }
}

int ems_init(unsigned int delay_ms) {
  if (event_list != NULL) {
    fprintf(stderr, "EMS state has already been initialized\n");
    return 1;
  }

  event_list = create_list();
  state_access_delay_ms = delay_ms;

  return event_list == NULL;
}

int ems_terminate() {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  free_list(event_list);
  event_list = NULL;
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (get_event_with_delay(event_id) != NULL) {
    fprintf(stderr, "Event already exists\n");
    return 1;
  }

  struct Event* event = malloc(sizeof(struct Event));

  if (event == NULL) {
    fprintf(stderr, "Error allocating memory for event\n");
    return 1;
  }

  pthread_rwlock_rdlock(&event_list->rwlock);

  event->id = event_id;
  event->rows = num_rows;
  event->cols = num_cols;
  event->reservations = 0;
  pthread_rwlock_init(&event->rwlock,NULL);
  event->data = malloc(num_rows * num_cols * sizeof(struct data));

  if (event->data == NULL) {
    fprintf(stderr, "Error allocating memory for event data\n");
    free(event);
    pthread_rwlock_unlock(&event_list->rwlock);
    return 1;
  }

  for (size_t i = 0; i < num_rows * num_cols; i++) {
    event->data[i].value = 0;
    pthread_mutex_init(&event->data[i].mutex, NULL);
  }

  if (append_to_list(event_list, event) != 0) {
    fprintf(stderr, "Error appending event to list\n");
    free(event->data);
    free(event);
    pthread_rwlock_unlock(&event_list->rwlock);
    return 1;
  }

  pthread_rwlock_unlock(&event_list->rwlock);
  return 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  struct Event* event = get_event_with_delay(event_id);

  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  }

  pthread_rwlock_rdlock(&(event->rwlock));

  unsigned int reservation_id = ++event->reservations;

  sortReserves(xs, ys, num_seats);
  sortReserves(ys, xs, num_seats);

  size_t i = 0;
  for (; i < num_seats; i++) {
    size_t row = xs[i];
    size_t col = ys[i];

    if (row <= 0 || row > event->rows || col <= 0 || col > event->cols) {
      fprintf(stderr, "Invalid seat\n");
      break;
    }

    pthread_mutex_lock(&event->data->mutex);
    
    if (*get_seat_with_delay(event, seat_index(event, row, col)) != 0) {
      fprintf(stderr, "Seat already reserved\n");
      pthread_mutex_unlock(&event->data->mutex);
      break;
    }

    *get_seat_with_delay(event, seat_index(event, row, col)) = reservation_id;

    pthread_mutex_unlock(&event->data->mutex);
  }

  // If the reservation was not successful, free the seats that were reserved.
  if (i < num_seats) {
    event->reservations--;
    for (size_t j = 0; j < i; j++) {
      *get_seat_with_delay(event, seat_index(event, xs[j], ys[j])) = 0;
    }
    pthread_rwlock_unlock(&(event->rwlock));
    return 1;
  }


  pthread_rwlock_unlock(&(event->rwlock));
  return 0;
}

int ems_show(unsigned int event_id, int fd) {

  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  struct Event* event = get_event_with_delay(event_id);

  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  }

  pthread_rwlock_wrlock(&(event->rwlock));

  char buffer[10000];
  char smallBuffer[10];
  memset(buffer, 0, sizeof(buffer));

  for (size_t i = 1; i <= event->rows; i++) {
    for (size_t j = 1; j <= event->cols; j++) {
      unsigned int* seat = get_seat_with_delay(event, seat_index(event, i, j));

      snprintf(smallBuffer, sizeof(smallBuffer), "%u", *seat);
      strcat(buffer, smallBuffer);

      if (j < event->cols) {
        strcat(buffer, " ");
      }
    }

    strcat(buffer, "\n");
  }

  writeToFile(fd,buffer);


  pthread_rwlock_unlock(&(event->rwlock));
  return 0;
}

int ems_list_events(int fd) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (event_list->head == NULL) {
    printf("No events\n");
    return 0;
  }

  pthread_rwlock_wrlock(&event_list->rwlock);

  struct ListNode* current = event_list->head;

  char buffer[10000];
  memset(buffer, 0, sizeof(buffer));
  char smallBuffer[10];

  while (current != NULL) {
    strcat(buffer,"Event: ");
    snprintf(smallBuffer, sizeof(smallBuffer), "%u", (current->event)->id);
    strcat(buffer, smallBuffer);
    strcat(buffer, "\n");
    current = current->next;
  }

  writeToFile(fd, buffer);
  
  pthread_rwlock_unlock(&event_list->rwlock);
  return 0;
}

void ems_wait(unsigned int delay_ms) {
  struct timespec delay = delay_to_timespec(delay_ms);
  nanosleep(&delay, NULL);
}

int ems_file(char * dirPath,char * filename, int maxThreads){
  char filePathIn[strlen(dirPath)+strlen(filename)+2];
  snprintf(filePathIn, sizeof(filePathIn), "%s/%s", dirPath, filename);

  char filePathOut[strlen(dirPath)+strlen(filename)+2];
  char fileNameParsed[strlen(filename)];
  memset(fileNameParsed, 0, sizeof(fileNameParsed));
  strncpy(fileNameParsed, filename, strlen(filename)-5);
  snprintf(filePathOut, sizeof(filePathOut), "%s/%s.out", dirPath, fileNameParsed);

  int fdout = open(filePathOut,O_CREAT | O_TRUNC | O_WRONLY , S_IRUSR | S_IWUSR);
  if (fdout < 0){
      fprintf(stderr, "open error: %s\n", strerror(errno));
      return -1;
  }

  int fdin = open(filePathIn,O_RDONLY);
  if (fdin < 0){
      fprintf(stderr, "open error: %s\n", strerror(errno));
      return -1;
  }

  pthread_t tid[maxThreads];
  int continueReading = 1, threadsFinished=0, barrierFound=0, activeThreads=0;

  int threadState[maxThreads];
  memset(threadState, 0, sizeof(threadState));

  unsigned int threadWait[maxThreads];
  memset(threadWait, 0, sizeof(threadWait));

  int threadResult[maxThreads];
  memset(threadResult, 0, sizeof(threadResult));

  pthread_mutex_t parseMutex = PTHREAD_MUTEX_INITIALIZER;

  while(continueReading){
    for(int i = 0; i < maxThreads; i++){
      if (threadState[i] == 0){
        Arguments * arguments = malloc(sizeof(struct arguments));
        arguments->fdin = fdin;
        arguments->fdout = fdout;
        arguments->threadState = threadState;
        arguments->id = i;
        arguments->parseMutex = &parseMutex;
        arguments->threadResult = threadResult;
        arguments->threadWait = threadWait;
        if(i != 0 && threadResult[i-1]==1){
          free(arguments);
          barrierFound = 1;
          break;
        }
        if(pthread_create(&tid[i], 0, switchCase, arguments) != 0){
          fprintf(stderr, "Error creating thread\n");
        }
        activeThreads++;
      }
    }

    threadsFinished = 0;
    while (threadsFinished == 0 || barrierFound){
      for(int i = 0; i < maxThreads; i++){
        if(threadState[i] == 1){
          if(pthread_join(tid[i], NULL)){
            fprintf(stderr, "Error joining thread\n");
          }

          if(threadResult[i]==0){
            continueReading = 0;
          }
          else if(threadResult[i]==1){
            barrierFound = 1;
          }
          
          threadState[i] = 0;
          threadsFinished++;
          activeThreads--;
        }
      }
      if(barrierFound && activeThreads==0)
        barrierFound = 0;
    }
  }
  
  for(int i = 0; i < maxThreads; i++){
    if (threadState[i] != 0){
      int *result = NULL;
      if(pthread_join(tid[i], (void **)&result)){
        fprintf(stderr, "Error joining thread\n");
      }
      free(result);
    }
  }


  close(fdin);
  close(fdout);
  return 0;
}

