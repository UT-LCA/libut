/*
 * ioqueues.c
 * Note: code to support network packets passing are dropped
 */

#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <base/hash.h>
#include <base/log.h>
#include <base/lrpc.h>
#include <base/mem.h>
#include <base/thread.h>

#include <hwalloc/shm.h>

#include "defs.h"

#define COMMAND_QUEUE_MCOUNT    4096

DEFINE_SPINLOCK(qlock);
unsigned int nrqs = 0;

struct hwallocd_control hwc;

static int generate_random_key(mem_key_t *key)
{
    int fd, ret;
    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return -1;

    ret = read(fd, key, sizeof(*key));
    close(fd);
    if (ret != sizeof(*key))
        return -1;

    return 0;
}

/* Could be a macro really, this is totally static */
static size_t calculate_shm_space(unsigned int thread_count)
{
    size_t ret = 0, q;

    /* Header + queue_spec information */
    ret += sizeof(struct control_hdr);
    ret += sizeof(struct thread_spec) * thread_count;
    ret = align_up(ret, CACHE_LINE_SIZE);

    /* RX queues (wb is not included) */
    q = sizeof(struct lrpc_msg) * COMMAND_QUEUE_MCOUNT;
    q = align_up(q, CACHE_LINE_SIZE);
    ret += q * thread_count;

    /* TX command queues */
    q = sizeof(struct lrpc_msg) * COMMAND_QUEUE_MCOUNT;
    q = align_up(q, CACHE_LINE_SIZE);
    q += align_up(sizeof(uint32_t), CACHE_LINE_SIZE);
    ret += q * thread_count;

    /* Shared queue pointers for the iokernel to use to determine busyness */
    q = align_up(sizeof(struct q_ptrs), CACHE_LINE_SIZE);
    ret += q * thread_count;

    ret = align_up(ret, PGSIZE_2MB);

    return ret;
}

static void ioqueue_alloc(struct shm_region *r, struct queue_spec *q,
              char **ptr, size_t msg_count, bool alloc_wb)
{
    q->msg_buf = ptr_to_shmptr(r, *ptr, sizeof(struct lrpc_msg) * msg_count);
    *ptr += align_up(sizeof(struct lrpc_msg) * msg_count, CACHE_LINE_SIZE);

    if (alloc_wb) {
        q->wb = ptr_to_shmptr(r, *ptr, sizeof(uint32_t));
        *ptr += align_up(sizeof(uint32_t), CACHE_LINE_SIZE);
    }

    q->msg_count = msg_count;
}

static void queue_pointers_alloc(struct shm_region *r,
        struct thread_spec *tspec, char **ptr)
{
    /* set wb for rxq */
    tspec->rxq.wb = ptr_to_shmptr(r, *ptr, sizeof(struct q_ptrs));

    tspec->q_ptrs = ptr_to_shmptr(r, *ptr, sizeof(struct q_ptrs));
    *((uint32_t *) *ptr) = 0;
    *ptr += align_up(sizeof(struct q_ptrs), CACHE_LINE_SIZE);
}

static int ioqueues_shm_setup(unsigned int threads)
{
    struct shm_region *r = &netcfg.tx_region;
    char *ptr;
    int i, ret;
    size_t shm_len;

    ret = generate_random_key(&hwc.key);
    if (ret < 0)
        return ret;
    hwc.key = rand_crc32c(hwc.key);

    /* map shared memory for control header, command queues */
    shm_len = calculate_shm_space(threads);
    r->len = shm_len;
    r->base = mem_map_shm(hwc.key, NULL, shm_len, PGSIZE_2MB, true);
    if (r->base == MAP_FAILED) {
        log_err("control_setup: mem_map_shm() failed");
        return -1;
    }

    /* set up queues in shared memory */
    hwc.thread_count = threads;
    ptr = r->base;
    ptr += sizeof(struct control_hdr) + sizeof(struct thread_spec) * threads;
    ptr = (char *)align_up((uintptr_t)ptr, CACHE_LINE_SIZE);

    for (i = 0; i < threads; i++) {
        struct thread_spec *tspec = &hwc.threads[i];
        ioqueue_alloc(r, &tspec->rxq, &ptr, COMMAND_QUEUE_MCOUNT, false);
        ioqueue_alloc(r, &tspec->txq, &ptr, COMMAND_QUEUE_MCOUNT, true);

        queue_pointers_alloc(r, tspec, &ptr);
    }

    return 0;
}

static void ioqueues_shm_cleanup(void)
{
    mem_unmap_shm(netcfg.tx_region.base);
}

/*
 * Send an array of n file descriptors fds on the unix control socket fd.
 * Returns the number of bytes sent on success or -1 on error.
 */
