#ifndef __IO_H__
#define __IO_H__

#include <sys/types.h>
#include <libaio.h>
#include "buse.h"


struct io_req {
	void *buf;
	enum req_type type;
	size_t size;
	long long offset;
	void *private1;
	void *private2;
	struct iocb iocb;
	struct iocb *iocbs;
	int (*complete)(struct io_req*);
};

struct io_req *io_prepare_req (struct io_req **, struct buse_req *);
int io_free_req (struct io_req *);
int io_submit_req (struct io_req *);
int io_complete_req (struct io_req *);
int io_init (void *);
int io_destory (void *);

#endif
