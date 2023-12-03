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

#include "constants.h"
#include "operations.h"
#include "parser.h"
#include "inputAux.h"

int main(int argc, char *argv[]) {
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;

  if (argc > 1) {
    char *endptr;
    unsigned long int delay = strtoul(argv[1], &endptr, 10);

    // verificação do delay
    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }
    state_access_delay_ms = (unsigned int)delay;
  }

  if (ems_init(state_access_delay_ms)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }
  // processo de leitura dos ficheiros de um diretorio
  if (argc > 2){
    // verificação da diretoria
    DIR *Dir;
    // se for preciso concatenar meter aqui!!!!!!
    Dir = opendir(argv[2]);
    if ( Dir == NULL){
      fprintf(stderr, "openDir error: %s\n", strerror(errno));
      return 1;
    }
    /* leitura do diretorio */
    struct dirent *file;
    while ((file = readdir(Dir)) != NULL) {
        // Ignora entradas especiais "." e ".."
        size_t size = strlen(file->d_name);
        if (strcmp(file->d_name, ".") != 0 && strcmp(file->d_name, "..") != 0 &&
            !strcmp(file->d_name + (size > 5 ?  size - 5 : 0), ".jobs")) {
            if(ems_file(argv[2], file->d_name) == -1){
              fprintf(stderr, "failed!");
              return 1;
            }
        }
    }
    closedir(Dir);
    return 0;
  }

  while (1) {

    printf("> ");
    fflush(stdout);
    
    if (!switchCase(STDIN_FILENO)){
      return 0;
    }
  }
}
