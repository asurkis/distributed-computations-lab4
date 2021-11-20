#include "pa23.h"

static int init_process(struct Self *self) {
  for (size_t i = 0; i < self->n_processes; ++i) {
    for (size_t j = 0; j < self->n_processes; ++j) {
      if (i == j)
        continue;
      int *pair = &self->pipes[2 * (i * self->n_processes + j)];
      if (j != self->id) {
        fprintf(self->pipes_log, "Process %zu closing its pipe end %d\n",
                self->id, pair[0]);
        CHK_ERRNO(close(pair[0]));
      }
      if (i != self->id) {
        fprintf(self->pipes_log, "Process %zu closing its pipe end %d\n",
                self->id, pair[1]);
        CHK_ERRNO(close(pair[1]));
      }
    }
  }
  return 0;
}

static int deinit_process(struct Self *self) {
  for (size_t i = 0; i < self->n_processes; ++i) {
    if (i != self->id) {
      int read_end = self->pipes[2 * (i * self->n_processes + self->id) + 0];
      int write_end = self->pipes[2 * (self->id * self->n_processes + i) + 1];
      fprintf(self->pipes_log, "Process %zu closing its pipe end %d\n",
              self->id, read_end);
      CHK_ERRNO(close(read_end));
      fprintf(self->pipes_log, "Process %zu closing its pipe end %d\n",
              self->id, write_end);
      CHK_ERRNO(close(write_end));
    }
  }
  free(self->pipes);
  fclose(self->pipes_log);
  fclose(self->events_log);
  return 0;
}

static int wait_for_message(struct Self *self, size_t from, Message *msg,
                            MessageType type) {
  int retcode = 0;
  while (!retcode) {
    retcode = receive(self, (local_id)from, msg);
    CHK_RETCODE(retcode);
  }
  return 0;
}

void transfer(void *parent_data, local_id src, local_id dst, balance_t amount) {
  struct Self *self = parent_data;
  Message msg;
  msg.s_header.s_magic = MESSAGE_MAGIC;
  msg.s_header.s_local_time = ++self->local_time;
  msg.s_header.s_type = TRANSFER;
  msg.s_header.s_payload_len = sizeof(TransferOrder);
  TransferOrder *order = (TransferOrder *)msg.s_payload;
  order->s_amount = amount;
  order->s_src = src;
  order->s_dst = dst;
  send(self, src, &msg);
  wait_for_message(self, dst, &msg, ACK);
}

static int run_child(struct Self *self) {
  Message msg;
  BalanceHistory history;
  CHK_RETCODE(init_process(self));

  history.s_id = self->id;
  history.s_history[0].s_balance = self->my_balance;
  history.s_history[0].s_time = 0;
  history.s_history[0].s_balance_pending_in = 0;
  history.s_history_len = 1;

  msg.s_header.s_magic = MESSAGE_MAGIC;
  msg.s_header.s_local_time = ++self->local_time;
  msg.s_header.s_type = STARTED;
  msg.s_header.s_payload_len = snprintf(
      msg.s_payload, MAX_PAYLOAD_LEN, log_started_fmt, (int)self->local_time,
      (int)self->id, (int)getpid(), (int)getppid(), self->my_balance);
  fputs(msg.s_payload, self->events_log);
  CHK_RETCODE(send_multicast(self, &msg));

  for (size_t i = 1; i < self->n_processes; ++i) {
    if (i != self->id) {
      CHK_RETCODE(wait_for_message(self, i, &msg, STARTED));
    }
  }

  fprintf(self->events_log, log_received_all_started_fmt, (int)self->local_time,
          (int)self->id);

  for (;;) {
    int retcode = receive_any(self, &msg);
    CHK_RETCODE(retcode);
    if (!retcode)
      continue;
    for (timestamp_t t =
             history.s_history[history.s_history_len - 1].s_time + 1;
         t < self->local_time; ++t) {
      history.s_history[history.s_history_len].s_balance = self->my_balance;
      history.s_history[history.s_history_len].s_time = t;
      history.s_history[history.s_history_len].s_balance_pending_in = 0;
      history.s_history_len++;
    }
    if (msg.s_header.s_type == STOP)
      break;
    if (msg.s_header.s_type == TRANSFER) {
      TransferOrder *order = (TransferOrder *)msg.s_payload;
      if (order->s_src == self->id) {
        msg.s_header.s_local_time = ++self->local_time;
        send(self, order->s_dst, &msg);
        self->my_balance -= order->s_amount;
      }

      if (order->s_dst == self->id) {
        for (size_t i = 0; i < history.s_history_len; ++i) {
          BalanceState *state = &history.s_history[i];
          timestamp_t ts = state->s_time;
          if (msg.s_header.s_local_time - 2 < ts && ts < self->local_time) {
            state->s_balance_pending_in += order->s_amount;
          }
        }
        msg.s_header.s_magic = MESSAGE_MAGIC;
        msg.s_header.s_payload_len = 0;
        msg.s_header.s_type = ACK;
        msg.s_header.s_local_time = ++self->local_time;
        send(self, 0, &msg);
        self->my_balance += order->s_amount;
      }
    }
  }

  for (timestamp_t t = history.s_history[history.s_history_len - 1].s_time + 1;
       t <= self->local_time; ++t) {
    history.s_history[history.s_history_len].s_balance = self->my_balance;
    history.s_history[history.s_history_len].s_time = t;
    history.s_history[history.s_history_len].s_balance_pending_in = 0;
    history.s_history_len++;
  }

  msg.s_header.s_magic = MESSAGE_MAGIC;
  msg.s_header.s_local_time = ++self->local_time;
  msg.s_header.s_type = DONE;
  msg.s_header.s_payload_len =
      snprintf(msg.s_payload, MAX_PAYLOAD_LEN, log_done_fmt,
               (int)self->local_time, (int)self->id, self->my_balance);
  fputs(msg.s_payload, self->events_log);
  CHK_RETCODE(send_multicast(self, &msg));

  for (size_t i = 1; i < self->n_processes; ++i) {
    if (i != self->id) {
      CHK_RETCODE(wait_for_message(self, i, &msg, DONE));
    }
  }

  fprintf(self->events_log, log_received_all_done_fmt, (int)self->local_time,
          (int)self->id);

  msg.s_header.s_magic = MESSAGE_MAGIC;
  msg.s_header.s_local_time = ++self->local_time;
  msg.s_header.s_type = BALANCE_HISTORY;
  msg.s_header.s_payload_len =
      (char *)&history.s_history[history.s_history_len] - (char *)&history;
  memcpy(msg.s_payload, &history, msg.s_header.s_payload_len);
  CHK_RETCODE(send(self, 0, &msg));

  CHK_RETCODE(deinit_process(self));
  return 0;
}

