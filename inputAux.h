#ifndef INPUT_MAIN_CASE
#define INPUT_MAIN_CASE

/// its the main switch case.
/// @param fd is the fileDescriptor 
/// @return  0 if we want to end the program , 1 otherwise.
int switchCase(int fdIn, int fdOut, pthread_t *tid, int threadID, int *threadState);

#endif  // INPUT_MAIN_CASE