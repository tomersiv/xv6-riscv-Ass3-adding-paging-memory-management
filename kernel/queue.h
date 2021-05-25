struct queue {
  int front;
  int rear;
  int size;
  int q[32];
};

void enqueue(struct queue* queue, int item);
int dequeue(struct queue* queue);
void front_to_rear(struct queue *queue);