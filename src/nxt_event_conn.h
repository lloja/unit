
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NXT_EVENT_CONN_H_INCLUDED_
#define _NXT_EVENT_CONN_H_INCLUDED_


typedef nxt_msec_t (*nxt_event_conn_timer_val_t)(nxt_event_conn_t *c,
    uintptr_t data);


#define NXT_EVENT_NO_BUF_PROCESS      0
#define NXT_EVENT_BUF_PROCESS         1
#define NXT_EVENT_BUF_COMPLETION      1

#define NXT_EVENT_TIMER_AUTORESET     1
#define NXT_EVENT_TIMER_NO_AUTORESET  0


typedef struct {
    uint8_t                       process_buffers;
    uint8_t                       autoreset_timer;

    nxt_work_handler_t            ready_handler;
    nxt_work_handler_t            close_handler;
    nxt_work_handler_t            error_handler;

    nxt_work_handler_t            timer_handler;
    nxt_event_conn_timer_val_t    timer_value;
    uintptr_t                     timer_data;
} nxt_event_conn_state_t;


typedef struct {
    double                        average;
    size_t                        limit;
    size_t                        limit_after;
    size_t                        max_limit;
    nxt_msec_t                    last;
} nxt_event_write_rate_t;


typedef struct {

    nxt_work_handler_t            connect;
    nxt_work_handler_t            accept;

    /*
     * The read() with NULL c->read buffer waits readiness of a connection
     * to avoid allocation of read buffer if the connection will time out
     * or will be closed with error.  The kqueue-specific read() can also
     * detect case if a client did not sent anything and has just closed the
     * connection without errors.  In the latter case state's close_handler
     * is called.
     */
    nxt_work_handler_t            read;

    ssize_t                       (*recvbuf)(nxt_event_conn_t *c, nxt_buf_t *b);

    ssize_t                       (*recv)(nxt_event_conn_t *c, void *buf,
                                      size_t size, nxt_uint_t flags);

    /*
     * The write() is an interface to write a buffer chain with a given rate
     * limit.  It calls write_chunk() in a loop and handles write event timer.
     */
    nxt_work_handler_t            write;

    /*
     * The write_chunk() interface writes a buffer chain with a given limit
     * and toggles write event.  SSL/TLS libraries' write_chunk() interface
     * buffers data and calls the library specific send() interface to write
     * the buffered data eventually.
     */
    ssize_t                       (*write_chunk)(nxt_event_conn_t *c,
                                      nxt_buf_t *b, size_t limit);

    /*
     * The sendbuf() is an interface for OS-specific sendfile
     * implementations or simple writev().
     */
    ssize_t                       (*sendbuf)(nxt_event_conn_t *c, nxt_buf_t *b,
                                      size_t limit);
    /*
     * The writev() is an interface to write several nxt_iobuf_t buffers.
     */
    ssize_t                       (*writev)(nxt_event_conn_t *c,
                                      nxt_iobuf_t *iob, nxt_uint_t niob);
    /*
     * The send() is an interface to write a single buffer.  SSL/TLS
     * libraries' send() interface handles also the libraries' errors.
     */
    ssize_t                       (*send)(nxt_event_conn_t *c, void *buf,
                                      size_t size);

    nxt_work_handler_t            shutdown;
} nxt_event_conn_io_t;


struct nxt_event_conn_s {
    /*
     * Must be the first field, since nxt_fd_event_t
     * and nxt_event_conn_t are used interchangeably.
     */
    nxt_fd_event_t                socket;

    nxt_buf_t                     *read;
    const nxt_event_conn_state_t  *read_state;
    nxt_work_queue_t              *read_work_queue;
    nxt_timer_t                   read_timer;

    nxt_buf_t                     *write;
    const nxt_event_conn_state_t  *write_state;
    nxt_work_queue_t              *write_work_queue;
    nxt_event_write_rate_t        *rate;
    nxt_timer_t                   write_timer;

    nxt_off_t                     sent;
    uint32_t                      max_chunk;
    uint32_t                      nbytes;

    nxt_event_conn_io_t           *io;

#if (NXT_SSLTLS || NXT_THREADS)
    /* SunC does not support "zero-sized struct/union". */

    union {
#if (NXT_SSLTLS)
        void                      *ssltls;
#endif
#if (NXT_THREADS)
        nxt_thread_pool_t         *thread_pool;
#endif
    } u;

#endif

    nxt_mem_pool_t                *mem_pool;

    nxt_task_t                    task;
    nxt_log_t                     log;

