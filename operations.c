#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>

#include "eventlist.h"
#include "constants.h"
#include "operations.h"
#include "parser.h"

pthread_mutex_t parseMutex;         // Lock for parsing command
pthread_rwlock_t createEventLock;   // Lock for creating events
pthread_rwlock_t waitCommandLock;   // Lock for the Wait command
int barrierFound = 0;               // Flag for Barrier command
unsigned int * threadWait;          // List of time for each thread to wait before executing

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

  pthread_rwlock_rdlock(&createEventLock);
  if (get_event_with_delay(event_id) != NULL) {
    fprintf(stderr, "Event already exists\n");
    pthread_rwlock_unlock(&createEventLock);
    return 1;
  }
  pthread_rwlock_unlock(&createEventLock);

  struct Event* event = malloc(sizeof(struct Event));

  if (event == NULL) {
    fprintf(stderr, "Error allocating memory for event\n");
    return 1;
  }

  event->id = event_id;
  event->rows = num_rows;
  event->cols = num_cols;
  atomic_store(&event->reservations, 0);
  event->data = malloc(num_rows * num_cols * sizeof(struct data));

  if (event->data == NULL) {
    fprintf(stderr, "Error allocating memory for event data\n");
    free(event);
    return 1;
  }

  for (size_t i = 0; i < num_rows * num_cols; i++) {
    event->data[i].value = 0;
    pthread_rwlock_init(&event->data[i].seatLock, NULL);
  }

  pthread_rwlock_wrlock(&createEventLock);
  if (append_to_list(event_list, event) != 0) {
    fprintf(stderr, "Error appending event to list\n");
    free(event->data);
    free(event);
    pthread_rwlock_unlock(&createEventLock);
    return 1;
  }

  pthread_rwlock_unlock(&createEventLock);
  return 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {

  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  pthread_rwlock_rdlock(&createEventLock);
  struct Event* event = get_event_with_delay(event_id);
  pthread_rwlock_unlock(&createEventLock);

  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  }

  unsigned int lastReservation = atomic_load(&event->reservations);
  unsigned int reservation_id = ++lastReservation;
  atomic_store(&event->reservations, reservation_id);

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

    size_t seatIndex = seat_index(event, row, col);
    pthread_rwlock_wrlock(&event->data[seatIndex].seatLock);
    
    if (*get_seat_with_delay(event, seatIndex) != 0) {
      fprintf(stderr, "Seat already reserved\n");
      pthread_rwlock_unlock(&event->data[seatIndex].seatLock);
      break;
    }

    *get_seat_with_delay(event, seatIndex) = reservation_id;
  }
  
  // If the reservation was not successful, free the seats that were reserved.
  if (i < num_seats) {
    atomic_store(&event->reservations, --reservation_id);
    for (size_t j = 0; j < i; j++) {
      size_t seatIndex = seat_index(event, xs[j], ys[j]);
      *get_seat_with_delay(event, seatIndex) = 0;
      pthread_rwlock_unlock(&event->data[seatIndex].seatLock);
    }
    return 1;
  }else{
    for (size_t j = 0; j < i; j++) {
      size_t seatIndex = seat_index(event, xs[j], ys[j]);
      pthread_rwlock_unlock(&event->data[seatIndex].seatLock);
    }
    return 0;
  }
}

int ems_show(unsigned int event_id, int fd) {

  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  pthread_rwlock_rdlock(&createEventLock);
  struct Event* event = get_event_with_delay(event_id);
  pthread_rwlock_unlock(&createEventLock);


  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  }

  char buffer[10000];
  char smallBuffer[10];
  memset(buffer, 0, sizeof(buffer));

  for (size_t i = 1; i <= event->rows; i++) {
    for (size_t j = 1; j <= event->cols; j++) {

      size_t seatIndex = seat_index(event, i, j);
      pthread_rwlock_rdlock(&event->data[seatIndex].seatLock);

      unsigned int* seat = get_seat_with_delay(event, seatIndex);

      snprintf(smallBuffer, sizeof(smallBuffer), "%u", *seat);
      strcat(buffer, smallBuffer);

      if (j < event->cols) {
        strcat(buffer, " ");
      }
    }

    strcat(buffer, "\n");
  }


  for (size_t i = 1; i <= event->rows; i++) {
    for (size_t j = 1; j <= event->cols; j++) {
      size_t seatIndex = seat_index(event, i, j);
      pthread_rwlock_unlock(&event->data[seatIndex].seatLock);
    }
  }

  writeToFile(fd,buffer);

  return 0;
}

