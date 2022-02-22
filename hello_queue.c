#include <minix/drivers.h>
#include <minix/chardriver.h>
#include <stdio.h>
#include <stdlib.h>
#include <minix/ds.h>
#include <sys/ioc_hello_queue.h>
#include <sys/ioctl.h>
#include <stdio.h>

#include "hello_queue.h"

/*
 * Function prototypes for the hello driver.
 */
static int hello_queue_open(devminor_t minor, int access, endpoint_t user_endpt);
static int hello_queue_close(devminor_t minor);
static ssize_t hello_queue_read(devminor_t minor, u64_t position, endpoint_t endpt,
                                cp_grant_id_t grant, size_t size, int flags, cdev_id_t id);
static ssize_t hello_queue_write(devminor_t minor, u64_t position, endpoint_t endpt,
                     cp_grant_id_t grant, size_t size, int flags, cdev_id_t id);
static int hello_queue_ioctl(devminor_t minor, unsigned long request, endpoint_t endpt,
                 cp_grant_id_t grant, int flags, endpoint_t user_endpt, cdev_id_t id);
static int do_reset();
static int do_set(endpoint_t endpt, cp_grant_id_t grant);
static int do_exchange(endpoint_t endpt, cp_grant_id_t grant);
static int do_delete();
static void initialize();
static void increase_buffer_size(size_t limit);

/* SEF functions and variables. */
static void sef_local_startup(void);
static int sef_cb_init(int type, sef_init_info_t *info);
static int sef_cb_lu_state_save(int);
static int lu_state_restore(void);

/* Entry points to the hello_queue driver. */
static struct chardriver hello_queue_tab =
        {
                .cdr_open	= hello_queue_open,
                .cdr_close	= hello_queue_close,
                .cdr_read	= hello_queue_read,
                .cdr_write	= hello_queue_write,
                .cdr_ioctl  = hello_queue_ioctl,
        };


static char *hello_queue;
static size_t queue_size;
static size_t buffer_size;

static int hello_queue_open(devminor_t UNUSED(minor), int UNUSED(access),
        endpoint_t UNUSED(user_endpt))
{
    return OK;
}

static int hello_queue_close(devminor_t UNUSED(minor))
{
    return OK;
}

static ssize_t hello_queue_read(devminor_t UNUSED(minor), u64_t UNUSED(position),
                          endpoint_t endpt, cp_grant_id_t grant, size_t size, int UNUSED(flags),
                          cdev_id_t UNUSED(id))
{
    int ret;

    /* Possibly limit the read size. */
    if (size > queue_size)
        size = queue_size;

    if (size == 0)
        return 0;

    if ((ret = sys_safecopyto(endpt, grant, 0, (vir_bytes)hello_queue, size)) != OK)
        return ret;

    /* Move left elements. */
    for (int i = size; i < queue_size; ++i) {
        hello_queue[i - size] = hello_queue[i];
    }
    queue_size -= size;

    /* Possibly resize buffer. */
    if (buffer_size > 1 && 4 * queue_size <= buffer_size) {
        hello_queue = realloc(hello_queue, buffer_size / 2 * sizeof(char));
        if (hello_queue == NULL)
            exit(1);

        buffer_size /= 2;
    }

    /* Return the number of bytes read. */
    return size;
}

static ssize_t hello_queue_write(devminor_t UNUSED(minor), u64_t UNUSED(position), endpoint_t endpt,
                                 cp_grant_id_t grant, size_t size, int UNUSED(flags), cdev_id_t UNUSED(id)) {
    int ret;

    if (size == 0)
        return 0;

    /* Possibly resize buffer. */
    increase_buffer_size(queue_size + size);

    if ((ret = sys_safecopyfrom(endpt, grant, 0, (vir_bytes)(hello_queue + queue_size), size)) != OK)
        return ret;

    queue_size += size;
    return size;
}

static int hello_queue_ioctl(devminor_t minor, unsigned long request, endpoint_t endpt,
                             cp_grant_id_t grant, int flags, endpoint_t user_endpt, cdev_id_t id) {
    int r;

    switch (request) {
        case HQIOCRES:
            r = do_reset();
            return r;
        case HQIOCSET:
            r = do_set(endpt, grant);
            return r;
        case HQIOCXCH:
            r = do_exchange(endpt, grant);
            return r;
        case HQIOCDEL:
            r = do_delete();
            return r;
    }

    return ENOTTY;
}

static int do_reset() {
    hello_queue = realloc(hello_queue, DEVICE_SIZE * sizeof(char));
    if (hello_queue == NULL)
        exit(1);

    initialize();

    return OK;
}

