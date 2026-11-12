#include <stddef.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/types.h>

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
};

int read_cpu_stat(struct cpu_stat *stat)
{
  char buf[256];

  FILE *fp = fopen("/proc/stat", "r");
  if(fp == NULL) {
    printf("file open failed\n");
    return 0;
  }

  if(fgets(buf, sizeof(buf), fp) == NULL) {
    if(feof(fp)) {
      printf("reached EOF\n");
    } else if(ferror(fp)) {
      printf("fgets() failed to read\n");
    }
    fclose(fp);
    return 0;
  }

  unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
  int parsed_count = sscanf(
      buf,
      "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
      &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);

  if (parsed_count != 8) {
    printf("CPU stat parsing failed\n");
    fclose(fp);
    return 0;
  }

  stat->total = user + nice + system + idle
             + iowait + irq + softirq + steal;
  stat->idle  = idle + iowait;

  fclose(fp);
  return 0;
}



void *producer(void *arg)
{
  printf("Producer thread started\n");
  struct monitor_queue *queue = arg;

pthread_mutex_lock(&queue->mutex);

  while(queue->used >= QUEUE_SIZE)
  {
    printf("Producer waiting...\n");
    pthread_cond_wait(&queue->not_full, &queue->mutex);
    printf("Producer awakened\n");
  }

  queue->q[queue->head].type_identifier = 0;
  queue->q[queue->head].data.proc.cpu_data = 35.7f;
  queue->head = (queue->head + 1) % QUEUE_SIZE;
  queue->used += 1;

  pthread_cond_signal(&queue->not_empty);

  printf("used: %zu\n", queue->used);

pthread_mutex_unlock(&queue->mutex);

  return NULL;
}

void *consumer(void *arg)
{
  printf("Consumer thread started\n");
  struct monitor_queue *queue = arg;

pthread_mutex_lock(&queue->mutex);

  while(queue->used == 0)
  {
    printf("Consumer waiting...\n");
    pthread_cond_wait(&queue->not_empty, &queue->mutex);
    printf("Consumer awakened\n");
  }

  struct monitor_data data = queue->q[queue->tail];
  queue->tail = (queue->tail + 1) % QUEUE_SIZE;
  queue->used -= 1;

  pthread_cond_signal(&queue->not_full);

pthread_mutex_unlock(&queue->mutex);

  printf("value: %f\n", data.data.proc.cpu_data);

  return NULL;

}

int main()
{
  struct monitor_queue queue;
  pthread_t thread_id_P;
  pthread_t thread_id_C;

  struct cpu_stat curr;

  queue.head = 0;
  queue.tail = 0;
  queue.used = 0;

  pthread_mutex_init(&queue.mutex, NULL);
  pthread_cond_init(&queue.not_empty, NULL);
  pthread_cond_init(&queue.not_full, NULL);

  // test
  for(int i=0; i<QUEUE_SIZE; i++)
  {
    queue.q[i].type_identifier = 0;
    queue.q[i].data.proc.cpu_data = 35.7f;

    queue.used += 1;
  }


  if(pthread_create(&thread_id_P, NULL, producer, &queue) != 0) {
    perror("thread create failed\n");
    return 1;
  }

  if(pthread_create(&thread_id_C, NULL, consumer, &queue) != 0) {
    perror("consumer thread create failed\n");
    return 1;
  }


  pthread_join(thread_id_P, NULL);
  pthread_join(thread_id_C, NULL);

  printf("used: %zu, head: %zu, cpu_data: %f\n", queue.used, queue.head, queue.q[0].data.proc.cpu_data);

  int result = read_cpu_stat(&curr);

  if(result == 0) {
     printf("total=%llu, idle=%llu\n", curr.total, curr.idle);
  } else {
     printf("CPU stat read failed\n");
  }

  pthread_mutex_destroy(&queue.mutex);
  pthread_cond_destroy(&queue.not_empty);
  pthread_cond_destroy(&queue.not_full);

  return 0;
}
