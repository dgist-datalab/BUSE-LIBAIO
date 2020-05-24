#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <libaio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/socket.h> 
#include <netinet/in.h>

#include "io.h"
#include "lfqueue.h"

#define MIN_NR 0
#define MAX_NR 128

int dev_fd;
io_context_t ioctx;
volatile int io_is_exit = 0;
pthread_t io_tid[2];

extern struct queue *submit_q;
extern struct queue *complete_q;

struct io_req *io_prepare_req (struct io_req **_io_req, struct buse_req *b_req) {
	struct io_req *io_req;
	(*_io_req) = (struct io_req *)malloc(sizeof(struct io_req));
	io_req = *_io_req;
	//posix_memalign(&io_req->buf, ALIGN_UNIT, b_req->len);
	io_req->buf = b_req->buf;
	io_req->type = b_req->type;
	io_req->size = b_req->len;
	io_req->offset = b_req->offset;
	io_req->complete = io_complete_req;
	io_req->private1 = b_req;
	io_req->iocbs = &io_req->iocb;

	switch(io_req->type) {
		case REQ_R:
			io_prep_pread(&io_req->iocb, dev_fd, io_req->buf, io_req->size, io_req->offset);
			io_req->iocb.data = io_req;
			break;
		case REQ_W:
			//memcpy(io_req->buf, b_req->user_buf, io_req->size);
			io_prep_pwrite(&io_req->iocb, dev_fd, io_req->buf, io_req->size, io_req->offset);
			io_req->iocb.data = io_req;
			break;
		case REQ_T:
			break;
		case REQ_F:
			io_prep_fsync(&io_req->iocb, dev_fd);
			io_req->iocb.data = io_req;
			break;
		default:
			break;
	}
	return io_req;
}

int io_free_req (struct io_req *io_req) {
	free(io_req);
	return 0;
}

int io_submit_req (struct io_req *io_req) {
	if (io_submit(ioctx, 1, &io_req->iocbs) != 1) {
		perror("io_submit failed");
	}
	return 0;
}

int io_complete_req (struct io_req *io_req) {
	struct buse_req *b_req = (struct buse_req *)io_req->private1;
	/*
	switch (io_req->type) {
		case REQ_R:
			//memcpy(b_req->user_buf, io_req->buf, b_req->len);
			//b_req->reply.len = htonl(b_req->len);
			break;
		case REQ_W:
			break;
		case REQ_T:
			break;
		case REQ_F:
			break;
		default:
			break;
	}
	*/
	io_free_req(io_req);
	while(q_enqueue(b_req, complete_q) == 0);
	return 0;
}

void *io_handler (void *userdata) {
	void *ret;
	struct buse_req *b_req;
	struct io_req *io_req;
	while (!io_is_exit) {
		if ((ret = q_dequeue(submit_q)) == NULL) {
			continue;
		}
		b_req = (struct buse_req *)ret;
		io_prepare_req(&io_req, b_req);
		io_submit_req(io_req);
	}
	pthread_exit(NULL);
}

void *io_poller (void *userdata) {
	int num_events;
	struct io_event *events = (struct io_event *)malloc(MAX_NR * sizeof(struct io_event));
	struct timespec timeout;
	//struct io_event event;
	struct io_req *io_req;

	timeout.tv_sec = 1;
	timeout.tv_nsec = 0;

	while (!io_is_exit) {
		num_events = io_getevents(ioctx, MIN_NR, MAX_NR, events, &timeout);
		for (int i = 0; i < num_events; i++) {
			struct io_event event = events[i];

			io_req = (struct io_req *)event.data;
			io_req->complete(io_req);
		}
	}
	free(events);
	pthread_exit(NULL);
}

int io_init (void *userdata) {

	memset(&ioctx, 0, sizeof(ioctx));
	if (io_setup(100, &ioctx) != 0) {
		perror("io_setup failed");
	}
	pthread_create(&io_tid[0], NULL, io_handler, NULL);
	pthread_create(&io_tid[1], NULL, io_poller, NULL);
	return 0;
}

int io_destory (void *userdata) {
	void *ret = NULL;
	io_is_exit = 1;
	pthread_join(io_tid[0], &ret);
	pthread_join(io_tid[1], &ret);
	fsync(dev_fd);
	close(dev_fd);
	io_destroy(ioctx);
	return 0;
}
