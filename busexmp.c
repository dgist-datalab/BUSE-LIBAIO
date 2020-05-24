/*
 * busexmp - example memory-based block device using BUSE
 * Copyright (C) 2013 Adam Cozzette
 *
 * This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <argp.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h> 
#include <netinet/in.h>
#include <fcntl.h>
#include <linux/fs.h>

#include "buse.h"
#include "lfqueue.h"
#include "io.h"
#include "heap.h"

/* BUSE callbacks */
//static void *data;

#define ALIGN_UNIT 512
#define QUEUE_SIZE 1024

struct queue *submit_q;
struct queue *complete_q;
struct heap *pq;

volatile int buse_is_exit = 0;
u_int64_t s_seq = 0;
u_int64_t r_seq = 0;
pthread_t buse_tid;
extern int dev_fd;

static int xmp_read(int sk, void *buf, u_int32_t len, u_int64_t offset, void *userdata, struct nbd_request *request)
{
	struct buse_req *b_req;
	if (*(int *)userdata)
		fprintf(stderr, "R - %lu, %u\n", offset, len);
	//memcpy(buf, (char *)data + offset, len);
	buse_prepare_req(&b_req, sk, len, offset, REQ_R, buf, request);
	buse_submit_req(b_req);
	return 0;
}

static int xmp_write(int sk, void *buf, u_int32_t len, u_int64_t offset, void *userdata, struct nbd_request *request)
{
	struct buse_req *b_req;
	if (*(int *)userdata)
		fprintf(stderr, "W - %lu, %u\n", offset, len);

	b_req = buse_prepare_req(&b_req, sk, len, offset, REQ_W, buf, request);
	buse_submit_req(b_req);
	//memcpy((char *)data + offset, buf, len);
	return 0;
}

static void xmp_disc(int sk, void *userdata, struct nbd_request *request)
{
	if (*(int *)userdata)
		fprintf(stderr, "Received a disconnect request.\n");
	buse_destroy(NULL);
}

static int xmp_flush(int sk, void *userdata, struct nbd_request *request)
{
	struct buse_req *b_req;
	if (*(int *)userdata)
		fprintf(stderr, "Received a flush request.\n");
	b_req = buse_prepare_req(&b_req, sk, 0, 0, REQ_F, NULL, request);
	buse_submit_req(b_req);
	return 0;
}

static int xmp_trim(int sk, u_int64_t from, u_int32_t len, void *userdata, struct nbd_request *request)
{
	if (*(int *)userdata)
		fprintf(stderr, "T - %lu, %u\n", from, len);
	return 0;
}

/* argument parsing using argp */

static struct argp_option options[] = {
	{"verbose", 'v', 0, 0, "Produce verbose output", 0},
	{0},
};

struct arguments {
	unsigned long long size;
	char * device;
	int verbose;
};

static unsigned long long strtoull_with_prefix(const char * str, char * * end) {
	unsigned long long v = strtoull(str, end, 0);
	switch (**end) {
		case 'K':
			v *= 1024;
			*end += 1;
			break;
		case 'M':
			v *= 1024 * 1024;
			*end += 1;
			break;
		case 'G':
			v *= 1024 * 1024 * 1024;
			*end += 1;
			break;
	}
	return v;
}