static ssize_t ioqueues_send_fds(int fd, int *fds, int n)
{
    struct msghdr msg;
    char buf[CMSG_SPACE(sizeof(int) * n)];
    struct iovec iov[1];
    char iobuf[1];
    struct cmsghdr *cmptr;

    /* init message header, iovec is necessary even though it's unused */
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);
    iov[0].iov_base = iobuf;
    iov[0].iov_len = sizeof(iobuf);
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    /* init control message */
    cmptr = CMSG_FIRSTHDR(&msg);
    cmptr->cmsg_len = CMSG_LEN(sizeof(int) * n);
    cmptr->cmsg_level = SOL_SOCKET;
    cmptr->cmsg_type = SCM_RIGHTS;
    memcpy((int *) CMSG_DATA(cmptr), fds, n * sizeof(int));

    return sendmsg(fd, &msg, 0);
}

/*
 * Register this runtime with the hwallocd. All threads must complete their
 * per-thread ioqueues initialization before this function is called.
 */
int ioqueues_register_hwallocd(void)
{
    struct control_hdr *hdr;
    struct shm_region *r = &netcfg.tx_region;
    struct sockaddr_un addr;
    int ret, i;
    int kthread_fds[NCPU];

    /* initialize control header */
    hdr = r->base;
    hdr->magic = CONTROL_HDR_MAGIC;
    hdr->thread_count = hwc.thread_count;

    hdr->sched_cfg.priority = SCHED_PRIORITY_NORMAL;
    hdr->sched_cfg.max_cores = hwc.thread_count;
    hdr->sched_cfg.guaranteed_cores = guaranteedks;
    hdr->sched_cfg.congestion_latency_us = 0;
    hdr->sched_cfg.scaleout_latency_us = 0;

    memcpy(hdr->threads, hwc.threads,
            sizeof(struct thread_spec) * hwc.thread_count);

    /* register with hwalloc */
    BUILD_ASSERT(strlen(CONTROL_SOCK_PATH) <= sizeof(addr.sun_path) - 1);
    memset(&addr, 0x0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CONTROL_SOCK_PATH, sizeof(addr.sun_path) - 1);

    hwc.fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (hwc.fd == -1) {
        log_err("register_iokernel: socket() failed [%s]", strerror(errno));
        goto fail;
    }

    if (connect(hwc.fd, (struct sockaddr *)&addr,
         sizeof(struct sockaddr_un)) == -1) {
        log_err("register_iokernel: connect() failed [%s]", strerror(errno));
        goto fail_close_fd;
    }

    ret = write(hwc.fd, &hwc.key, sizeof(hwc.key));
    if (ret != sizeof(hwc.key)) {
        log_err("register_iokernel: write() failed [%s]", strerror(errno));
        goto fail_close_fd;
    }

    ret = write(hwc.fd, &netcfg.tx_region.len, sizeof(netcfg.tx_region.len));
    if (ret != sizeof(netcfg.tx_region.len)) {
        log_err("register_iokernel: write() failed [%s]", strerror(errno));
        goto fail_close_fd;
    }

    /* send efds to hwalloc */
    for (i = 0; i < hwc.thread_count; i++)
        kthread_fds[i] = hwc.threads[i].park_efd;
    ret = ioqueues_send_fds(hwc.fd, &kthread_fds[0], hwc.thread_count);
    if (ret < 0) {
        log_err("register_iokernel: ioqueues_send_fds() failed with ret %d",
                ret);
        goto fail_close_fd;
    }

    return 0;

fail_close_fd:
    close(hwc.fd);
fail:
    ioqueues_shm_cleanup();
    return -errno;
}

int ioqueues_init_thread(void)
{
    int ret;
    pid_t tid = thread_gettid();
    struct shm_region *r = &netcfg.tx_region;

    spin_lock(&qlock);
    assert(nrqs < hwc.thread_count);
    struct thread_spec *ts = &hwc.threads[nrqs++];
    ts->tid = tid;
    ts->park_efd = myk()->park_efd;
    spin_unlock(&qlock);

    ret = shm_init_lrpc_in(r, &ts->rxq, &myk()->rxq);
    BUG_ON(ret);

    ret = shm_init_lrpc_out(r, &ts->txq, &myk()->txq);
    BUG_ON(ret);

    myk()->q_ptrs = (struct q_ptrs *) shmptr_to_ptr(r, ts->q_ptrs,
            sizeof(uint32_t));
    BUG_ON(!myk()->q_ptrs);

    return 0;
}

/*
 * General initialization for runtime <-> iokernel communication. Must be
 * called before per-thread ioqueues initialization.
 */
int ioqueues_init()
{
    int ret;

    spin_lock_init(&qlock);

    ret = ioqueues_shm_setup(maxks);
    if (ret) {
        log_err("ioqueues_init: ioqueues_shm_setup() failed, ret = %d", ret);
        return ret;
    }

    return 0;
}
