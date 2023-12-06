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

#include "eventlist.h"
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

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols, int fd) {
  if (event_list == NULL) {
    writeToFile(fd, "EMS state must be initialized\n");
    return 1;
  }

  if (get_event_with_delay(event_id) != NULL) {
    writeToFile(fd, "Event already exists\n");
    return 1;
  }

  struct Event* event = malloc(sizeof(struct Event));

  if (event == NULL) {
    writeToFile(fd, "Error allocating memory for event\n");
    return 1;
  }

  event->id = event_id;
  event->rows = num_rows;
  event->cols = num_cols;
  event->reservations = 0;
  event->data = malloc(num_rows * num_cols * sizeof(unsigned int));

  if (event->data == NULL) {
    writeToFile(fd, "Error allocating memory for event data\n");
    free(event);
    return 1;
  }

  for (size_t i = 0; i < num_rows * num_cols; i++) {
    event->data[i] = 0;
  }

  if (append_to_list(event_list, event) != 0) {
    writeToFile(fd, "Error appending event to list\n");
    free(event->data);
    free(event);
    return 1;
  }

  return 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys, int fd) {
  if (event_list == NULL) {
    writeToFile(fd,  "EMS state must be initialized\n");
    return 1;
  }

  struct Event* event = get_event_with_delay(event_id);

  if (event == NULL) {
    writeToFile(fd, "Event not found\n");
    return 1;
  }

  unsigned int reservation_id = ++event->reservations;

  size_t i = 0;
  for (; i < num_seats; i++) {
    size_t row = xs[i];
    size_t col = ys[i];

    if (row <= 0 || row > event->rows || col <= 0 || col > event->cols) {
      writeToFile(fd, "Invalid seat\n");
      break;
    }

    if (*get_seat_with_delay(event, seat_index(event, row, col)) != 0) {
      writeToFile(fd, "Seat already reserved\n");
      break;
    }

    *get_seat_with_delay(event, seat_index(event, row, col)) = reservation_id;
  }

  // If the reservation was not successful, free the seats that were reserved.
  if (i < num_seats) {
    event->reservations--;
    for (size_t j = 0; j < i; j++) {
      *get_seat_with_delay(event, seat_index(event, xs[j], ys[j])) = 0;
    }
    return 1;
  }

  return 0;
}

int ems_show(unsigned int event_id, int fd) {
  if (event_list == NULL) {
    writeToFile(fd, "EMS state must be initialized\n");
    return 1;
  }

  struct Event* event = get_event_with_delay(event_id);

  if (event == NULL) {
    writeToFile(fd, "Event not found\n");
    return 1;
  }

  char buffer[1024];
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

  return 0;
}

int ems_list_events(int fd) {
  if (event_list == NULL) {
    writeToFile(fd, "EMS state must be initialized\n");
    return 1;
  }

  if (event_list->head == NULL) {
    writeToFile(fd,"No events\n");
    return 0;
  }

  struct ListNode* current = event_list->head;
  char buffer[1024];
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

  return 0;
}

int ems_file(char * dirPath,char * filename){
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
  
  while(switchCase(fdin, fdout));
  
  close(fdin);
  close(fdout);
  return 0;
}

void ems_wait(unsigned int delay_ms) {
  struct timespec delay = delay_to_timespec(delay_ms);
  nanosleep(&delay, NULL);
}

