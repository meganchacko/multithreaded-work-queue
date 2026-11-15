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

static pthread_mutex_t lock;

/* protected by lock */
static int free_chairs;
static unsigned long next_seq;
static bool shutting_down;

static sem_t coord_sem;
static sem_t tutor_sem;

static student_t *students;
static int *tutor_ids;

static int g_num_students;
static int g_num_tutors;
static int g_num_chairs;
static int g_num_help;

/* arrival fifo and priority queue, both protected by lock */
static request_t *arrival_buf SKELETON_UNUSED;
static int arrival_head SKELETON_UNUSED;
static int arrival_count SKELETON_UNUSED;
static int arrival_cap SKELETON_UNUSED;

static request_t *pq_buf SKELETON_UNUSED;
static int pq_count SKELETON_UNUSED;
static int pq_cap SKELETON_UNUSED;

/* queue helpers: caller must hold lock */
static void arrival_push(request_t r);
static request_t arrival_pop(void) SKELETON_UNUSED;
static bool arrival_empty(void) SKELETON_UNUSED;
static void pq_insert(request_t r) SKELETON_UNUSED;
static request_t pq_remove_best(void) SKELETON_UNUSED;
static bool pq_empty(void) SKELETON_UNUSED;
static int queues_alloc(int cap);
static void queues_free(void);

static void *coordinator_thread(void *arg);
static void *tutor_thread(void *arg);

static void Sem_wait(sem_t *s);
static void sleep_us(long us);

static void *student_thread(void *arg);

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
    student_t *s = arg;

    while (s->visits_done < g_num_help) {
        int visit = s->visits_done + 1;

        /* programming: a whole number of ms in [2, 10] */
        sleep_us((2 + rand_r(&s->seed) % 9) * 1000L);

        Pthread_mutex_lock(&lock);
        if (free_chairs == 0) {
            log_event(LOG_NO_CHAIR, s->id, -1, visit);
            Pthread_mutex_unlock(&lock);
            continue;
        }
        /* taking the chair and joining the fifo happen in one critical
         * section, so fifo order is exactly the order chairs were taken */
        free_chairs--;
        request_t r = {s->id, visit, next_seq++};
        log_event(LOG_SEATED, s->id, -1, visit);
        arrival_push(r);
        Pthread_mutex_unlock(&lock);
        sem_post(&coord_sem);

        /* the tutor frees our chair when it picks us, so we don't touch
         * free_chairs here; assigned_tutor is set before wake is posted */
        Sem_wait(&s->wake);
        int tutor = s->assigned_tutor;

        sleep_us(200);
        log_event(LOG_STUDENT_DONE, s->id, tutor, visit);
        s->visits_done++;
    }
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
    pthread_t *sthreads = NULL, *tthreads = NULL;
    int nwake = 0;
    int ret = -1;

    g_num_students = num_students;
    g_num_tutors = num_tutors;
    g_num_chairs = num_chairs;
    g_num_help = num_help;

    students = calloc(num_students, sizeof(*students));
    tutor_ids = calloc(num_tutors, sizeof(*tutor_ids));
    sthreads = calloc(num_students, sizeof(*sthreads));
    tthreads = calloc(num_tutors, sizeof(*tthreads));
    if (!students || !tutor_ids || !sthreads || !tthreads) {
        perror("calloc");
        goto free_arrays;
    }
    if (queues_alloc(num_chairs) != 0) {
        fprintf(stderr, "queues_alloc failed\n");
        goto free_arrays;
    }

    Pthread_mutex_init(&lock, NULL);
    if (sem_init(&coord_sem, 0, 0) != 0) {
        perror("sem_init");
        goto destroy_lock;
    }
    if (sem_init(&tutor_sem, 0, 0) != 0) {
        perror("sem_init");
        goto destroy_coord;
    }

    unsigned int now = (unsigned int)time(NULL);

    /* nwake counts the wake semaphores created, so we only destroy those */
    for (nwake = 0; nwake < num_students; nwake++) {
        students[nwake].id = nwake + 1;
        students[nwake].visits_done = 0;
        students[nwake].seed = now ^ ((unsigned int)(nwake + 1) * 2654435761u);
        if (sem_init(&students[nwake].wake, 0, 0) != 0) {
            perror("sem_init");
            goto destroy_wakes;
        }
    }
    for (int i = 0; i < num_tutors; i++) {
        tutor_ids[i] = i + 1;
    }

    free_chairs = num_chairs;
    next_seq = 0;
    shutting_down = false;

    pthread_t coord;
    int nt = 0, ns = 0;

    if (pthread_create(&coord, NULL, coordinator_thread, NULL) != 0) {
        fprintf(stderr, "pthread_create failed for coordinator\n");
        goto destroy_wakes;
    }
    for (nt = 0; nt < num_tutors; nt++) {
        if (pthread_create(&tthreads[nt], NULL, tutor_thread, &tutor_ids[nt]) != 0) {
            fprintf(stderr, "pthread_create failed for tutor %d\n", nt + 1);
            break;
        }
    }
    /* only start students once every tutor is running */
    if (nt == num_tutors) {
        for (ns = 0; ns < num_students; ns++) {
            if (pthread_create(&sthreads[ns], NULL, student_thread, &students[ns]) != 0) {
                fprintf(stderr, "pthread_create failed for student %d\n", ns + 1);
                break;
            }
        }
    }
    if (nt == num_tutors && ns == num_students) {
        ret = 0;
    }

    /* if a tutor or student failed to start, this still shuts down the ones that did */
    for (int i = 0; i < ns; i++) {
        Pthread_join(sthreads[i], NULL);
    }
    /* students are done, so both queues are empty; one post lets the
     * coordinator see shutting_down, and it then wakes every tutor */
    Pthread_mutex_lock(&lock);
    shutting_down = true;
    Pthread_mutex_unlock(&lock);
    sem_post(&coord_sem);
    Pthread_join(coord, NULL);
    for (int i = 0; i < nt; i++) {
        Pthread_join(tthreads[i], NULL);
    }

destroy_wakes:
    for (int i = 0; i < nwake; i++) {
        sem_destroy(&students[i].wake);
    }
    sem_destroy(&tutor_sem);
destroy_coord:
    sem_destroy(&coord_sem);
destroy_lock:
    pthread_mutex_destroy(&lock);
    queues_free();
free_arrays:
    free(students);
    free(tutor_ids);
    free(sthreads);
    free(tthreads);
    return ret;
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
