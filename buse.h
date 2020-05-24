#ifndef BUSE_H_INCLUDED
#define BUSE_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

enum req_type {
	REQ_R = 0,
	REQ_W,
	REQ_T,
	REQ_F
};


#include <sys/types.h>
#include <linux/nbd.h>
#define DEVNAME "/dev/nvme1n1"

  struct buse_operations {
    int (*read)(int sk, void *buf, u_int32_t len, u_int64_t offset, void *userdata, struct nbd_request *request);
    int (*write)(int sk, void *buf, u_int32_t len, u_int64_t offset, void *userdata, struct nbd_request *request);
    void (*disc)(int sk, void *userdata, struct nbd_request *request);
    int (*flush)(int sk, void *userdata, struct nbd_request *request);
    int (*trim)(int sk, u_int64_t from, u_int32_t len, void *userdata, struct nbd_request *request);

    // either set size, OR set both blksize and size_blocks
    u_int64_t size;
    u_int32_t blksize;
    u_int64_t size_blocks;
  };

  int buse_main(const char* dev_file, const struct buse_operations *bop, void *userdata);

  struct buse_req *buse_prepare_req (struct buse_req **, int, u_int32_t, u_int64_t, enum req_type, void *, struct nbd_request *);
  int buse_submit_req (struct buse_req *);
  int buse_init(void *);
  int buse_destroy(void *);
  int write_all(int, char*, size_t);
  int read_all(int, char*, size_t);
  
  struct buse_req {
	 int sk;
	 u_int32_t len;
	 u_int64_t offset;
	 enum req_type type;
	 struct nbd_reply reply;
	 void *buf;
	 u_int64_t seq;
  };



#ifdef __cplusplus
}
#endif

#endif /* BUSE_H_INCLUDED */
