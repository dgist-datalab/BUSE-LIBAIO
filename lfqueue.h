#ifndef __H_QUEUE_Q_
#define __H_QUEUE_Q_
	
#include <pthread.h>
typedef struct node{
	void *n;
	struct node *next;
}node;
typedef struct queue{
	int size;
	int m_size;
	node *head;
	node *tail;
}queue;
void q_init(queue**,int);
int q_enqueue(void *,queue*);
void *q_dequeue(queue*);
void q_free(queue*);
#endif
