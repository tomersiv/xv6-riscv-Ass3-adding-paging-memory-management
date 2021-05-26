struct queue {
  int front; // first element in the queue
  int rear; // last element in the queue
  int size; // size of the queue
  int q[32]; // array of page indices for Second Chance Fifo
};

void enqueue(struct queue* queue, int item);
int dequeue(struct queue* queue);
void front_to_rear(struct queue *queue);