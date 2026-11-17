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

typedef struct 
{
    int student_id;
    int visit;
    unsigned long seq;
} request_t;

typedef struct 
{
    int id;
    int visits_done;
    int assigned_tutor;
    sem_t wake;
    unsigned int seed;
} student_t;

static pthread_mutex_t lock;

// protected by lock
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

// arrival fifo and priority queue, both protected by lock
static request_t *arrival_buf;
static int arrival_head;
static int arrival_count;

static request_t *pq_buf;
static int pq_count;

static void *coordinator_thread(void *arg);
static void *tutor_thread(void *arg);

static void Sem_wait(sem_t *s);
static void sleep_us(long us);

static void *student_thread(void *arg);

// accepts decimal digits only, yielding an integer in [1, INT_MAX]. 
static int parse_positive_int(const char *text, int *result)
{
    if (text == NULL || *text == '\0') 
    {
        return -1;
    }

    int value = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) 
    {
        if (*p < '0' || *p > '9') {
            return -1;
        }
        const int digit = (int)(*p - '0');
        if (value > (INT_MAX - digit) / 10) {
            return -1;
        }
        value = value * 10 + digit;
    }


    if (value == 0) 
    {
        return -1;
    }
    *result = value;
    return 0;
}


static void *coordinator_thread(void *arg)
{
    (void)arg;

    while (true) {
        Sem_wait(&coord_sem);

        Pthread_mutex_lock(&lock);

        // every student is joined before the shutdown post, so an empty
        // fifo here means we're done; wake every tutor so they can exit
        if (shutting_down && arrival_count == 0) {
            Pthread_mutex_unlock(&lock);

            for (int i = 0; i < g_num_tutors; i++) {
                sem_post(&tutor_sem);
            }

            break;
        }

        if (arrival_count > 0) {
            request_t r = arrival_buf[arrival_head];
            arrival_head = (arrival_head + 1) % g_num_chairs;
            arrival_count--;

            pq_buf[pq_count] = r;
            pq_count++;

            log_event(LOG_QUEUED, r.student_id, -1, r.visit);

            Pthread_mutex_unlock(&lock);

            sem_post(&tutor_sem);
        } else {
            Pthread_mutex_unlock(&lock);
        }
    }

    return NULL;
}

static void *tutor_thread(void *arg)
{
    int tutor_id = *(int *)arg;

    while (true) {
        Sem_wait(&tutor_sem);

        Pthread_mutex_lock(&lock);

        if (shutting_down && pq_count == 0) {
            Pthread_mutex_unlock(&lock);
            break;
        }

        if (pq_count == 0) {
            Pthread_mutex_unlock(&lock);
            continue;
        }

        // lowest visit number wins; ties go to whoever sat down first
        int best = 0;
        for (int i = 1; i < pq_count; i++) {
            if (pq_buf[i].visit < pq_buf[best].visit ||
                (pq_buf[i].visit == pq_buf[best].visit &&
                 pq_buf[i].seq < pq_buf[best].seq)) {
                best = i;
            }
        }

        request_t r = pq_buf[best];
        for (int i = best; i < pq_count - 1; i++) {
            pq_buf[i] = pq_buf[i + 1];
        }
        pq_count--;

        // free the chair as soon as we pick the student, not after
        // tutoring, so another student can sit down in the meantime
        free_chairs++;

        students[r.student_id - 1].assigned_tutor = tutor_id;

        // log ASSIGNED under the lock and before posting wake, so it
        // always shows up in the log before the student's STUDENT_DONE
        log_event(LOG_ASSIGNED, r.student_id, tutor_id, r.visit);

        Pthread_mutex_unlock(&lock);

        sem_post(&students[r.student_id - 1].wake);

        sleep_us(200);

        log_event(LOG_TUTOR_DONE, r.student_id, tutor_id, r.visit);
    }

    return NULL;
}

