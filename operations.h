#ifndef EMS_OPERATIONS_H
#define EMS_OPERATIONS_H

#include <stddef.h>
#include "constants.h"

typedef struct threadArguments{
    unsigned int event_id, delay;
    int fdOut, threadID;
    int *threadState;
    size_t num_rows, num_columns, num_coords;
    size_t *xs, *ys;
} ThreadArguments;

int writeToFile(int fd, char * buffer);

/// Initializes the EMS state.
/// @param delay_ms State access delay in milliseconds.
/// @param fd File descriptor to write
/// @return 0 if the EMS state was initialized successfully, 1 otherwise.
int ems_init(unsigned int delay_ms);

/// Destroys the EMS state.
/// @param fd File descriptor to write
int ems_terminate();

/// Creates a new event with the given id and dimensions.
/// @param event_id Id of the event to be created.
/// @param num_rows Number of rows of the event to be created.
/// @param num_cols Number of columns of the event to be created.
/// @param fd File descriptor to write
/// @return 0 if the event was created successfully, 1 otherwise.
void* ems_create(void* arguments);

/// Creates a new reservation for the given event.
/// @param event_id Id of the event to create a reservation for.
/// @param num_seats Number of seats to reserve.
/// @param xs Array of rows of the seats to reserve.
/// @param ys Array of columns of the seats to reserve.
/// @param fd File descriptor to write
/// @return 0 if the reservation was created successfully, 1 otherwise.
void* ems_reserve(void* arguments);

/// Prints the given event.
/// @param event_id Id of the event to print.
/// @param fd File descriptor to write
/// @return 0 if the event was printed successfully, 1 otherwise.
void* ems_show(void* arguments);

/// Prints all the events.
/// @param fd File descriptor to write
/// @return 0 if the events were printed successfully, 1 otherwise.
void* ems_list_events(void* arguments);

/// Waits for a given amount of time.
/// @param delay_us Delay in milliseconds.
/// @param fd File descriptor to write
void* ems_wait(void* arguments);

/// read all the .job files.
/// @param dirpath the path to the dir.
/// @return 0 if all went sucessfully, 1 otherwise.
int ems_file(char * dirpath,char *filename, int maxThreads);

#endif  // EMS_OPERATIONS_H
