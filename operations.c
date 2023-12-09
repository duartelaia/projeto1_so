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
int * threadState;

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

  return &event->data[index];
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


void* ems_create(void *arguments) {
  ThreadArguments *parsedArguments = (ThreadArguments*) arguments;
  threadState[parsedArguments->threadID] = 2;
  if (event_list == NULL) {
    writeToFile(parsedArguments->fdOut, "EMS state must be initialized\n");
    writeToFile(parsedArguments->fdOut, "Failed to create event\n");
    threadState[parsedArguments->threadID] = 1;
    pthread_exit((void*) -1);
  }

  if (get_event_with_delay(parsedArguments->event_id) != NULL) {
    writeToFile(parsedArguments->fdOut, "Event already exists\n");
    writeToFile(parsedArguments->fdOut, "Failed to create event\n");
    threadState[parsedArguments->threadID] = 1;
    pthread_exit((void*) -1);
  }

  struct Event* event = malloc(sizeof(struct Event));

  if (event == NULL) {
    writeToFile(parsedArguments->fdOut, "Error allocating memory for event\n");
    writeToFile(parsedArguments->fdOut, "Failed to create event\n");
    threadState[parsedArguments->threadID] = 1;
    pthread_exit((void*) -1);
  }

  event->id = parsedArguments->event_id;
  event->rows = parsedArguments->num_rows;
  event->cols = parsedArguments->num_columns;
  event->reservations = 0;
  event->data = malloc(parsedArguments->num_rows * parsedArguments->num_columns * sizeof(unsigned int));

  if (event->data == NULL) {
    writeToFile(parsedArguments->fdOut, "Error allocating memory for event data\n");
    free(event);
    writeToFile(parsedArguments->fdOut, "Failed to create event\n");
    threadState[parsedArguments->threadID] = 1;
    pthread_exit((void*) -1);
  }

  for (size_t i = 0; i < parsedArguments->num_rows * parsedArguments->num_columns; i++) {
    event->data[i] = 0;
  }

  if (append_to_list(event_list, event) != 0) {
    writeToFile(parsedArguments->fdOut, "Error appending event to list\n");
    free(event->data);
    free(event);
    writeToFile(parsedArguments->fdOut, "Failed to create event\n");
    threadState[parsedArguments->threadID] = 1;
    pthread_exit((void*) -1);
  }

  printf("%d : criou\n", parsedArguments->threadID);
  threadState[parsedArguments->threadID] = 1;
  return 0;
}

void* ems_reserve(void *arguments) {
  ThreadArguments *parsedArguments = (ThreadArguments*) arguments;
  threadState[parsedArguments->threadID] = 2;
  if (event_list == NULL) {
    writeToFile(parsedArguments->fdOut,  "EMS state must be initialized\n");
    writeToFile(parsedArguments->fdOut, "Failed to reserve seats\n");
  }

  struct Event* event = get_event_with_delay(parsedArguments->event_id);

  if (event == NULL) {
    writeToFile(parsedArguments->fdOut, "Event not found\n");
    writeToFile(parsedArguments->fdOut, "Failed to reserve seats\n");
  }

  unsigned int reservation_id = ++event->reservations;

  size_t i = 0;
  for (; i < parsedArguments->num_coords; i++) {
    size_t row = parsedArguments->xs[i];
    size_t col = parsedArguments->ys[i];

    if (row <= 0 || row > event->rows || col <= 0 || col > event->cols) {
      writeToFile(parsedArguments->fdOut, "Invalid seat\n");
      break;
    }

    if (*get_seat_with_delay(event, seat_index(event, row, col)) != 0) {
      writeToFile(parsedArguments->fdOut, "Seat already reserved\n");
      break;
    }
    *get_seat_with_delay(event, seat_index(event, row, col)) = reservation_id;
  }

  // If the reservation was not successful, free the seats that were reserved.
  if (i < parsedArguments->num_coords) {
    event->reservations--;
    for (size_t j = 0; j < i; j++) {
      *get_seat_with_delay(event, seat_index(event, parsedArguments->xs[j], parsedArguments->ys[j])) = 0;
    }
    writeToFile(parsedArguments->fdOut, "Failed to reserve seats\n");
  }

  printf("%d : reservou\n", parsedArguments->threadID);
  threadState[parsedArguments->threadID] = 1;
  return 0;
}

