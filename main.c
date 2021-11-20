#include "main.h"

struct Self self;
Message msg;

static int init_process() {
  for (size_t i = 0; i < self.n_processes; ++i) {
    for (size_t j = 0; j < self.n_processes; ++j) {
      if (i == j)
        continue;
      int *pair = &self.pipes[2 * (i * self.n_processes + j)];
      if (j != self.id) {
        fprintf(self.pipes_log, "Process %zu closing its pipe end %d\n",
                self.id, pair[0]);
        CHK_ERRNO(close(pair[0]));
      }
      if (i != self.id) {
        fprintf(self.pipes_log, "Process %zu closing its pipe end %d\n",
                self.id, pair[1]);
        CHK_ERRNO(close(pair[1]));
      }
    }
  }
  return 0;
}

static int deinit_process() {
  for (size_t i = 0; i < self.n_processes; ++i) {
    if (i != self.id) {
      int read_end = self.pipes[2 * (i * self.n_processes + self.id) + 0];
      int write_end = self.pipes[2 * (self.id * self.n_processes + i) + 1];
      fprintf(self.pipes_log, "Process %zu closing its pipe end %d\n", self.id,
              read_end);
      CHK_ERRNO(close(read_end));
      fprintf(self.pipes_log, "Process %zu closing its pipe end %d\n", self.id,
              write_end);
      CHK_ERRNO(close(write_end));
    }
  }
  free(self.pipes);
  fclose(self.pipes_log);
  fclose(self.events_log);
  return 0;
}

/* static int send_empty(size_t dst, MessageType type) {
  msg.s_header.s_magic = MESSAGE_MAGIC;
  msg.s_header.s_local_time = ++self.local_time;
  msg.s_header.s_type = type;
  msg.s_header.s_payload_len = 0;
  return send(&self, (local_id)dst, &msg);
} */

static int send_empty_multicast(MessageType type) {
  msg.s_header.s_magic = MESSAGE_MAGIC;
  msg.s_header.s_local_time = ++self.local_time;
  msg.s_header.s_type = type;
  msg.s_header.s_payload_len = 0;
  return send_multicast(&self, &msg);
}

static int wait_for_message(size_t from, MessageType type) {
  int retcode = 0;
  while (!retcode) {
    retcode = receive(&self, (local_id)from, &msg);
    CHK_RETCODE(retcode);
  }
  return 0;
}

static int sync_started_done(MessageType type) {
  msg.s_header.s_magic = MESSAGE_MAGIC;
  msg.s_header.s_local_time = ++self.local_time;
  msg.s_header.s_type = type;
  msg.s_header.s_payload_len = snprintf(
      msg.s_payload, MAX_PAYLOAD_LEN, log_started_fmt, (int)self.local_time,
      (int)self.id, (int)getpid(), (int)getppid(), 0);
  fputs(msg.s_payload, self.events_log);
  CHK_RETCODE(send_multicast(&self, &msg));

  for (size_t i = 1; i < self.n_processes; ++i) {
    if (i != self.id) {
      CHK_RETCODE(wait_for_message(i, type));
    }
  }
  return 0;
}

int request_cs(const void *self_) {
  (void)self_;
  CHK_RETCODE(send_empty_multicast(CS_REQUEST));
  return 0;
}

int release_cs(const void *self_) {
  (void)self_;
  CHK_RETCODE(send_empty_multicast(CS_RELEASE));
  return 0;
}

static int run_child() {
  CHK_RETCODE(init_process());
  CHK_RETCODE(sync_started_done(STARTED));
  fprintf(self.events_log, log_received_all_started_fmt, (int)self.local_time,
          (int)self.id);

  if (self.use_mutex) {
    request_cs(&self);
  }
  for (size_t i = 0; i < 5 * self.id; ++i) {
    char buf[256];
    snprintf(buf, sizeof(buf), log_loop_operation_fmt, (int)self.id,
             (int)(i + 1), (int)(5 * self.id));
    print(buf);
  }
  if (self.use_mutex) {
    request_cs(&self);
  }

  CHK_RETCODE(sync_started_done(DONE));
  fprintf(self.events_log, log_received_all_done_fmt, (int)self.local_time,
          (int)self.id);

  CHK_RETCODE(deinit_process());
  return 0;
}

static int run_parent() {
  CHK_RETCODE(init_process());

  for (size_t i = 1; i < self.n_processes; ++i)
    CHK_RETCODE(wait_for_message(i, STARTED));
  fprintf(self.events_log, log_received_all_started_fmt, (int)self.local_time,
          (int)self.id);

  for (size_t i = 1; i < self.n_processes; ++i)
    CHK_RETCODE(wait_for_message(i, DONE));
  fprintf(self.events_log, log_received_all_done_fmt, (int)self.local_time,
          (int)self.id);

  for (size_t i = 1; i < self.n_processes; ++i)
    wait(NULL);

  CHK_RETCODE(deinit_process());
  return 0;
}

timestamp_t get_lamport_time() { return self.local_time; }

int main(int argc, char *argv[]) {
  if (argc < 3) {
    printf("Usage: %s -p <number of processes> [--mutexl]\n", argv[0]);
    return 0;
  }

  int found_n_children = 0;
  self.use_mutex = 0;

  for (size_t i = 0; i < argc; ++i) {
    if (strcmp("-p", argv[i]) == 0) {
      found_n_children = 1;
      size_t n_children;
      if (i + 1 >= argc || sscanf(argv[i + 1], "%zu", &n_children) != 1) {
        printf("Usage: %s -p <number of processes> [--mutexl]\n", argv[0]);
        return 0;
      }
      self.n_processes = n_children + 1;
    } else if (strcmp("--mutexl", argv[i]) == 0) {
      self.use_mutex = 1;
    }
  }

  if (!found_n_children) {
    printf("Usage: %s -p <number of processes> [--mutexl]\n", argv[0]);
    return 0;
  }

  self.pipes_log = fopen(pipes_log, "w");
  self.events_log = fopen(events_log, "a");
  self.local_time = 0;

  self.cs_queue = malloc(sizeof(struct QueueEntry) * self.n_processes);
  self.cs_queue_len = 0;

  self.pipes = malloc(2 * sizeof(int) * self.n_processes * self.n_processes);
  for (size_t i = 0; i < self.n_processes; ++i) {
    for (size_t j = 0; j < self.n_processes; ++j) {
      int *to_create = &self.pipes[2 * (i * self.n_processes + j)];
      if (i == j) {
        to_create[0] = -1;
        to_create[1] = -1;
      } else {
        CHK_ERRNO(pipe(to_create));
        CHK_ERRNO(fcntl(to_create[0], F_SETFL, O_NONBLOCK));
        CHK_ERRNO(fcntl(to_create[1], F_SETFL, O_NONBLOCK));
        fprintf(self.pipes_log, "Pipe %zu ==> %zu: rfd=%d, wfd=%d\n", i, j,
                to_create[0], to_create[1]);
      }
    }
  }
  fflush(self.pipes_log);

  for (size_t id = 1; id < self.n_processes; ++id) {
    pid_t pid = fork();
    CHK_ERRNO(pid);
    if (!pid) {
      self.id = id;
      return run_child();
    }
  }

  self.id = 0;
  return run_parent();
}
