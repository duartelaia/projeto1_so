pthread_t tid[maxThreads];
  int continueReading = 1, threadsFinished=0, barrierFound=0, activeThreads=0;

  int threadState[maxThreads];
  memset(threadState, 0, sizeof(threadState));

  int threadResult[maxThreads];
  memset(threadResult, 0, sizeof(threadResult));

  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

  while(continueReading){
    for(int i = 0; i < maxThreads; i++){
      if (threadState[i] == 0){
        Arguments * arguments = malloc(sizeof(struct arguments));
        arguments->fdin = fdin;
        arguments->fdout = fdout;
        arguments->threadState = threadState;
        arguments->id = i;
        arguments->mutex = &mutex;
        arguments->threadResult = threadResult;
        if(pthread_create(&tid[i], 0, switchCase, arguments) != 0){
          fprintf(stderr, "Error creating thread\n");
        }
        activeThreads++;
        if(threadResult[i]==1)
          break;
        printf("tarefa %d - criou a thread\n",i);
      }
    }

    threadsFinished = 0;
    while (threadsFinished != 0 || barrierFound){
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
      if(barrierFound && threadsFinished==activeThreads)
        barrierFound = 0;
    }
  }