void* ems_show(void *arguments) {
  ThreadArguments *parsedArguments = (ThreadArguments*) arguments;
  threadState[parsedArguments->threadID] = 2;
  if (event_list == NULL) {
    writeToFile(parsedArguments->fdOut, "EMS state must be initialized\n");
    writeToFile(parsedArguments->fdOut, "Failed to show event\n");
    threadState[parsedArguments->threadID] = 1;
    pthread_exit((void*) -1);
  }

  struct Event* event = get_event_with_delay(parsedArguments->event_id);

  if (event == NULL) {
    writeToFile(parsedArguments->fdOut, "Event not found\n");
    writeToFile(parsedArguments->fdOut, "Failed to show event\n");
    threadState[parsedArguments->threadID] = 1;
    pthread_exit((void*) -1);
  }

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

  writeToFile(parsedArguments->fdOut,buffer);
  printf("%d : mostrou\n", parsedArguments->threadID);
  threadState[parsedArguments->threadID] = 1;
  return 0;
}

void* ems_list_events(void *arguments) {
  ThreadArguments *parsedArguments = (ThreadArguments*) arguments;
  threadState[parsedArguments->threadID] = 2;
  if (event_list == NULL) {
    writeToFile(parsedArguments->fdOut, "EMS state must be initialized\n");
    writeToFile(parsedArguments->fdOut, "Failed to list events\n");
    threadState[parsedArguments->threadID] = 1;
    pthread_exit((void*) -1);
  }

  if (event_list->head == NULL) {
    writeToFile(parsedArguments->fdOut,"No events\n");
    threadState[parsedArguments->threadID] = 1;
    pthread_exit((void*) -1);
  }

  struct ListNode* current = event_list->head;
  char buffer[5000];
  memset(buffer, 0, sizeof(buffer));
  char smallBuffer[10];

  while (current != NULL) {
    strcat(buffer,"Event: ");
    snprintf(smallBuffer, sizeof(smallBuffer), "%u", (current->event)->id);
    strcat(buffer, smallBuffer);
    strcat(buffer, "\n");
    current = current->next;
  }

  writeToFile(parsedArguments->fdOut, buffer);
  threadState[parsedArguments->threadID] = 1;
  printf("%d : listou\n", parsedArguments->threadID);
  return 0;
}

void* ems_wait(void *arguments) {
  ThreadArguments *parsedArguments = (ThreadArguments*) arguments;
  threadState[parsedArguments->threadID] = 2;
  struct timespec delay = delay_to_timespec(parsedArguments->delay);
  nanosleep(&delay, NULL);
  threadState[parsedArguments->threadID] = 1;
  return 0;
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
  int continueReading = 1;
  long unsigned int m = (long unsigned int) maxThreads;
  threadState = malloc(sizeof(int)*m);
  memset(threadState, 0, sizeof(int)*m);
  
  while(continueReading){

    for(int i = 0; i < maxThreads; i++){
      if (threadState[i] == 0){
        if (switchCase(fdin, fdout, &tid[i], i, threadState)==2){
          continueReading = 0;
          break;
        }
        printf("tarefa %d - criou a thread\n",i);
      }
    }

    for(int i = 0; i < maxThreads; i++){
      if(threadState[i] == 1){
        printf("deu join\n");
        pthread_join(tid[i], NULL);    
        threadState[i] = 0;
      }
    }
  }
  free(threadState);
  close(fdin);
  close(fdout);
  return 0;
}