static int do_set(endpoint_t endpt, cp_grant_id_t grant) {
    int ret;

    if (MSG_SIZE <= 0)
        return -1;

    increase_buffer_size(MSG_SIZE);

    if (MSG_SIZE >= queue_size) {
        if ((ret = sys_safecopyfrom(endpt, grant, 0, (vir_bytes)hello_queue, MSG_SIZE)) != OK)
            return ret;

        queue_size = MSG_SIZE;
    }
    else {
        if ((ret = sys_safecopyfrom(endpt, grant, 0, (vir_bytes)(hello_queue + queue_size - MSG_SIZE), MSG_SIZE)) != OK)
            return ret;
    }

    return OK;
}

static int do_exchange(endpoint_t endpt, cp_grant_id_t grant) {
    char change[2];
    int ret;

    if ((ret = sys_safecopyfrom(endpt, grant, 0, (vir_bytes)change, 2)) != OK)
        return ret;

    for (int i = 0; i < queue_size; ++i) {
        if (hello_queue[i] == change[0])
            hello_queue[i] = change[1];
    }

    return OK;
}

static int do_delete() {
    size_t current_size = 0;

    for (size_t i = 0; i < queue_size; ++i) {
        if (i % 3 != 2) {
            hello_queue[current_size] = hello_queue[i];
            ++current_size;
        }
    }

    queue_size = current_size;
    return OK;
}

static void initialize() {
    queue_size = DEVICE_SIZE;
    buffer_size = DEVICE_SIZE;
    for (int i = 0; i < DEVICE_SIZE; ++i) {
        int mod = i % 3;
        switch (mod) {
            case 0:
                hello_queue[i] = 'x';
                break;
            case 1:
                hello_queue[i] = 'y';
                break;
            default:
                hello_queue[i] = 'z';
                break;
        }
    }
}

static void increase_buffer_size(size_t limit) {
    if (limit > buffer_size) {
        while (limit > buffer_size) {
            buffer_size *= 2;
        }

        hello_queue = realloc(hello_queue, buffer_size * sizeof(char));
        if (hello_queue == NULL)
            exit(1);
    }
}

static int sef_cb_lu_state_save(int UNUSED(state)) {
    /* Save the state. */
    ds_publish_u32("queue_size", queue_size, DSF_OVERWRITE);
    ds_publish_u32("buffer_size", buffer_size, DSF_OVERWRITE);
    ds_publish_mem("hello_queue", hello_queue, queue_size, DSF_OVERWRITE);

    return OK;
}

static int lu_state_restore() {
    /* Restore the state. */
    ds_retrieve_u32("queue_size", &queue_size);
    ds_retrieve_u32("buffer_size", &buffer_size);

    hello_queue = malloc(buffer_size * sizeof(char));
    if (hello_queue == NULL) {
        exit(1);
    }
    ds_retrieve_mem("hello_queue", hello_queue, &queue_size);

    ds_delete_u32("queue_size");
    ds_delete_u32("buffer_size");
    ds_delete_mem("hello_queue");

    return OK;
}

static void sef_local_startup()
{
    /*
     * Register init callbacks. Use the same function for all event types
     */
    sef_setcb_init_fresh(sef_cb_init);
    sef_setcb_init_lu(sef_cb_init);
    sef_setcb_init_restart(sef_cb_init);

    /*
     * Register live update callbacks.
     */
    /* - Agree to update immediately when LU is requested in a valid state. */
    sef_setcb_lu_prepare(sef_cb_lu_prepare_always_ready);
    /* - Support live update starting from any standard state. */
    sef_setcb_lu_state_isvalid(sef_cb_lu_state_isvalid_standard);
    /* - Register a custom routine to save the state. */
    sef_setcb_lu_state_save(sef_cb_lu_state_save);

    /* Let SEF perform startup. */
    sef_startup();
}

static int sef_cb_init(int type, sef_init_info_t *UNUSED(info))
{
    /* Initialize the hello driver. */
    int do_announce_driver = TRUE;

    switch(type) {
        case SEF_INIT_FRESH:
            hello_queue = malloc(DEVICE_SIZE * sizeof(char));
            if (hello_queue == NULL) {
                exit(1);
            }
            initialize();
            break;

        case SEF_INIT_LU:
            /* Restore the state. */
            lu_state_restore();
            do_announce_driver = FALSE;
            break;

        case SEF_INIT_RESTART:
            /* Restore the state. */
            lu_state_restore();
            break;
    }

    /* Announce we are up when necessary. */
    if (do_announce_driver) {
        chardriver_announce();
    }

    /* Initialization completed successfully. */
    return OK;
}

int main(void)
{
    /*
     * Perform initialization.
     */
    sef_local_startup();

    /*
     * Run the main loop.
     */
    chardriver_task(&hello_queue_tab);
    free(hello_queue);
    return OK;
}
