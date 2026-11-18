#include <stddef.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <errno.h>

#define BUF_SIZE 256
#define QUEUE_SIZE 16


struct proc_data {
	float cpu_data;
	float mem_data;
};

struct mydevice_data {
	char usr_buf[BUF_SIZE];
	ssize_t length;
};

struct cpu_stat {
  unsigned long long total;
  unsigned long long idle;
  };

struct monitor_data {
	int type_identifier;

	union {
		struct proc_data proc;
		struct mydevice_data mydev;
		} data;
};

struct monitor_queue {
        pthread_mutex_t mutex;
        pthread_cond_t not_empty;
        pthread_cond_t not_full;

        struct monitor_data q[QUEUE_SIZE];
	size_t head;
	size_t tail;
	size_t used;
	int active_producers;
	int stop_requested;
};

void producer_finished(struct monitor_queue *queue)
{
  pthread_mutex_lock(&queue->mutex);

  queue->active_producers--;

  pthread_cond_broadcast(&queue->not_empty);

  pthread_mutex_unlock(&queue->mutex);
}

void *mydevice_producer(void *arg) {

  struct monitor_queue *queue = arg;

  int fd = open("/dev/mydevice", O_RDONLY);
  if(fd < 0) {
    perror("mydevice driver open failed");
    goto cleanup;
  }

  struct pollfd pfd = {
  .fd =  fd,
  .events =  POLLIN,
  .revents = 0
};

  while(1) {

  pthread_mutex_lock(&queue->mutex);
  int stop = queue->stop_requested;
  pthread_mutex_unlock(&queue->mutex);

  if(stop) break;

  int poll_ret = poll(&pfd, 1, 1000);

  if(poll_ret == 0) continue;

  if(poll_ret < 0) {
      if(errno == EINTR) {
        continue;
      }

      perror("poll failed");
      goto cleanup;
  }

  if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
    fprintf(stderr, "mydevice poll event error: 0x%x\n",
            (unsigned int)pfd.revents);
    goto cleanup;
  }

  if(poll_ret > 0 && (pfd.revents & POLLIN)) {
    struct mydevice_data usr_data;

    ssize_t bytes_read = read(
        fd,
        usr_data.usr_buf,
        sizeof(usr_data.usr_buf)
    );

  if(bytes_read < 0) {
    if(errno == EAGAIN || errno == EINTR) continue;

    perror("mydevice read failed");
    goto cleanup;
    }

  usr_data.length = bytes_read;

  if(bytes_read > 0) {
    printf("Read %zd bytes\n", usr_data.length);

pthread_mutex_lock(&queue->mutex);

  while (queue->used >= QUEUE_SIZE && !queue->stop_requested) {
    pthread_cond_wait(&queue->not_full, &queue->mutex);
  }
  if(queue->stop_requested) {
    pthread_mutex_unlock(&queue->mutex);
    goto cleanup;
  }

  queue->q[queue->head].type_identifier = 1;
  queue->q[queue->head].data.mydev = usr_data;

  queue->head = (queue->head + 1) % QUEUE_SIZE;
  queue->used++;

pthread_cond_signal(&queue->not_empty);

pthread_mutex_unlock(&queue->mutex);
    }
  }
}
cleanup:
  if(fd >= 0) {
      close(fd);
  }

  producer_finished(queue);
  return NULL;
}

int read_cpu_stat(struct cpu_stat *stat)
{
  char buf[256];

  FILE *fp = fopen("/proc/stat", "r");
  if(fp == NULL) {
    printf("file open failed\n");
    return -1;
  }

  if(fgets(buf, sizeof(buf), fp) == NULL) {
    if(feof(fp)) {
      printf("reached EOF\n");
    } else if(ferror(fp)) {
      printf("fgets() failed to read\n");
    }
    fclose(fp);
    return -1;
  }

  unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
  int parsed_count = sscanf(
      buf,
      "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
      &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);

  if (parsed_count != 8) {
    printf("CPU stat parsing failed\n");
    fclose(fp);
    return -1;
  }

  stat->total = user + nice + system + idle
             + iowait + irq + softirq + steal;
  stat->idle  = idle + iowait;

  fclose(fp);
  return 0;
}

int read_mem_usage(float *usage)
{
  if(usage == NULL) {
      printf("usage is NULL\n");
      return -1;
  }
  char buf[256];
  int found_total = 0;
  int found_available = 0;

  FILE *fp = fopen("/proc/meminfo", "r");
  if(fp == NULL) {
    printf("file open failed\n");
    return -1;
  }

  unsigned long long mem_total = 0;
  unsigned long long mem_available = 0;

  while(fgets(buf, sizeof(buf), fp) != NULL) {

	int parsed_count = sscanf(buf, "MemTotal: %llu", &mem_total);

	if(parsed_count == 1) {
		printf("MemTotal: %llu\n", mem_total);
		found_total = 1;
	}

	parsed_count = sscanf(buf, "MemAvailable: %llu", &mem_available);

	if(parsed_count == 1) {
		printf("MemAvailable: %llu\n", mem_available);
		found_available = 1;
	}
}

  if(ferror(fp)) {
	  printf("File read failed\n");
	  fclose(fp);
	  return -1;
   }

  fclose(fp);

  if(!found_total || !found_available || mem_total == 0 || mem_available > mem_total) return -1;

  *usage = (float)(mem_total - mem_available) / mem_total * 100.0f;

  return 0;
}