    nxt_listen_socket_t           *listen;
    nxt_sockaddr_t                *remote;
    nxt_sockaddr_t                *local;
    const char                    *action;

    uint8_t                       peek;
    uint8_t                       blocked;      /* 1 bit */
    uint8_t                       delayed;      /* 1 bit */

#define NXT_CONN_SENDFILE_OFF     0
#define NXT_CONN_SENDFILE_ON      1
#define NXT_CONN_SENDFILE_UNSET   3

    uint8_t                       sendfile;     /* 2 bits */
    uint8_t                       tcp_nodelay;  /* 1 bit */

    nxt_queue_link_t              link;
};


/*
 * The nxt_event_conn_listen_t is separated from nxt_listen_socket_t
 * because nxt_listen_socket_t is one per process whilst each worker
 * thread uses own nxt_event_conn_listen_t.
 */
typedef struct {
    /* Must be the first field. */
    nxt_fd_event_t                socket;

    nxt_task_t                    task;

    uint32_t                      ready;
    uint32_t                      batch;

    /* An accept() interface is cached to minimize memory accesses. */
    nxt_work_handler_t            accept;

    nxt_listen_socket_t           *listen;

    nxt_timer_t                   timer;

    nxt_queue_link_t              link;
} nxt_event_conn_listen_t;


#define                                                                       \
nxt_event_conn_timer_init(ev, c, wq)                                          \
    do {                                                                      \
        (ev)->work_queue = (wq);                                              \
        (ev)->log = &(c)->log;                                                \
        (ev)->precision = NXT_TIMER_DEFAULT_PRECISION;                        \
    } while (0)


#define                                                                       \
nxt_event_read_timer_conn(ev)                                                 \
    nxt_timer_data(ev, nxt_event_conn_t, read_timer)


#define                                                                       \
nxt_event_write_timer_conn(ev)                                                \
    nxt_timer_data(ev, nxt_event_conn_t, write_timer)


#if (NXT_HAVE_UNIX_DOMAIN)

#define                                                                       \
nxt_event_conn_tcp_nodelay_on(task, c)                                        \
    do {                                                                      \
        nxt_int_t  ret;                                                       \
                                                                              \
        if ((c)->remote->u.sockaddr.sa_family != AF_UNIX) {                   \
            ret = nxt_socket_setsockopt(task, (c)->socket.fd, IPPROTO_TCP,    \
                                        TCP_NODELAY, 1);                      \
                                                                              \
            (c)->tcp_nodelay = (ret == NXT_OK);                               \
        }                                                                     \
    } while (0)


#else

#define                                                                       \
nxt_event_conn_tcp_nodelay_on(task, c)                                        \
    do {                                                                      \
        nxt_int_t  ret;                                                       \
                                                                              \
        ret = nxt_socket_setsockopt(task, (c)->socket.fd, IPPROTO_TCP,        \
                                    TCP_NODELAY, 1);                          \
                                                                              \
        (c)->tcp_nodelay = (ret == NXT_OK);                                   \
    } while (0)

#endif


NXT_EXPORT nxt_event_conn_t *nxt_event_conn_create(nxt_mem_pool_t *mp,
    nxt_task_t *task);
void nxt_event_conn_io_shutdown(nxt_task_t *task, void *obj, void *data);
NXT_EXPORT void nxt_event_conn_close(nxt_event_engine_t *engine,
    nxt_event_conn_t *c);

NXT_EXPORT void nxt_event_conn_timer(nxt_event_engine_t *engine,
    nxt_event_conn_t *c, const nxt_event_conn_state_t *state, nxt_timer_t *tev);
NXT_EXPORT void nxt_event_conn_work_queue_set(nxt_event_conn_t *c,
    nxt_work_queue_t *wq);

void nxt_event_conn_sys_socket(nxt_task_t *task, void *obj, void *data);
void nxt_event_conn_io_connect(nxt_task_t *task, void *obj, void *data);
nxt_int_t nxt_event_conn_socket(nxt_task_t *task, nxt_event_conn_t *c);
void nxt_event_conn_connect_test(nxt_task_t *task, void *obj, void *data);
void nxt_event_conn_connect_error(nxt_task_t *task, void *obj, void *data);

NXT_EXPORT nxt_int_t nxt_event_conn_listen(nxt_task_t *task,
    nxt_listen_socket_t *ls);
void nxt_event_conn_io_accept(nxt_task_t *task, void *obj, void *data);
NXT_EXPORT void nxt_event_conn_accept(nxt_task_t *task,
    nxt_event_conn_listen_t *cls, nxt_event_conn_t *c);
