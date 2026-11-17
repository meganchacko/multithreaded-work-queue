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
    int visit_num;
    unsigned long sequence_order;
} request_t;

typedef struct 
{
    int id;
    int visits_done;
    int tutor;
    sem_t wake;
    unsigned int random_state;
} student_t;

static pthread_mutex_t lock;

// protected by lock
static int empty_chairs;
static unsigned long next_sequence;
static bool shut_down;

static sem_t coord_sem;
static sem_t tutor_sem;

static student_t *students;
static int *tutor_ids;

static int num_tutors_copy;
static int num_chairs_copy;
static int num_help_copy;

// arrival fifo and priority queue, both protected by lock
static request_t *arrival_queue;
static int arrival_head;
static int arrival_size;

static request_t *priority_queue;
static int priority_queue_size;

static void *coordinator_thread(void *arg);
static void *tutor_thread(void *arg);

static void Sem_wait(sem_t *s);
static void sleep_micro(long microseconds);

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
        if (shut_down && arrival_size == 0) {
            Pthread_mutex_unlock(&lock);

            for (int i = 0; i < num_tutors_copy; i++) {
                sem_post(&tutor_sem);
            }

            break;
        }

        if (arrival_size > 0) {
            request_t r = arrival_queue[arrival_head];
            arrival_head = (arrival_head + 1) % num_chairs_copy;
            arrival_size--;

            priority_queue[priority_queue_size] = r;
            priority_queue_size++;

            log_event(LOG_QUEUED, r.student_id, -1, r.visit_num);

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

        if (shut_down && priority_queue_size == 0) {
            Pthread_mutex_unlock(&lock);
            break;
        }

        if (priority_queue_size == 0) {
            Pthread_mutex_unlock(&lock);
            continue;
        }

        // lowest visit number wins; ties go to whoever sat down first
        int best = 0;
        for (int i = 1; i < priority_queue_size; i++) {
            if (priority_queue[i].visit_num < priority_queue[best].visit_num ||
                (priority_queue[i].visit_num == priority_queue[best].visit_num &&
                 priority_queue[i].sequence_order < priority_queue[best].sequence_order)) {
                best = i;
            }
        }

        request_t r = priority_queue[best];
        for (int i = best; i < priority_queue_size - 1; i++) {
            priority_queue[i] = priority_queue[i + 1];
        }
        priority_queue_size--;

        // free the chair as soon as we pick the student, not after
        // tutoring, so another student can sit down in the meantime
        empty_chairs++;

        students[r.student_id - 1].tutor = tutor_id;

        // log ASSIGNED under the lock and before posting wake, so it
        // always shows up in the log before the student's STUDENT_DONE
        log_event(LOG_ASSIGNED, r.student_id, tutor_id, r.visit_num);

        Pthread_mutex_unlock(&lock);

        sem_post(&students[r.student_id - 1].wake);

        sleep_micro(200);

        log_event(LOG_TUTOR_DONE, r.student_id, tutor_id, r.visit_num);
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

static void sleep_micro(long microseconds)
{
    struct timespec sleeptime, remaining_time;

    sleeptime.tv_sec = microseconds / 1000000;
    sleeptime.tv_nsec = (microseconds % 1000000) * 1000;
    while (nanosleep(&sleeptime, &remaining_time) != 0 && errno == EINTR) {
        sleeptime = remaining_time;
    }
}

static void *student_thread(void *arg)
{
    student_t *s = arg;

    while (s->visits_done < num_help_copy) 
    {
        int visit_num = s->visits_done + 1;

        // programming: a whole number of ms in [2, 10]
        sleep_micro((2 + rand_r(&s->random_state) % 9) * 1000L);

        Pthread_mutex_lock(&lock);
        if (empty_chairs == 0) 
        {
            log_event(LOG_NO_CHAIR, s->id, -1, visit_num);
            Pthread_mutex_unlock(&lock);
            continue;
        }
        // taking the chair and joining the fifo happen in one critical
        // section, so fifo order is exactly the order chairs were taken
        empty_chairs--;
        request_t r = {s->id, visit_num, next_sequence++};
        log_event(LOG_SEATED, s->id, -1, visit_num);
        arrival_queue[(arrival_head + arrival_size) % num_chairs_copy] = r;
        arrival_size++;
        Pthread_mutex_unlock(&lock);
        sem_post(&coord_sem);

        // the tutor frees our chair when it picks us, so we don't touch
        // empty_chairs here; s->tutor is set before wake is posted

        Sem_wait(&s->wake);
        int tutor = s->tutor;

        sleep_micro(200);
        log_event(LOG_STUDENT_DONE, s->id, tutor, visit_num);
        s->visits_done++;
    }
    return NULL;
}

static int run_simulation(int num_students, int num_tutors,
                          int num_chairs, int num_help)
{
    pthread_t *student_threads = NULL, *tutor_threads = NULL;
    int wakes_created = 0;
    int result = -1;

    num_tutors_copy = num_tutors;
    num_chairs_copy = num_chairs;
    num_help_copy = num_help;

    students = calloc(num_students, sizeof(*students));
    tutor_ids = calloc(num_tutors, sizeof(*tutor_ids));
    student_threads = calloc(num_students, sizeof(*student_threads));
    tutor_threads = calloc(num_tutors, sizeof(*tutor_threads));
    arrival_queue = calloc(num_chairs, sizeof(*arrival_queue));
    priority_queue = calloc(num_chairs, sizeof(*priority_queue));
    if (!students || !tutor_ids || !student_threads || !tutor_threads || !arrival_queue || !priority_queue)
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

    unsigned int start_time = (unsigned int)time(NULL);

    // wakes_created counts the wake semaphores made so far, so we only destroy those


    for (wakes_created = 0; wakes_created < num_students; wakes_created++) 
    {
        students[wakes_created].id = wakes_created + 1;
        students[wakes_created].visits_done = 0;
        students[wakes_created].random_state = start_time ^ ((unsigned int)(wakes_created + 1) * 2654435761u);
        if (sem_init(&students[wakes_created].wake, 0, 0) != 0) 
        {
            perror("sem_init");
            goto destroy_wakes;
        }
    }

    for (int i = 0; i < num_tutors; i++) 
    {
        tutor_ids[i] = i + 1;
    }

    empty_chairs = num_chairs;
    next_sequence = 0;
    shut_down = false;
    arrival_head = 0;
    arrival_size = 0;
    priority_queue_size = 0;

    pthread_t coord;
    int tutors_started = 0, students_started = 0;

    if (pthread_create(&coord, NULL, coordinator_thread, NULL) != 0) 
    {
        fprintf(stderr, "pthread_create failed for coordinator\n");
        goto destroy_wakes;
    }

    for (tutors_started = 0; tutors_started < num_tutors; tutors_started++) {
        if (pthread_create(&tutor_threads[tutors_started], NULL, tutor_thread, &tutor_ids[tutors_started]) != 0) 
        {
            fprintf(stderr, "pthread_create failed for tutor %d\n", tutors_started + 1);
            break;
        }
    }
    // only start students once every tutor is running
    if (tutors_started == num_tutors) 
    {
        for (students_started = 0; students_started < num_students; students_started++) 
        {
            if (pthread_create(&student_threads[students_started], NULL, student_thread, &students[students_started]) != 0) 
            {
                fprintf(stderr, "pthread_create failed for student %d\n", students_started + 1);

                break;
            }
        }
    }
    if (tutors_started == num_tutors && students_started == num_students) 
    {
        result = 0;
    }

    // if a tutor or student failed to start, this still shuts down the ones that did
    for (int i = 0; i < students_started; i++) 
    {
        Pthread_join(student_threads[i], NULL);
    }
    // students are done, so both queues are empty; one post lets the
    // coordinator see shut_down, and it then wakes every tutor
    Pthread_mutex_lock(&lock);
    shut_down = true;
    Pthread_mutex_unlock(&lock);
    sem_post(&coord_sem);
    Pthread_join(coord, NULL);

    for (int i = 0; i < tutors_started; i++) 
    {
        Pthread_join(tutor_threads[i], NULL);
    }

    // a failed destroy makes the run fail, but we keep cleaning up the rest
destroy_wakes:
    for (int i = 0; i < wakes_created; i++) 
    {
        if (sem_destroy(&students[i].wake) != 0) {
            perror("sem_destroy");
            result = -1;
        }
    }
    if (sem_destroy(&tutor_sem) != 0) 
    {
        perror("sem_destroy");
        result = -1;
    }
destroy_coord:
    if (sem_destroy(&coord_sem) != 0) 
    {
        perror("sem_destroy");
        result = -1;
    }
destroy_lock:
    if (pthread_mutex_destroy(&lock) != 0) 
    {
        fprintf(stderr, "pthread_mutex_destroy failed\n");
        result = -1;

    }

free_arrays:
    free(students);
    free(tutor_ids);
    free(student_threads);
    free(tutor_threads);
    free(arrival_queue);
    free(priority_queue);
    return result;
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