static void Sem_wait(sem_t *s)
{
    // a signal can interrupt the wait; that isn't a real post, so wait again
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

    while (s->visits_done < g_num_help) 
    {
        int visit = s->visits_done + 1;

        // programming: a whole number of ms in [2, 10]
        sleep_us((2 + rand_r(&s->seed) % 9) * 1000L);

        Pthread_mutex_lock(&lock);
        if (free_chairs == 0) 
        {
            log_event(LOG_NO_CHAIR, s->id, -1, visit);
            Pthread_mutex_unlock(&lock);
            continue;
        }
        // taking the chair and joining the fifo happen in one critical
        // section, so fifo order is exactly the order chairs were taken
        free_chairs--;
        request_t r = {s->id, visit, next_seq++};
        log_event(LOG_SEATED, s->id, -1, visit);
        arrival_buf[(arrival_head + arrival_count) % g_num_chairs] = r;
        arrival_count++;
        Pthread_mutex_unlock(&lock);
        sem_post(&coord_sem);

        // the tutor frees our chair when it picks us, so we don't touch
        // free_chairs here; assigned_tutor is set before wake is posted

        Sem_wait(&s->wake);
        int tutor = s->assigned_tutor;

        sleep_us(200);
        log_event(LOG_STUDENT_DONE, s->id, tutor, visit);
        s->visits_done++;
    }
    return NULL;
}

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
    arrival_buf = calloc(num_chairs, sizeof(*arrival_buf));
    pq_buf = calloc(num_chairs, sizeof(*pq_buf));
    if (!students || !tutor_ids || !sthreads || !tthreads || !arrival_buf || !pq_buf)
    {
        perror("calloc");
        goto free_arrays;
    }

    Pthread_mutex_init(&lock, NULL);
    if (sem_init(&coord_sem, 0, 0) != 0) 
    {
        perror("sem_init");
        goto destroy_lock;
    }

    if (sem_init(&tutor_sem, 0, 0) != 0) 
    {
        perror("sem_init");
        goto destroy_coord;
    }

    unsigned int now = (unsigned int)time(NULL);

    // nwake counts the wake semaphores created, so we only destroy those


    for (nwake = 0; nwake < num_students; nwake++) 
    {
        students[nwake].id = nwake + 1;
        students[nwake].visits_done = 0;
        students[nwake].seed = now ^ ((unsigned int)(nwake + 1) * 2654435761u);
        if (sem_init(&students[nwake].wake, 0, 0) != 0) 
        {
            perror("sem_init");
            goto destroy_wakes;
        }
    }

    for (int i = 0; i < num_tutors; i++) 
    {
        tutor_ids[i] = i + 1;
    }

    free_chairs = num_chairs;
    next_seq = 0;
    shutting_down = false;
    arrival_head = 0;
    arrival_count = 0;
    pq_count = 0;

    pthread_t coord;
    int nt = 0, ns = 0;

    if (pthread_create(&coord, NULL, coordinator_thread, NULL) != 0) 
    {
        fprintf(stderr, "pthread_create failed for coordinator\n");
        goto destroy_wakes;
    }

    for (nt = 0; nt < num_tutors; nt++) {
        if (pthread_create(&tthreads[nt], NULL, tutor_thread, &tutor_ids[nt]) != 0) 
        {
            fprintf(stderr, "pthread_create failed for tutor %d\n", nt + 1);
            break;
        }
    }
    // only start students once every tutor is running
    if (nt == num_tutors) 
    {
        for (ns = 0; ns < num_students; ns++) 
        {
            if (pthread_create(&sthreads[ns], NULL, student_thread, &students[ns]) != 0) 
            {
                fprintf(stderr, "pthread_create failed for student %d\n", ns + 1);

                break;
            }
        }
    }
    if (nt == num_tutors && ns == num_students) 
    {
        ret = 0;
    }

    // if a tutor or student failed to start, this still shuts down the ones that did
    for (int i = 0; i < ns; i++) 
    {
        Pthread_join(sthreads[i], NULL);
    }
    // students are done, so both queues are empty; one post lets the
    // coordinator see shutting_down, and it then wakes every tutor
    Pthread_mutex_lock(&lock);
    shutting_down = true;
    Pthread_mutex_unlock(&lock);
    sem_post(&coord_sem);
    Pthread_join(coord, NULL);

    for (int i = 0; i < nt; i++) 
    {
        Pthread_join(tthreads[i], NULL);
    }

    // a failed destroy makes the run fail, but we keep cleaning up the rest
destroy_wakes:
    for (int i = 0; i < nwake; i++) 
    {
        if (sem_destroy(&students[i].wake) != 0) {
            perror("sem_destroy");
            ret = -1;
        }
    }
    if (sem_destroy(&tutor_sem) != 0) 
    {
        perror("sem_destroy");
        ret = -1;
    }
destroy_coord:
    if (sem_destroy(&coord_sem) != 0) 
    {
        perror("sem_destroy");
        ret = -1;
    }
destroy_lock:
    if (pthread_mutex_destroy(&lock) != 0) 
    {
        fprintf(stderr, "pthread_mutex_destroy failed\n");
        ret = -1;

    }

free_arrays:
    free(students);
    free(tutor_ids);
    free(sthreads);
    free(tthreads);
    free(arrival_buf);
    free(pq_buf);
    return ret;
}

int main(int argc, char *argv[])
{
    int num_students, num_tutors, num_chairs, num_help;

    if (argc != 5 ||
        parse_positive_int(argc > 1 ? argv[1] : NULL, &num_students) != 0 ||
        parse_positive_int(argc > 2 ? argv[2] : NULL, &num_tutors) != 0 ||
        parse_positive_int(argc > 3 ? argv[3] : NULL, &num_chairs) != 0 ||
        parse_positive_int(argc > 4 ? argv[4] : NULL, &num_help) != 0) 
        {
        fprintf(stderr,
                "Usage: %s <students> <tutors> <chairs> <help>\n"
                "All four arguments must be positive integers.\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    if (init_log(num_students, num_help) != 0) 
    {
        perror("init_log");
        return EXIT_FAILURE;
    }

    if (run_simulation(num_students, num_tutors, num_chairs, num_help) != 0) 
    {
        destroy_log();
        return EXIT_FAILURE;
    }

    print_log();
    destroy_log();
    return EXIT_SUCCESS;
}
