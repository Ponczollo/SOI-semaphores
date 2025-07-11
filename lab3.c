#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/prctl.h>

#define SHARED 1
#define TRUE 1


// === STRUCTS ===

struct fifoqueue_s{
  unsigned int queue_size;
  unsigned int taken_size;
  char* content;
};
typedef struct fifoqueue_s fifoqueue_t;

struct buffer_s{
  char name;
  sem_t empty;
  sem_t full;
  sem_t mutex;
  fifoqueue_t fifoqueue;
};
typedef struct buffer_s buffer_t;

struct producer_s{
  char name;
  unsigned int n_buffers;
  char product;
  buffer_t** write_buffers;
};
typedef struct producer_s producer_t;

struct consumer_s{
  char name;
  unsigned int n_buffers;
  buffer_t** read_buffers;
};
typedef struct consumer_s consumer_t;


// === SHARED MALLOC ===

void* shared_malloc(size_t size) {
  int prot = PROT_READ | PROT_WRITE;
  int flags = MAP_SHARED | MAP_ANON;
  return mmap(NULL, size, prot, flags, -1, 0);
}


// === CONSTRUCTORS ===

void init_buffer(buffer_t* buffer, int buffer_size, char name)
{
  buffer->name = name;
  sem_init(&(buffer->empty), SHARED, buffer_size);
  sem_init(&(buffer->full), SHARED, 0);
  sem_init(&(buffer->mutex), SHARED, 1);
  buffer->fifoqueue.taken_size = 0;
  buffer->fifoqueue.queue_size = buffer_size;
  buffer->fifoqueue.content = shared_malloc(sizeof(char) * buffer_size);
}

buffer_t* construct_buffer(int buffer_size, char name){
  buffer_t* retv = shared_malloc(sizeof(buffer_t));

  init_buffer(retv, buffer_size, name);

  return retv;
}

void init_producer(producer_t* producer, char name, int n_buffers, char product, buffer_t* write_buffers[])
{
  producer->name = name;
  producer->n_buffers = n_buffers;
  producer->write_buffers = write_buffers;
  producer->product = product;
}

producer_t* construct_producer(char name, int n_buffers, char product, buffer_t* write_buffers[])
{
  producer_t* retv = malloc(sizeof(producer_t));

  init_producer(retv, name, n_buffers, product, write_buffers);

  return retv; 
}

void init_consumer(consumer_t* consumer, char name, int n_buffers, buffer_t* read_buffers[])
{
  consumer->n_buffers = n_buffers;
  consumer->name = name;
  consumer->read_buffers = read_buffers;
}

consumer_t* construct_consumer(char name, int n_buffers, buffer_t* read_buffers[])
{
  consumer_t* retv = malloc(sizeof(consumer_t));

  init_consumer(retv, name, n_buffers, read_buffers);

  return retv; 
}


// === SEMAPHORES INCREMENTATION AND DECREMENTATION ===

void sem_inc(sem_t* sem)
{
  if(sem_post(sem) !=0)
    __THROW "sem increment error";
}

void sem_dec(sem_t* sem)
{
  if(sem_wait(sem) !=0)
    __THROW "sem_wait error";
}


// === FIFOQUEUE ===

void take_item(fifoqueue_t* queue, char value)
{
  if (queue->taken_size == queue->queue_size) __THROW "IndexError - queue is full";

  queue->content[queue->taken_size] = value;
  queue->taken_size++;
}

char remove_item(fifoqueue_t* queue)
{
  if (queue->taken_size == 0) __THROW "IndexError - queue is empty";

  char retv = queue->content[0];

  if (queue->taken_size > 1)
    memcpy(queue->content, queue->content + 1, (queue->taken_size - 1) * sizeof(char));
  
  queue->taken_size--;
  return retv;
}


// === PRINTS ===

void blue()
{
  printf("\033[0;34m");
}