void nxt_event_conn_accept_error(nxt_task_t *task, nxt_event_conn_listen_t *cls,
    const char *accept_syscall, nxt_err_t err);

void nxt_conn_wait(nxt_event_conn_t *c);

void nxt_event_conn_io_read(nxt_task_t *task, void *obj, void *data);
ssize_t nxt_event_conn_io_recvbuf(nxt_event_conn_t *c, nxt_buf_t *b);
ssize_t nxt_event_conn_io_recv(nxt_event_conn_t *c, void *buf,
    size_t size, nxt_uint_t flags);

void nxt_conn_io_write(nxt_task_t *task, void *obj, void *data);
ssize_t nxt_conn_io_sendbuf(nxt_task_t *task, nxt_sendbuf_t *sb);
ssize_t nxt_conn_io_writev(nxt_task_t *task, nxt_sendbuf_t *sb,
    nxt_iobuf_t *iob, nxt_uint_t niob);
ssize_t nxt_conn_io_send(nxt_task_t *task, nxt_sendbuf_t *sb, void *buf,
    size_t size);

size_t nxt_event_conn_write_limit(nxt_event_conn_t *c);
nxt_bool_t nxt_event_conn_write_delayed(nxt_event_engine_t *engine,
    nxt_event_conn_t *c, size_t sent);
ssize_t nxt_event_conn_io_write_chunk(nxt_event_conn_t *c, nxt_buf_t *b,
    size_t limit);
ssize_t nxt_event_conn_io_writev(nxt_event_conn_t *c, nxt_iobuf_t *iob,
    nxt_uint_t niob);
ssize_t nxt_event_conn_io_send(nxt_event_conn_t *c, void *buf, size_t size);

NXT_EXPORT void nxt_event_conn_io_close(nxt_task_t *task, void *obj,
    void *data);

NXT_EXPORT void nxt_event_conn_job_sendfile(nxt_task_t *task,
    nxt_event_conn_t *c);


#define nxt_event_conn_connect(engine, c)                                     \
    nxt_work_queue_add(&engine->socket_work_queue, nxt_event_conn_sys_socket, \
                       c->socket.task, c, c->socket.data)


#define nxt_event_conn_read(engine, c)                                        \
    do {                                                                      \
        nxt_event_engine_t  *e = engine;                                      \
                                                                              \
        c->socket.read_work_queue = &e->read_work_queue;                      \
                                                                              \
        nxt_work_queue_add(&e->read_work_queue, c->io->read,                  \
                           c->socket.task, c, c->socket.data);                \
    } while (0)


#define nxt_event_conn_write(e, c)                                            \
    do {                                                                      \
        nxt_event_engine_t  *engine = e;                                      \
                                                                              \
        c->socket.write_work_queue = &engine->write_work_queue;               \
                                                                              \
        nxt_work_queue_add(&engine->write_work_queue, c->io->write,           \
                           c->socket.task, c, c->socket.data);                \
    } while (0)


extern nxt_event_conn_io_t       nxt_unix_event_conn_io;


typedef struct {
    /*
     * Client and peer connections are not embedded because already
     * existent connections can be switched to the event connection proxy.
     */
    nxt_event_conn_t             *client;
    nxt_event_conn_t             *peer;
    nxt_buf_t                    *client_buffer;
    nxt_buf_t                    *peer_buffer;

    size_t                       client_buffer_size;
    size_t                       peer_buffer_size;

    nxt_msec_t                   client_wait_timeout;
    nxt_msec_t                   connect_timeout;
    nxt_msec_t                   reconnect_timeout;
    nxt_msec_t                   peer_wait_timeout;
    nxt_msec_t                   client_write_timeout;
    nxt_msec_t                   peer_write_timeout;

    uint8_t                      connected;  /* 1 bit */
    uint8_t                      delayed;    /* 1 bit */
    uint8_t                      retries;    /* 8 bits */
    uint8_t                      retain;     /* 2 bits */

    nxt_work_handler_t           completion_handler;
} nxt_event_conn_proxy_t;


NXT_EXPORT nxt_event_conn_proxy_t *nxt_event_conn_proxy_create(
    nxt_event_conn_t *c);
NXT_EXPORT void nxt_event_conn_proxy(nxt_task_t *task,
    nxt_event_conn_proxy_t *p);


#endif /* _NXT_EVENT_CONN_H_INCLUDED_ */