/* Parse a single option. */
static error_t parse_opt(int key, char *arg, struct argp_state *state) {
	struct arguments *arguments = (struct arguments *)(state->input);
	char * endptr;

	switch (key) {

		case 'v':
			arguments->verbose = 1;
			break;

		case ARGP_KEY_ARG:
			switch (state->arg_num) {

				case 0:
					arguments->size = strtoull_with_prefix(arg, &endptr);
					if (*endptr != '\0') {
						/* failed to parse integer */
						errx(EXIT_FAILURE, "SIZE must be an integer");
					}
					break;

				case 1:
					arguments->device = arg;
					break;

				default:
					/* Too many arguments. */
					return ARGP_ERR_UNKNOWN;
			}
			break;

		case ARGP_KEY_END:
			if (state->arg_num < 2) {
				warnx("not enough arguments");
				argp_usage(state);
			}
			break;

		default:
			return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static struct argp argp = {
	.options = options,
	.parser = parse_opt,
	.args_doc = "SIZE DEVICE",
	.doc = "BUSE virtual block device that stores its content in memory.\n"
		"`SIZE` accepts suffixes K, M, G. `DEVICE` is path to block device, for example \"/dev/nbd0\".",
};

//static struct argp argp;


int main(int argc, char *argv[]) {
	/*
	struct arguments arguments;
	memset((void*)&argp, 0, sizeof(struct argp));
	arguments.verbose = 0;
	argp.options = options;
	argp.parser = parse_opt;
	argp.args_doc = "SIZE DEVICE";
	argp.doc = "BUSE virtual block device that stores its content in memory.\n"
		"`SIZE` accepts suffixes K, M, G. `DEVICE` is path to block device, for example \"/dev/nbd0\".";
		*/
	struct arguments arguments = {
		.verbose = 0,
	};
	argp_parse(&argp, argc, argv, 0, 0, &arguments);


	unsigned long numblocks = 0;
	dev_fd = open(DEVNAME, O_RDWR | O_DIRECT);
	ioctl(dev_fd, BLKGETSIZE, &numblocks);
	arguments.size = (u_int64_t)numblocks * 512;

	/*
	struct buse_operations aop;
	aop.read = xmp_read;
	aop.write = xmp_write;
	aop.disc = xmp_disc;
	aop.flush = xmp_flush;
	aop.trim = xmp_trim;
	aop.size = arguments.size;
	*/

	struct buse_operations aop = {
		.read = xmp_read,
		.write = xmp_write,
		.disc = xmp_disc,
		.flush = xmp_flush,
		.trim = xmp_trim,
		.size = arguments.size,
	};

	//data = malloc(aop.size);

	//if (data == NULL) err(EXIT_FAILURE, "failed to alloc space for data");

	buse_init(NULL);

	return buse_main(arguments.device, &aop, (void *)&arguments.verbose);
}

struct buse_req *buse_prepare_req (struct buse_req **_b_req, int sk, u_int32_t len, u_int64_t offset, enum req_type type, void *buf, struct nbd_request *request) {
	struct buse_req *b_req;
	(*_b_req) = (struct buse_req *)malloc(sizeof(struct buse_req));
	b_req = (*_b_req);
	b_req->sk = sk;
	b_req->len = len;
	b_req->offset = offset;
	b_req->type = type;
	b_req->buf = NULL;
	b_req->reply.magic = htonl(NBD_REPLY_MAGIC);
	b_req->reply.error = htonl(0);
	b_req->seq = s_seq++;
	memcpy(b_req->reply.handle, request->handle, sizeof(b_req->reply.handle));

	switch(b_req->type) {
		case REQ_R:
			if (posix_memalign(&b_req->buf, ALIGN_UNIT, b_req->len)) {
				perror("R alloc failed");
			}
			break;
		case REQ_W:
			if (posix_memalign(&b_req->buf, ALIGN_UNIT, b_req->len)) {
				perror("W alloc failed");
			}
			read_all(sk, (char *)b_req->buf, len);
			break;
		case REQ_T:
			break;
		case REQ_F:
			break;
		default:
			break;
	}

	

	return b_req;
}

int buse_free_req (struct buse_req *b_req) {
	if (b_req->buf) {
		free(b_req->buf);
	}
	free(b_req);
	return 0;
}

int buse_submit_req (struct buse_req *b_req) {
	while (q_enqueue(b_req, submit_q) == 0);
	return 0;
}

int buse_complete_req (struct buse_req *b_req) {
	switch (b_req->type) {
		case REQ_R:
			printf("R b_req->seq: %zu, b_req->offset: %zu, b_req->len: %u, b_req->buf: %p\n", b_req->seq, b_req->offset, b_req->len, b_req-> buf);
			write_all(b_req->sk, (char *)&b_req->reply, sizeof(struct nbd_reply));
			write_all(b_req->sk, (char *)b_req->buf, b_req->len);
			break;
		case REQ_W:
			printf("R b_req->seq: %zu, b_req->offset: %zu, b_req->len: %u, b_req->buf: %p\n", b_req->seq, b_req->offset, b_req->len, b_req-> buf);
			write_all(b_req->sk, (char *)&b_req->reply, sizeof(struct nbd_reply));
			break;
		case REQ_T:
			break;
		case REQ_F:
			write_all(b_req->sk, (char *)&b_req->reply, sizeof(struct nbd_reply));
			break;
		default:
			break;
	}
	return 0;
}


void *buse_poller (void *userdata) {
	void *ret, *ret_key, *ret_val;
	u_int64_t min_seq;
	struct buse_req *b_req, *min_b_req;

	while (!buse_is_exit) {
		if ((ret = q_dequeue(complete_q)) == NULL) {
			continue;
		}
		b_req = (struct buse_req *)ret;
		buse_complete_req(b_req);
		buse_free_req(b_req);
		/*
		if (b_req->seq == r_seq) {
			buse_complete_req(b_req);
			buse_free_req(b_req);
			r_seq++;
		} else if (!heap_size(pq)) {
			heap_insert(pq, &b_req->seq, b_req);
		} else {
			heap_insert(pq, &b_req->seq, b_req);
			while (heap_min(pq, &ret_key, &ret_val)) {
				min_seq = *((u_int64_t *)ret_key);
				min_b_req = (struct buse_req *)ret_val;
				if (min_seq == r_seq) {
					heap_delmin(pq, &ret_key, &ret_val);
					buse_complete_req(min_b_req);
					buse_free_req(b_req);
					r_seq++;
				} else {
					break;
				}
			}
		}
		*/
	}
	pthread_exit(NULL);
}


int buse_init (void *userdata) {
	q_init(&submit_q, QUEUE_SIZE);
	q_init(&complete_q, QUEUE_SIZE);
	pq = (struct heap *)malloc(sizeof(struct heap));
	heap_create(pq, QUEUE_SIZE, NULL);
	pthread_create(&buse_tid, NULL, buse_poller, NULL);
	io_init(userdata);
	return 0;
}

int buse_destroy (void *userdata) {
	void *ret = NULL;
	io_destory(userdata);
	buse_is_exit = 1;
	pthread_join(buse_tid, &ret);
	q_free(submit_q);
	q_free(complete_q);
	heap_destroy(pq);
	free(pq);
	return 0;
}