void red()
{
  printf("\033[0;31m");
}

void green()
{
  printf("\033[0;32m");
}

void print_produce(producer_t* producer)
{
  green();
  printf("(ITEM PRODUCED)\tProducer: %c\n", producer->name);
}

void print_take(buffer_t* buffer, char product)
{
  blue();
  printf("(ITEM TAKEN)\tBuffer: %c\t Item: %c\n", buffer->name, product);
}

void print_remove(buffer_t* buffer, char product, char consumer_name)
{
  red();
  printf("(ITEM REMOVED)\tBuffer: %c\t Item: %c\t Consumer: %c\n", buffer->name, product, consumer_name);
}


// === CHILD PROCESESS ===

void producer_run(producer_t* producer)
{
  while(TRUE)
  {
    for (int i = 0; i < producer->n_buffers; i++){
      print_produce(producer);
      
      sem_dec(&(producer->write_buffers[i]->empty));
      sem_dec(&(producer->write_buffers[i]->mutex));

      take_item(&(producer->write_buffers[i]->fifoqueue), producer->product);
      print_take(producer->write_buffers[i], producer->product);

      sem_inc(&(producer->write_buffers[i]->mutex));
      sem_inc(&(producer->write_buffers[i]->full));

      if(prctl(PR_SET_PDEATHSIG, SIGHUP) == -1) exit(0);
      
      sleep(1);
    }
  }
}

void consumer_run(consumer_t* consumer)
{
  char to_remove;
  while(TRUE)
  {
    for(int i = 0; i < consumer->n_buffers; i++)
    {
      sem_dec(&(consumer->read_buffers[i]->full));
      sem_dec(&(consumer->read_buffers[i]->mutex));

      to_remove = remove_item(&(consumer->read_buffers[i]->fifoqueue));
      print_remove(consumer->read_buffers[i], to_remove, consumer->name);

      sem_inc(&(consumer->read_buffers[i]->mutex));
      sem_inc(&(consumer->read_buffers[i]->empty));

      if(prctl(PR_SET_PDEATHSIG, SIGHUP) == -1) exit(0);

      sleep(1);
    }
  }
}

bool child_check()
{
  return fork() == 0;
}


// TERMINATE PROGRAM

void handle_signal(int sig)
{
  printf("Recieved signal: %d, terminating process...", sig);
  exit(0);
}


// === MAIN ===

int main()
{
  // BUFFERS
  buffer_t* buffers[] = {construct_buffer(1, '1'), construct_buffer(2, '2'), construct_buffer(3, '3'), construct_buffer(4, '4')};

  // PRODUCER A
  if(child_check())
  {
    producer_t* producer_A = construct_producer('A', 1, 'a', &buffers[0]);
    producer_run(producer_A);
  }
  // PRODUCER B
  if(child_check())
  {
    producer_t* producer_B = construct_producer('B', 1, 'b', &buffers[3]);
    producer_run(producer_B);
  }
  // PRODUCER C
  if(child_check())
  {
    producer_t* producer_C = construct_producer('C', 4, 'c', buffers);
    producer_run(producer_C);
  }
  // CONSUMER 1
  if(child_check())
  {
    consumer_t* consumer_1 = construct_consumer('1', 1, &buffers[0]);
    consumer_run(consumer_1);
  }
  // CONSUMER 2
  if(child_check())
  {
    consumer_t* consumer_2 = construct_consumer('2', 1, &buffers[1]);
    consumer_run(consumer_2);
  }
  // CONSUMER 3
  if(child_check())
  {
    consumer_t* consumer_3 = construct_consumer('3', 1, &buffers[2]);
    consumer_run(consumer_3);
  }
  // CONSUMER 4
  /*
  if(child_check())
  {
    consumer_t* consumer_4 = construct_consumer('4', 1, &buffers[3]);
    consumer_run(consumer_4);
  }
  */

  while(TRUE) {}

  return 0;
}
