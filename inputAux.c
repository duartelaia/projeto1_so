#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
// novos
// imports novos a partir daqui 
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>

#include "constants.h"
#include "operations.h"
#include "parser.h"
#include "inputAux.h"

void* switchCase(void *arguments){
  unsigned int event_id, delay;
  size_t num_rows, num_columns, num_coords;
  size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

  Arguments * parsedArguments = (Arguments*) arguments;
  int fdIn = parsedArguments->fdin;
  int fdOut = parsedArguments->fdout;
  int threadID = parsedArguments->id;
  int * threadState = parsedArguments->threadState;
  pthread_mutex_t * switchCaseMutex = parsedArguments->mutex;
  free(arguments);
  threadState[threadID] = 2;

  int * result = malloc(sizeof(int));
  if(result == NULL) 
    pthread_exit(NULL);

  *result = -1;

  pthread_mutex_lock(switchCaseMutex);

  switch (get_next(fdIn)) {
    case CMD_CREATE:
      if (parse_create(fdIn, &event_id, &num_rows, &num_columns) != 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        threadState[threadID] = 1;
        pthread_exit(result);
      }

      pthread_mutex_unlock(switchCaseMutex);

      if (ems_create(event_id, num_rows, num_columns)) {
        fprintf(stderr, "Failed to create event\n");
      }

      break;

    case CMD_RESERVE:
      num_coords = parse_reserve(fdIn, MAX_RESERVATION_SIZE, &event_id, xs, ys);

      if (num_coords == 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        threadState[threadID] = 1;
        pthread_exit(result);
      }

      pthread_mutex_unlock(switchCaseMutex);

      if (ems_reserve(event_id, num_coords, xs, ys)) {
        fprintf(stderr, "Failed to reserve seats\n");
      }

      break;

    case CMD_SHOW:
      if (parse_show(fdIn, &event_id) != 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        threadState[threadID] = 1;
        pthread_exit(result);
      }
      pthread_mutex_unlock(switchCaseMutex);

      if (ems_show(event_id, fdOut)) {
        fprintf(stderr, "Failed to show event\n");
      }

      break;

    case CMD_LIST_EVENTS:
      pthread_mutex_unlock(switchCaseMutex);

      if (ems_list_events(fdOut)) {
        fprintf(stderr, "Failed to list events\n");
      }

      break;

    case CMD_WAIT:
      if (parse_wait(fdIn, &delay, NULL) == -1) {  // thread_id is not implemented
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        threadState[threadID] = 1;
        pthread_exit(result);
      }

      pthread_mutex_unlock(switchCaseMutex);

      if (delay > 0) {
        printf("Waiting...\n");
        ems_wait(delay);
      }

      break;

    case CMD_INVALID:
      fprintf(stderr, "Invalid command. See HELP for usage\n");
      break;

    case CMD_HELP:
      pthread_mutex_unlock(switchCaseMutex);
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
      pthread_mutex_unlock(switchCaseMutex);
      threadState[threadID] = 1;
      *result = 1;
      pthread_exit(result);
    case CMD_EMPTY:
      pthread_mutex_unlock(switchCaseMutex);
      break;

    case EOC:
      pthread_mutex_unlock(switchCaseMutex);
      threadState[threadID] = 1;
      *result = 0;
      pthread_exit(result);
  }
  pthread_mutex_unlock(switchCaseMutex);
  threadState[threadID] = 1;
  *result = 2;
  pthread_exit(result);
}