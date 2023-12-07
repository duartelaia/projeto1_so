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
#include <wait.h>

#include "constants.h"
#include "operations.h"
#include "parser.h"
#include "inputAux.h"

int main(int argc, char *argv[]) {
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;

  if (argc > 5){
    return 1;
  }

  // MaxProcesses
  int maxProcesses = atoi(argv[3]);

  // MaxThreads
  int maxThreads = atoi(argv[4]);

  // Delay
  char *endptr;
  unsigned long int delay = strtoul(argv[1], &endptr, 10);

  if (*endptr != '\0' || delay > UINT_MAX) {
    fprintf(stderr, "Invalid delay value or value too large\n");
    return 1;
  }
  state_access_delay_ms = (unsigned int)delay;
  

  if (ems_init(state_access_delay_ms)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  // File system
  DIR *dir;
  dir = opendir(argv[2]);
  if ( dir == NULL){
    fprintf(stderr, "opendir error: %s\n", strerror(errno));
    return 1;
  }

  struct dirent *file;

  int process_counter = 0;
  while (1) {
    if (process_counter >= maxProcesses){
      // Wait for any process to end
      int state;
      wait(&state);
      printf("%d\n", state);
      process_counter--;
    }
      
    file = readdir(dir);
    if (file == NULL)
      break;

    // If the file is not the right type, dont create the process
    size_t size = strlen(file->d_name);
    if (!(strcmp(file->d_name, ".") != 0 && strcmp(file->d_name, "..") != 0 &&
        !strcmp(file->d_name + (size > 5 ?  size - 5 : 0), ".jobs"))) {
          continue;
    }

    pid_t pid = fork();

    if (pid < 0){
      fprintf(stderr, "Fork error\n");
      return 1;
    }

    if (pid == 0){
      // Child  
      if(ems_file(argv[2], file->d_name) == -1){
        fprintf(stderr, "failed!\n");
        exit(1);
      }
      exit(0);
    }
    else{
      // Parent
      process_counter++;
    }
  }

  while (process_counter > 0){
    wait(NULL);
    process_counter--;
  }
  
  closedir(dir);

  ems_terminate();
  return 0;
  
}