static int run_parent(struct Self *self) {
  Message msg;
  AllHistory history;
  CHK_RETCODE(init_process(self));

  for (size_t i = 1; i < self->n_processes; ++i)
    CHK_RETCODE(wait_for_message(self, i, &msg, STARTED));
  fprintf(self->events_log, log_received_all_started_fmt, (int)self->local_time,
          (int)self->id);

  bank_robbery(self, self->n_processes - 1);

  msg.s_header.s_magic = MESSAGE_MAGIC;
  msg.s_header.s_local_time = ++self->local_time;
  msg.s_header.s_type = STOP;
  msg.s_header.s_payload_len = 0;
  CHK_RETCODE(send_multicast(self, &msg));

  for (size_t i = 1; i < self->n_processes; ++i)
    CHK_RETCODE(wait_for_message(self, i, &msg, DONE));
  fprintf(self->events_log, log_received_all_done_fmt, (int)self->local_time,
          (int)self->id);

  history.s_history_len = self->n_processes - 1;
  for (size_t i = 1; i < self->n_processes; ++i) {
    CHK_RETCODE(wait_for_message(self, i, &msg, BALANCE_HISTORY));
    memcpy(&history.s_history[i - 1], msg.s_payload,
           msg.s_header.s_payload_len);
  }
  print_history(&history);

  for (size_t i = 1; i < self->n_processes; ++i)
    wait(NULL);

  CHK_RETCODE(deinit_process(self));
  return 0;
}

struct Self self;

timestamp_t get_lamport_time() { return self.local_time; }

int main(int argc, char *argv[]) {
  size_t n_children;
  if (argc < 3 || sscanf(argv[2], "%zu", &n_children) != 1) {
    printf("Usage: %s -p <number of processes>"
           "<amount of money for each process>\n",
           argv[0]);
    return 0;
  }
  if (argc < 3 + n_children) {
    printf("Usage: %s -p <number of processes>"
           "<amount of money for each process>\n",
           argv[0]);
    return 0;
  }

  self.pipes_log = fopen(pipes_log, "w");
  self.events_log = fopen(events_log, "a");
  self.local_time = 0;

  self.n_processes = n_children + 1;
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
    sscanf(argv[2 + id], "%" SCNd16, &self.my_balance);
    pid_t pid = fork();
    CHK_ERRNO(pid);
    if (!pid) {
      self.id = id;
      return run_child(&self);
    }
  }

  self.id = 0;
  return run_parent(&self);
}
