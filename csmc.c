#include "common_threads.h"
#include "logger.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* This starter deliberately DOES NOT implement the concurrency assignment. */

/* Temporary: keeps -Werror quiet about things nothing uses yet.
 * Remove from each declaration once it's used, then delete the macro. */
#define SKELETON_UNUSED __attribute__((unused))

typedef struct {
    int student_id;
    int visit;
    unsigned long seq;
} request_t;

typedef struct {
    int id;
    int visits_done;
    int assigned_tutor;
    sem_t wake;
    unsigned int seed;
} student_t;

static pthread_mutex_t lock SKELETON_UNUSED;

/* protected by lock */
static int free_chairs SKELETON_UNUSED;
static unsigned long next_seq SKELETON_UNUSED;
static bool shutting_down SKELETON_UNUSED;

static sem_t coord_sem SKELETON_UNUSED;
static sem_t tutor_sem SKELETON_UNUSED;

static student_t *students SKELETON_UNUSED;
static int *tutor_ids SKELETON_UNUSED;

static int g_num_students SKELETON_UNUSED;
static int g_num_tutors SKELETON_UNUSED;
static int g_num_chairs SKELETON_UNUSED;
static int g_num_help SKELETON_UNUSED;

/* arrival fifo and priority queue, both protected by lock */
static request_t *arrival_buf SKELETON_UNUSED;
static int arrival_head SKELETON_UNUSED;
static int arrival_count SKELETON_UNUSED;
static int arrival_cap SKELETON_UNUSED;

static request_t *pq_buf SKELETON_UNUSED;
static int pq_count SKELETON_UNUSED;
static int pq_cap SKELETON_UNUSED;

/* queue helpers: caller must hold lock */
static void arrival_push(request_t r) SKELETON_UNUSED;
static request_t arrival_pop(void) SKELETON_UNUSED;
static bool arrival_empty(void) SKELETON_UNUSED;
static void pq_insert(request_t r) SKELETON_UNUSED;
static request_t pq_remove_best(void) SKELETON_UNUSED;
static bool pq_empty(void) SKELETON_UNUSED;
static int queues_alloc(int cap) SKELETON_UNUSED;
static void queues_free(void) SKELETON_UNUSED;

static void *coordinator_thread(void *arg) SKELETON_UNUSED;
static void *tutor_thread(void *arg) SKELETON_UNUSED;

static void Sem_wait(sem_t *s) SKELETON_UNUSED;
static void sleep_us(long us) SKELETON_UNUSED;

static void *student_thread(void *arg) SKELETON_UNUSED;

/* Accepts decimal digits only, yielding an integer in [1, INT_MAX]. */
static int parse_positive_int(const char *text, int *result)
{
    if (text == NULL || *text == '\0') {
        return -1;
    }

    int value = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
        const int digit = (int)(*p - '0');
        if (value > (INT_MAX - digit) / 10) {
            return -1;
        }
        value = value * 10 + digit;
    }
    if (value == 0) {
        return -1;
    }
    *result = value;
    return 0;
}

static void arrival_push(request_t r)
{
    (void)r; /* TODO P2.1 */
}

static request_t arrival_pop(void)
{
    request_t r = {0}; /* TODO P2.1 */
    return r;
}

static bool arrival_empty(void)
{
    return true; /* TODO P2.1 */
}

static void pq_insert(request_t r)
{
    (void)r; /* TODO P2.2 */
}

static request_t pq_remove_best(void)
{
    request_t r = {0}; /* TODO P2.2 */
    return r;
}

static bool pq_empty(void)
{
    return true; /* TODO P2.2 */
}

static int queues_alloc(int cap)
{
    (void)cap; /* TODO P2.2 */
    return -1;
}

static void queues_free(void)
{
    /* TODO P2.2 */
}

static void *coordinator_thread(void *arg)
{
    (void)arg; /* TODO P2.3, P2.5 */
    return NULL;
}

static void *tutor_thread(void *arg)
{
    (void)arg; /* TODO P2.4, P2.5 */
    return NULL;
}

static void Sem_wait(sem_t *s)
{
    /* a signal can interrupt the wait; that isn't a real post, so wait again */
    while (sem_wait(s) != 0) {
        if (errno != EINTR) {
            perror("sem_wait");
            abort();
        }
    }
}

static void sleep_us(long us)
{
    struct timespec ts, rem;

    ts.tv_sec = us / 1000000;
    ts.tv_nsec = (us % 1000000) * 1000;
    while (nanosleep(&ts, &rem) != 0 && errno == EINTR) {
        ts = rem;
    }
}

static void *student_thread(void *arg)
{
    (void)arg; /* TODO P1.5 */
    return NULL;
}

/*
 * TODO: Implement the simulation.
 *
 * Students must design and implement:
 *  - student, coordinator, and tutor thread routines;
 *  - waiting-area and priority-queue data structures;
 *  - all mutex/semaphore synchronization and event logging;
 *  - worker-thread creation, shutdown, and joining.
 *
 * Return 0 ONLY after every worker thread has terminated and been joined.
 * Return nonzero if the simulation cannot complete successfully.
 *
 * The function name/organization is a starting point, not a required design.
 */
static int run_simulation(int num_students, int num_tutors,
                          int num_chairs, int num_help)
{
    (void)num_students;
    (void)num_tutors;
    (void)num_chairs;
    (void)num_help;

    fprintf(stderr, "Project 2 simulation is not yet implemented.\n");
    return -1;
}

int main(int argc, char *argv[])
{
    int num_students, num_tutors, num_chairs, num_help;

    if (argc != 5 ||
        parse_positive_int(argc > 1 ? argv[1] : NULL, &num_students) != 0 ||
        parse_positive_int(argc > 2 ? argv[2] : NULL, &num_tutors) != 0 ||
        parse_positive_int(argc > 3 ? argv[3] : NULL, &num_chairs) != 0 ||
        parse_positive_int(argc > 4 ? argv[4] : NULL, &num_help) != 0) {
        fprintf(stderr,
                "Usage: %s <students> <tutors> <chairs> <help>\n"
                "All four arguments must be positive integers.\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    if (init_log(num_students, num_help) != 0) {
        perror("init_log");
        return EXIT_FAILURE;
    }

    /* TODO: Replace run_simulation() with your thread implementation. */
    if (run_simulation(num_students, num_tutors, num_chairs, num_help) != 0) {
        destroy_log();
        return EXIT_FAILURE;
    }

    /* Do not call until student, tutor, and coordinator threads are joined. */
    print_log();
    destroy_log();
    return EXIT_SUCCESS;
}
