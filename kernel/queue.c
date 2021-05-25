#include "queue.h"

int dequeue(struct queue *queue)
{
  // if the queue is empty return -1
  if (queue->size == 0)
    return -1;
    
  int item = queue->q[queue->front];
  queue->front = (queue->front + 1) % 32;
  queue->size -= 1;
  return item;
}

void enqueue(struct queue *queue, int item)
{
  // if the queue is full then skip insertion
  if (queue->size == 32) {
    return;   
  }
  
  queue->rear = (queue->rear + 1) % 32;
  queue->q[queue->rear] = item;
  queue->size += 1;
}

void remove_item(struct queue *queue, int item){
  // after removing "item" queue->size will change, so back it up
  int initial_size = queue->size;
  
  for (int i = 0; i < initial_size; i++){
    int tmp = dequeue(queue);
    if (tmp != item) {
      enqueue(queue, tmp);
    }
  }
}

void front_to_rear(struct queue *queue){
  enqueue(queue, dequeue(queue));
}