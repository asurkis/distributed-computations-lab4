#include "pa23.h"

static int write_repeat(int fd, char const *buf, size_t size) {
  while (size) {
    ssize_t shift = write(fd, buf, size);
    if (shift < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      continue;
    CHK_ERRNO(shift);
    if (!shift)
      return -1;
    buf += shift;
    size -= shift;
  }
  return 1;
}

static int read_repeat(int fd, char *buf, size_t size) {
  int started_reading = 0;
  while (size) {
    ssize_t shift = read(fd, buf, size);
    if (shift < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (started_reading)
        continue;
      else
        return 0;
    }
    CHK_ERRNO(shift);
    if (!shift)
      return -1;
    buf += shift;
    size -= shift;
    started_reading = 1;
  }
  return 1;
}

int send(void *self_, local_id dst_, Message const *msg) {
  struct Self *self = self_;
  int fd = self->pipes[2 * (self->id * self->n_processes + dst_) + 1];
  CHK_RETCODE(write_repeat(
      fd, (char *)msg, sizeof(MessageHeader) + msg->s_header.s_payload_len));
  return 1;
}

int send_multicast(void *self_, Message const *msg) {
  struct Self *self = self_;
  for (size_t i = 0; i < self->n_processes; ++i) {
    if (i != self->id)
      send(self, (local_id)i, msg);
  }
  self->local_time = msg->s_header.s_local_time + 1;
  return 1;
}

int receive(void *self_, local_id from, Message *msg) {
  struct Self *self = self_;
  int fd = self->pipes[2 * (from * self->n_processes + self->id)];
  int retcode = read_repeat(fd, (char *)&msg->s_header, sizeof(MessageHeader));
  CHK_RETCODE_ZERO(retcode);
  retcode = 0;
  while (msg->s_header.s_payload_len && !retcode) {
    retcode = read_repeat(fd, msg->s_payload, msg->s_header.s_payload_len);
    CHK_RETCODE(retcode);
  }
  if (self->local_time < msg->s_header.s_local_time)
    self->local_time = msg->s_header.s_local_time;
  ++self->local_time;
  return 1;
}

int receive_any(void *self_, Message *msg) {
  struct Self *self = self_;
  for (size_t i = 0; i < self->n_processes; ++i) {
    if (i != self->id) {
      int retcode = receive(self, i, msg);
      CHK_RETCODE(retcode);
      if (retcode)
        return retcode;
    }
  }
  return 0;
}