int ems_list_events(int fd) {
  
  pthread_rwlock_wrlock(&createEventLock);
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    pthread_rwlock_unlock(&createEventLock);
    return 1;
  }

  if (event_list->head == NULL) {
    writeToFile(fd, "No events\n");
    pthread_rwlock_unlock(&createEventLock);
    return 0;
  }
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
  pthread_rwlock_unlock(&createEventLock);
  
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

  pthread_mutex_init(&parseMutex, NULL); 
  pthread_rwlock_init(&createEventLock, NULL);
  pthread_rwlock_init(&waitCommandLock,NULL);

  long unsigned int max = (long unsigned int) maxThreads;
  long unsigned int size = max * sizeof(int);
  threadWait = malloc(size);
  memset(threadWait, 0, size);

  pthread_t tid[maxThreads];

  int keepReading = 1;
  
  while(keepReading){
    keepReading = 1;
    barrierFound = 0;
    for(int i = 0; i < maxThreads; i++){
      Arguments * arguments = malloc(sizeof(struct arguments));
      arguments->fdin = fdin;
      arguments->fdout = fdout;
      arguments->id = i;
      if(pthread_create(&tid[i], 0, threadFunc, arguments) != 0){
        fprintf(stderr, "Error creating thread\n");
      }
    }
    for(int i = 0; i < maxThreads; i++){
      int *result = NULL;
      if(pthread_join(tid[i], (void **)&result)){
        fprintf(stderr, "Error joining thread\n");
      }
      if(keepReading != 0)
        keepReading = *result;
      
      free(result);
    }
  }

  free(threadWait);
  close(fdin);
  close(fdout);
  return 0;
}

void * threadFunc(void* arguments){
  Arguments * parsedArguments = (Arguments*) arguments;
  int fdIn = parsedArguments->fdin;
  int fdOut = parsedArguments->fdout;
  int threadID = parsedArguments->id;
  free(parsedArguments);
  int * res = malloc(sizeof(int));

  while(1){
    *res = switchCase(fdIn, fdOut, threadID);
    if(*res == 0 || *res == 1)
      break;
  }

  pthread_exit(res);
}

int switchCase(int fdIn, int fdOut, int threadID){
  unsigned int event_id, delay, thread_id;
  size_t num_rows, num_columns, num_coords;
  size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

  pthread_rwlock_wrlock(&waitCommandLock);
  if(threadWait[threadID]!=0){
    ems_wait(threadWait[threadID]);
    threadWait[threadID]=0;
  }
  pthread_rwlock_unlock(&waitCommandLock);

  pthread_mutex_lock(&parseMutex);

  if(barrierFound){
    pthread_mutex_unlock(&parseMutex);
    return 1;
  }

  switch (get_next(fdIn)) {
    case CMD_CREATE:
      if (parse_create(fdIn, &event_id, &num_rows, &num_columns) != 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        return -1;
      }

      pthread_mutex_unlock(&parseMutex);

      if (ems_create(event_id, num_rows, num_columns)) {
        fprintf(stderr, "Failed to create event\n");
      }

      break;

    case CMD_RESERVE:
      num_coords = parse_reserve(fdIn, MAX_RESERVATION_SIZE, &event_id, xs, ys);

      if (num_coords == 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        return -1;
      }

      pthread_mutex_unlock(&parseMutex);

      if (ems_reserve(event_id, num_coords, xs, ys)) {
        fprintf(stderr, "Failed to reserve seats\n");
      }

      break;

    case CMD_SHOW:
      if (parse_show(fdIn, &event_id) != 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        return -1;
      }
      pthread_mutex_unlock(&parseMutex);

      if (ems_show(event_id, fdOut)) {
        fprintf(stderr, "Failed to show event\n");
      }

      break;

    case CMD_LIST_EVENTS:
      pthread_mutex_unlock(&parseMutex);

      if (ems_list_events(fdOut)) {
        fprintf(stderr, "Failed to list events\n");
      }

      break;

    case CMD_WAIT:
      if (parse_wait(fdIn, &delay, &thread_id) == -1) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        return -1;
      }

      pthread_mutex_unlock(&parseMutex);

      if (delay > 0) {
        printf("Waiting...\n");
        
        if(thread_id==0)
          ems_wait(delay);
        else{
          pthread_rwlock_wrlock(&waitCommandLock);
          threadWait[--thread_id] = delay;
          pthread_rwlock_unlock(&waitCommandLock);
        }
      }
      break;

    case CMD_INVALID:
      fprintf(stderr, "Invalid command. See HELP for usage\n");
      break;

    case CMD_HELP:
      pthread_mutex_unlock(&parseMutex);
      printf(
          "Available commands:\n"
          "  CREATE <event_id> <num_rows> <num_columns>\n"
          "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
          "  SHOW <event_id>\n"
          "  LIST\n"
          "  WAIT <delay_ms> [thread_id]\n"  // thread_id is not implemented
          "  BARRIER\n"                      // Not implemented
          "  HELP\n");

      break;

    case CMD_BARRIER:
      barrierFound = 1;
      pthread_mutex_unlock(&parseMutex);
      return 1;
    case CMD_EMPTY:
      pthread_mutex_unlock(&parseMutex);
      break;

    case EOC:
      pthread_mutex_unlock(&parseMutex);
      return 0;
  }

  return 2;
}