int calculate_cpu_usage(
    const struct cpu_stat *prev,
    const struct cpu_stat *curr,
    float *usage) {

  if(prev == NULL || curr == NULL) {
    printf("pointer is NULL\n");
    return -1; }

  if(usage == NULL) {
    printf("usage is NULL\n");
    return -1; }

  if(curr->total < prev->total || curr->idle < prev->idle) return -1;

  unsigned long long delta_total = curr->total - prev->total;
  unsigned long long delta_idle  = curr->idle - prev->idle;

  if(delta_idle > delta_total) return -1;

  if(delta_total == 0) {
    printf("Invalid CPU time delta\n");
    return -1;
  } else {
      float cpu_usage = (float)(delta_total - delta_idle) / delta_total * 100.0f;
      *usage = cpu_usage;
  }

  return 0;
    }

void *proc_producer(void *arg)
{
  printf("Producer thread started\n");
  struct monitor_queue *queue = arg;

  struct cpu_stat curr;
  struct cpu_stat prev;
  float cpu_percent;
  float mem_percent;

  int prev_result = read_cpu_stat(&prev);

  if(prev_result != 0) {
        printf("CPU stat read failed\n");
        goto cleanup;
        }

  for(int i=0; i<30; i++) {

      sleep(1);

      int curr_result = read_cpu_stat(&curr);

    if(curr_result == 0) {

    int calc_result = calculate_cpu_usage(&prev, &curr, &cpu_percent);

    if (calc_result == 0) {
      printf("CPU usage: %.2f%%\n", cpu_percent);
    } else {
      printf("CPU usage calculation failed\n");
      goto cleanup;
    }
  } else {
      printf("CPU stat read failed\n");
      goto cleanup;
  }

  int mem_result = read_mem_usage(&mem_percent);

  if(mem_result != 0) {
      printf("Memory usage calculation failed\n");
      goto cleanup;
  }

  printf("Memory usage: %.2f%%\n", mem_percent);

pthread_mutex_lock(&queue->mutex);

  while(queue->used >= QUEUE_SIZE)
  {
    printf("Producer waiting...\n");
    pthread_cond_wait(&queue->not_full, &queue->mutex);
    printf("Producer awakened\n");
  }

  queue->q[queue->head].type_identifier = 0;
  queue->q[queue->head].data.proc.cpu_data = cpu_percent;
  queue->q[queue->head].data.proc.mem_data = mem_percent;
  queue->head = (queue->head + 1) % QUEUE_SIZE;
  queue->used += 1;

  pthread_cond_signal(&queue->not_empty);

  printf("used: %zu\n", queue->used);

pthread_mutex_unlock(&queue->mutex);

  prev = curr;
  }

cleanup:
  pthread_mutex_lock(&queue->mutex);

  queue->stop_requested = 1;
  pthread_cond_broadcast(&queue->not_full);

  pthread_mutex_unlock(&queue->mutex);

  producer_finished(queue);
  return NULL;
}

void *consumer(void *arg)
{
  printf("Consumer thread started\n");
  struct monitor_queue *queue = arg;

  while(1) {

pthread_mutex_lock(&queue->mutex);

  while(queue->used == 0 && queue->active_producers !=0)
  {
    printf("Consumer waiting...\n");
    pthread_cond_wait(&queue->not_empty, &queue->mutex);
    printf("Consumer awakened\n");
  }

  if(queue->used == 0 && queue->active_producers == 0) {
      pthread_mutex_unlock(&queue->mutex);
      return NULL;
  }

  struct monitor_data data = queue->q[queue->tail];
  queue->tail = (queue->tail + 1) % QUEUE_SIZE;
  queue->used -= 1;

  pthread_cond_signal(&queue->not_full);

pthread_mutex_unlock(&queue->mutex);

  if(data.type_identifier == 0) {
    printf("CPU: %.2f%%\n", data.data.proc.cpu_data);
    printf("Memory: %.2f%%\n", data.data.proc.mem_data);
  } else {
    printf("usr_data: %.*s\n",
          (int)data.data.mydev.length,
          data.data.mydev.usr_buf);
  }
}

return NULL;

}

int main()
{
  struct monitor_queue queue;
  pthread_t thread_id_P;
  pthread_t thread_id_P2;
  pthread_t thread_id_C;

  queue.head = 0;
  queue.tail = 0;
  queue.used = 0;
  queue.active_producers = 2;
  queue.stop_requested = 0;

  pthread_mutex_init(&queue.mutex, NULL);
  pthread_cond_init(&queue.not_empty, NULL);
  pthread_cond_init(&queue.not_full, NULL);

  if(pthread_create(&thread_id_P, NULL, proc_producer, &queue) != 0) {
    perror("thread create failed");
    return 1;
  }

  if(pthread_create(&thread_id_P2, NULL, mydevice_producer, &queue) != 0) {
    perror("thread create failed");
    return 1;
  }

  if(pthread_create(&thread_id_C, NULL, consumer, &queue) != 0) {
    perror("consumer thread create failed");
    return 1;
  }

  pthread_join(thread_id_P, NULL);
  pthread_join(thread_id_P2, NULL);
  pthread_join(thread_id_C, NULL);

  printf("used: %zu, head: %zu, cpu_data: %f, active_producers: %d\n", queue.used, queue.head, queue.q[0].data.proc.cpu_data, queue.active_producers);

  pthread_mutex_destroy(&queue.mutex);
  pthread_cond_destroy(&queue.not_empty);
  pthread_cond_destroy(&queue.not_full);

  return 0;
}
