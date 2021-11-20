#ifndef _PA23_H_
#define _PA23_H_

#include "common.h"
#include "ipc.h"
#include "pa2345.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <memory.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEBUG                                                                  \
  do {                                                                         \
    fprintf(stderr, "Process %zu [%s:%d]\n", self->id, __FILE__, __LINE__);    \
    fflush(stderr);                                                            \
  } while (0)

#define CHK_RETCODE(code)                                                      \
  do {                                                                         \
    intmax_t result__ = code;                                                  \
    if (result__ < 0) {                                                        \
      fprintf(stderr, "[%s:%d] %s\n", __FILE__, __LINE__, #code);              \
      return result__;                                                         \
    }                                                                          \
  } while (0)

#define CHK_ERRNO(code)                                                        \
  do {                                                                         \
    intmax_t result__ = code;                                                  \
    if (result__ < 0) {                                                        \
      perror("[" __FILE__ "] " #code);                                         \
      fprintf(stderr, "[%s:%d] errno = %d\n", __FILE__, __LINE__, errno);      \
      return result__;                                                         \
    }                                                                          \
  } while (0)

#define CHK_RETCODE_ZERO(code)                                                 \
  do {                                                                         \
    intmax_t result__ = code;                                                  \
    if (!result__)                                                             \
      return 0;                                                                \
    if (result__ < 0) {                                                        \
      fprintf(stderr, "[%s:%d] %s\n", __FILE__, __LINE__, #code);              \
      return result__;                                                         \
    }                                                                          \
  } while (0)

struct Self {
  int *pipes;
  FILE *events_log;
  FILE *pipes_log;
  size_t *cs_queue;
  size_t cs_queue_len;

  size_t id;
  size_t n_processes;

  timestamp_t local_time;
};

#endif
