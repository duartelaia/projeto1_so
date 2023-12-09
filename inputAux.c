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

int switchCase(int fdIn, int fdOut,pthread_t *tid, int threadID, int *threadState){
  unsigned int event_id, delay;
  size_t num_rows, num_columns, num_coords;
  size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];
  ThreadArguments arguments;
  arguments.threadID = threadID;
  arguments.threadState = threadState;
  arguments.fdOut = fdOut;

  switch (get_next(fdIn)) {
      case CMD_CREATE:
        if (parse_create(fdIn, &event_id, &num_rows, &num_columns) != 0) {
          writeToFile(fdOut, "Invalid command. See HELP for usage\n");
          break;
        }

        arguments.event_id = event_id;
        arguments.num_columns = num_columns;
        arguments.num_rows = num_rows;
  
        if(pthread_create(tid, NULL, ems_create, &arguments) != 0){
          fprintf(stderr, "Error creating thread\n");
        }

        break;

      case CMD_RESERVE:
        num_coords = parse_reserve(fdIn, MAX_RESERVATION_SIZE, &event_id, xs, ys);

        if (num_coords == 0) {
          writeToFile(fdOut, "Invalid command. See HELP for usage\n");
          break;
        }

        arguments.event_id = event_id;
        arguments.num_coords = num_coords;
        arguments.xs = xs;
        arguments.ys = ys;

        if(pthread_create(tid, NULL, ems_reserve, &arguments) != 0){
          fprintf(stderr, "Error creating thread\n");
        }

        break;

      case CMD_SHOW:
        if (parse_show(fdIn, &event_id) != 0) {
          writeToFile(fdOut, "Invalid command. See HELP for usage\n");
          break;
        }

        arguments.event_id = event_id;

        if(pthread_create(tid, NULL, ems_show, &arguments) != 0){
          fprintf(stderr, "Error creating thread\n");
        }

        break;

      case CMD_LIST_EVENTS:
        if(pthread_create(tid, NULL, ems_list_events, &arguments) != 0){
          fprintf(stderr, "Error creating thread\n");
        }

        break;

      case CMD_WAIT:
        if (parse_wait(fdIn, &delay, NULL) == -1) {  // thread_id is not implemented
          writeToFile(fdOut, "Invalid command. See HELP for usage\n");
          break;
        }

        arguments.delay = delay;

        if (delay > 0) {
          writeToFile(fdOut,"Waiting...\n");
          if(pthread_create(tid, NULL, ems_wait, &arguments) != 0){
            fprintf(stderr, "Error creating thread\n");
          }

        }

        break;

      case CMD_INVALID:
        writeToFile(fdOut, "Invalid command. See HELP for usage\n");
        break;

      case CMD_HELP:
        writeToFile(fdOut,
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
        return 1;
      case CMD_EMPTY:
        break;
      case EOC:
        return 2;
        
    }
  return 0;
}