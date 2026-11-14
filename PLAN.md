# CS4348 Project 2: CSMC (Seeking Tutor Problem) Plan

## How to use this file (for Claude Code)

This is **reference context only**. Do not write or change implementation code unless the user explicitly asks for a specific step. When asked, follow the locked design in section 4 and the interface contract in section 5 exactly.

**Precedence rule:** the professor's instructions (`reference/assignment_spec.txt`) always win. The starter files (`csmc.c`, `common_threads.h`, `README.md`) are a reference and starting point, not requirements. If a starter file conflicts with the instructions or makes them impossible to follow, follow the instructions. If this plan conflicts with the instructions, the instructions win, and you should flag the conflict instead of guessing. Section 12 lists the known discrepancies and how we resolved them.

Reference files in `reference/`:

| File | What it is |
| --- | --- |
| `assignment_spec.txt` | Full assignment text from the professor. **Source of truth.** |
| `starter_csmc.c.txt` | Starter `csmc.c` (`main()`, arg parsing, stub `run_simulation()`) |
| `common_threads.h.txt` | Supplied pthread helper wrappers |
| `logger.h.txt` | Supplied logger API (do not modify `logger.h` / `logger.c`) |
| `starter_README.md.txt` | Starter README (compile command, event table, logging rules) |

`logger.c` was not copied into `reference/`. Treat it as a black box that behaves as `logger.h` and the README describe.

Partners: Person 1 = Megan (msc220005). Person 2 = partner.

---

## 1. What we're building

The program is `./csmc <students> <tutors> <chairs> <help>`. All four arguments must be positive integers, and the starter `main()` already validates them.

The program runs these threads:

- one thread per student
- one thread per tutor
- exactly one coordinator thread

Each student does this cycle:

1. Program (sleep).
2. Try to take a chair.
3. If a chair is free, wait to be picked by a tutor, then get tutored.
4. Repeat until it has `help` successful visits.

The coordinator moves seated requests into a priority queue in seating order. Tutors pick the highest-priority request from that queue, free the chair, wake that specific student, and simulate tutoring.

All implementation goes in **`csmc.c`**. It is the only code file we submit, along with `report.txt`.

---

## 2. Hard requirements (from the professor; don't violate any)

### Admission and priority

- A student takes a chair only if one is free. If none is free, it logs `NO_CHAIR`, goes back to programming, and retries later. A failed attempt does **not** count as a visit.
- The coordinator must process accepted arrivals **in the order students successfully took chairs**.
- Priority = the visit number of the request: visit 1 → priority 1 (highest), visit 2 → priority 2, and so on.
- Same priority → **FIFO by arrival** (earlier seated first).
- Only requests the coordinator has **already inserted into the priority queue** are eligible for tutors. A new arrival does not invalidate a selection a tutor already made.

### Assignment and tutoring

- An available tutor selects the highest-priority request and notifies **that specific student**.
- The chair is freed **at selection time**, even if the student thread hasn't resumed yet. It is not held during tutoring.
- Student and tutor **each independently** sleep 0.2 ms to simulate tutoring. There is **no completion handshake**, so `STUDENT_DONE` and `TUTOR_DONE` may appear in either order.
- After its tutoring sleep, a student programs again before its next attempt.

### Timing

- Before **each** help attempt, the student sleeps a random **2–10 ms** ("programming").
- Tutoring sleep is **0.2 ms** (200 µs) on both sides.
- Use **thread-safe RNG**, meaning `rand_r()` with a per-thread seed, not `rand()`.
- **Sleeps must never stand in for synchronization.** The program must be correct under any scheduling.

### Termination and cleanup

- A student thread exits after `help` successful visits.
- After all students finish, the coordinator and all tutors must exit cleanly.
- `run_simulation()` must return 0 **only after every worker thread has terminated and been joined**. That includes a tutor still finishing its last tutoring sleep.
- Free all dynamic memory, and destroy all semaphores and the mutex.
- Return nonzero from `run_simulation()` if the simulation can't complete. `main` then calls `destroy_log()` and exits with failure, without calling `print_log()`.

### Memory and environment

- Allocate all input-dependent structures dynamically.
- Handle `malloc`/`calloc` failure and `pthread_create()` failure. See open question Q1 in section 11.
- Use the pthread helpers in `common_threads.h`.
- Develop and test on **csgrads1**. Use **cs3** for any run with more than 200 threads.

### Logging

- Use only the supplied logger for required events. Do not use `printf` for them.
- Student and tutor IDs are stable and distinct within their group. We use 1..N.
- Pass `-1` as `tutor_id` for `SEATED`, `QUEUED`, and `NO_CHAIR`.
- For shared-state transitions, call `log_event()` **while holding the lock** that protects that state, at the moment the transition happens.
- `ASSIGNED` must be logged **inside the same critical section** as the queue removal and chair release, and **before** the tutor wakes the student.
- The buffer holds exactly 5 events per expected session (`students × help × 5`). `log_event()` **kills the program if this is exceeded**, so any duplicate or extra event is fatal.

### Build (must be zero warnings)

```
gcc csmc.c logger.c -o csmc -Wall -Wextra -Werror -pthread -std=gnu11
```

This is the **only** build command we target. It comes from the instructions. The starter README's `-std=c11` command is ignored (see section 12).

---

## 3. Facts about the starter code

### `csmc.c`

- It includes only `logger.h`, `limits.h`, `stdio.h`, and `stdlib.h`. It does **not** include `common_threads.h`, so we must add that include.
- Its stub is `static int run_simulation(int num_students, int num_tutors, int num_chairs, int num_help)`. The comment says the name and organization are a starting point, not a required design. Keep the signature so `main()` stays unchanged.
- `main()` calls `init_log()` **before** `run_simulation()`, and `print_log()`/`destroy_log()` after it. Don't call any of these ourselves.

### `common_threads.h`

It provides **only**:

- `Pthread_create`
- `Pthread_join`
- `Pthread_mutex_lock`
- `Pthread_mutex_unlock`
- `Pthread_mutex_init`

Each one `assert`s that its return code is 0. That means a failure **aborts** the program rather than returning an error. Because of this, we create threads with raw `pthread_create()` plus an error check, so we can handle failures as the instructions require. We use the helpers for everything else (see 4.8 and section 12).

It has **no semaphore wrappers and no mutex-destroy wrapper**. We write our own small checked wrappers in `csmc.c` for `sem_init`, `sem_wait` (retry on `EINTR`), `sem_post`, and `sem_destroy`.

Its functions are non-static definitions in a header. That's fine because only `csmc.c` includes it.

### `logger.h`

- Enum values: `LOG_SEATED`, `LOG_QUEUED`, `LOG_ASSIGNED`, `LOG_STUDENT_DONE`, `LOG_TUTOR_DONE`, `LOG_NO_CHAIR`.
- Signature: `void log_event(EventType type, int student_id, int tutor_id, int visit);`
- For `NO_CHAIR`, the README passes the visit number the student was attempting: `log_event(LOG_NO_CHAIR, student, -1, visit)`.

---

## 4. Locked synchronization design

### 4.1 Shared state

| Name | Type | Owner | Protected by |
| --- | --- | --- | --- |
| `lock` | `pthread_mutex_t` | P1 creates | n/a (the one global app mutex) |
| `free_chairs` | `int` (init `num_chairs`) | P1 | `lock` |
| `arrival_fifo` | circular buffer of `request_t`, capacity `num_chairs` | P2 | `lock` |
| `pq` | priority queue of `request_t`, capacity `num_chairs` | P2 | `lock` |
| `next_seq` | `unsigned long`, arrival sequence counter | P1 increments at seat time | `lock` |
| `shutting_down` | `bool` | P1 sets | `lock` |
| `coord_sem` | `sem_t` (init 0) | P1 creates, P2 waits | n/a |
| `tutor_sem` | `sem_t` (init 0) | P1 creates, P2 posts and waits | n/a |
| `students[]` | array of `student_t`, length `num_students` | P1 | see below |

The request type:

```
request_t   { int student_id; int visit; unsigned long seq; }
student_t   { int id; int visits_done; int assigned_tutor;
              sem_t wake; unsigned int seed; }
```

How each `student_t` field is synchronized:

- `visits_done` and `seed` are touched only by that student's own thread, so they need no lock.
- `assigned_tutor` is written by the tutor **under `lock`** and **before** `sem_post(&wake)`. The student reads it only **after** `sem_wait(&wake)` returns, and the semaphore provides the happens-before ordering.
- `wake` is a private semaphore initialized to 0. Only the tutor that selected this student posts it.

Tutor IDs come from a dynamically allocated `int tutor_ids[]` holding 1..T, passed as each tutor's thread argument.

### 4.2 Invariants (check these when debugging)

1. `0 <= free_chairs <= num_chairs`.
2. `(# items in arrival_fifo) + (# items in pq) == num_chairs - free_chairs`. Every occupied chair is exactly one request, and it is in exactly one of the two queues. This is why both buffers can have capacity `num_chairs` and can never overflow.
3. `coord_sem`'s count is at most the number of items in `arrival_fifo`, plus 1 after shutdown is signaled.
4. Each `tutor_sem` post corresponds to exactly one item inserted into `pq`, plus `num_tutors` extra posts at shutdown. So a tutor that wakes before shutdown always finds `pq` non-empty.
5. A student has at most one outstanding request at a time.

### 4.3 Semaphore meanings

| Semaphore | Posted by | Waited on by | One post means |
| --- | --- | --- | --- |
| `coord_sem` | student (after seating); `run_simulation` (once, for shutdown) | coordinator | "one new arrival is in `arrival_fifo`" or "shutdown" |
| `tutor_sem` | coordinator (after each `QUEUED`; `num_tutors` times at shutdown) | tutors | "one request is in `pq`" or "shutdown" |
| `students[i].wake` | the tutor that selected student i | student i only | "you've been selected; `assigned_tutor` is set" |

Counting semaphores prevent **lost notifications**: a post that happens before the waiter waits is remembered. Removal under `lock` prevents **duplicate assignment**: only one tutor can remove a given request.

### 4.4 Locking rules

- There is **one mutex** for all shared queue and chair state. It's simple and easy to explain in the demo.
- **Never sleep while holding `lock`.** That covers the programming and tutoring sleeps.
- **Never `sem_wait` while holding `lock`**, because it can deadlock.
- `sem_post` can happen after unlocking. We post after unlock in every case.
- Log `SEATED`, `QUEUED`, and `ASSIGNED` inside the critical section.
- `NO_CHAIR` is logged inside the critical section where the failed chair check happens. It reflects a decision about shared state, so we log it under the lock too.
- `STUDENT_DONE` and `TUTOR_DONE` change no shared state, so log them right after the sleep without the lock.

### 4.5 Thread routines (pseudocode, not final code)

**Student (Person 1)**

```
s = arg (student_t*)
while s->visits_done < num_help:
    visit = s->visits_done + 1
    sleep random 2..10 ms using rand_r(&s->seed)      // programming
    lock
    if free_chairs == 0:
        log_event(LOG_NO_CHAIR, s->id, -1, visit)
        unlock
        continue                                      // back to programming
    free_chairs--
    seq = next_seq++
    log_event(LOG_SEATED, s->id, -1, visit)
    arrival_push({s->id, visit, seq})                 // P2's helper, caller holds lock
    unlock
    sem_post(&coord_sem)
    sem_wait(&s->wake)                                // chair already freed by tutor
    tutor = s->assigned_tutor
    sleep 0.2 ms
    log_event(LOG_STUDENT_DONE, s->id, tutor, visit)
    s->visits_done++
return NULL
```

**Coordinator (Person 2)**

```
loop:
    sem_wait(&coord_sem)
    lock
    if arrival_fifo empty:
        if shutting_down: unlock; break
        else: unlock; continue        // should not happen; treat as a bug while testing
    r = arrival_pop()                 // FIFO = seating order
    pq_insert(r)
    log_event(LOG_QUEUED, r.student_id, -1, r.visit)
    unlock
    sem_post(&tutor_sem)
after loop:
    for i in 1..num_tutors: sem_post(&tutor_sem)    // wake every tutor to exit
return NULL
```

**Tutor (Person 2)**

```
tid = *(int*)arg
loop:
    sem_wait(&tutor_sem)
    lock
    if pq empty:
        // only reachable on a shutdown post (invariant 4)
        unlock; break
    r = pq_remove_best()              // lowest visit, then lowest seq
    free_chairs++                     // chair released AT SELECTION
    students[r.student_id - 1].assigned_tutor = tid
    log_event(LOG_ASSIGNED, r.student_id, tid, r.visit)
    unlock
    sem_post(&students[r.student_id - 1].wake)
    sleep 0.2 ms
    log_event(LOG_TUTOR_DONE, r.student_id, tid, r.visit)
return NULL
```

### 4.6 Priority queue rule

`pq_remove_best()` returns the entry with the **smallest `visit`**. Ties go to the **smallest `seq`**.

Implementation choices:

- A linear scan over an array of capacity `num_chairs` is fine and easiest to explain. The stress test has at most 200 chairs × 20,000 operations.
- A binary heap keyed on `(visit, seq)` is also fine.

Because `seq` comes from seating order and the coordinator inserts in FIFO order, ties naturally resolve FIFO.

### 4.7 Shutdown sequence (main → coordinator → tutors)

1. `run_simulation()` joins **all** student threads.
2. At this point every request was seated, queued, and assigned, so both queues are empty and all chairs are free.
3. Lock, set `shutting_down = true`, unlock, then `sem_post(&coord_sem)` once.
4. The coordinator wakes, sees an empty FIFO and `shutting_down`, exits its loop, posts `tutor_sem` `num_tutors` times, and returns.
5. Each tutor wakes, sees an empty `pq`, and exits. A tutor still in its last 0.2 ms sleep finishes it, logs `TUTOR_DONE`, loops, consumes a shutdown post, and exits.
6. `run_simulation()` joins the coordinator, then all tutors, then cleans up and returns 0.

### 4.8 Failure handling in `run_simulation()`

- **Allocation failure** (before any thread exists): free whatever was allocated and return -1.
- **Creation order**: coordinator, then tutors, then students. That way any student that starts has a working pipeline.
- **Coordinator creation fails**: clean up and return -1.
- **Tutor k creation fails**: set `shutting_down` and post `coord_sem`. The coordinator posts `tutor_sem` `num_tutors` times, and extra posts are harmless. Join the coordinator and tutors 1..k-1, clean up, and return -1.
- **Student k creation fails**: stop creating students. Students 1..k-1 are running with a full pipeline, so they will finish. Join them, run the normal shutdown, clean up, and return -1. Their events stay within the buffer because we never exceed `students × help` sessions.
- **Thread creation API (decided):** call `pthread_create()` directly and check its return code. Do not use `Pthread_create`, because it asserts and aborts, which makes the required failure handling impossible. Use `Pthread_join`, `Pthread_mutex_init`, `Pthread_mutex_lock`, and `Pthread_mutex_unlock` from `common_threads.h` for everything else.

### 4.9 Design-question answers (for report and demo)

| Question | Answer |
| --- | --- |
| How does the coordinator learn of an arrival? | The student posts `coord_sem` after pushing to `arrival_fifo`. |
| How is arrival order preserved? | The push into `arrival_fifo` happens in the same critical section as the chair decrement and `SEATED`, so FIFO order = seating order. The coordinator pops in FIFO order. |
| How does a tutor know someone is waiting? | The coordinator posts `tutor_sem` once per `QUEUED` request. |
| How does the tutor wake its student? | It sets `assigned_tutor` under the lock, then posts that student's private `wake` semaphore. |
| What needs the mutex? | `free_chairs`, `arrival_fifo`, `pq`, `next_seq`, `shutting_down`, and the `assigned_tutor` write. |
| When does chair occupancy change? | `--` when the student is seated (`SEATED`), `++` when the tutor selects it (`ASSIGNED`). |
| How are lost notifications and duplicate assignments prevented? | Counting semaphores remember posts; removal from `pq` happens under the mutex. |
| How do the coordinator and tutors terminate? | The `shutting_down` flag plus extra semaphore posts. They exit only when woken and their queue is empty with the flag set. |

---

## 5. Interface contract between Person 1 and Person 2

Agree on this before splitting work. Person 1 commits these declarations as the shared skeleton.

**Globals.** These are `static` in `csmc.c`:

```
lock, free_chairs, next_seq, shutting_down,
coord_sem, tutor_sem, students, tutor_ids,
g_num_students, g_num_tutors, g_num_chairs, g_num_help,
arrival_fifo storage (buf, head, count, cap), pq storage (buf, count, cap)
```

**Types:** `request_t` and `student_t` as defined in 4.1.

**Person 2 provides.** All of these require the caller to hold `lock`:

- `static void arrival_push(request_t r);`
- `static request_t arrival_pop(void);`
- `static bool arrival_empty(void);`
- `static void pq_insert(request_t r);`
- `static request_t pq_remove_best(void);`
- `static bool pq_empty(void);`
- `static int queues_alloc(int cap);` returns 0 on success and -1 on allocation failure. It must not leak partial allocations.
- `static void queues_free(void);`
- `static void *coordinator_thread(void *arg);`
- `static void *tutor_thread(void *arg);` where `arg` is an `int*` tutor ID.

**Person 1 provides:**

- `static void *student_thread(void *arg);` where `arg` is a `student_t*`.
- Semaphore wrappers. These are capitalized to match the `common_threads.h` style and to avoid clashing with the libc names. Person 2 calls these too. Signatures and behavior (agreed):
  - `static int Sem_init(sem_t *s, unsigned int value);` returns 0 on success, -1 on failure. The caller handles the failure; it's only called during setup in `run_simulation()`.
  - `static void Sem_wait(sem_t *s);` retries on `EINTR`. On any other failure it calls `perror` and `abort()`, the same as the `common_threads.h` helpers.
  - `static void Sem_post(sem_t *s);` On failure it calls `perror` and `abort()`.
  - `static int Sem_destroy(sem_t *s);` returns 0 on success, -1 on failure.
  - **Cleanup rule:** if `Sem_destroy` or `pthread_mutex_destroy` fails during cleanup, keep cleaning up everything else, then have `run_simulation()` return nonzero.
- Sleep helpers: `sleep_us(long us)` using `nanosleep`, and `program_sleep(unsigned *seed)`.
- `run_simulation()` with all creation, joining, shutdown triggering, and cleanup.

`-Wall -Werror` turns an unused `static` function or variable into a build error. Every stub and global must be referenced (for example, the thread routines passed to `pthread_create`) or temporarily marked `SKELETON_UNUSED`, a macro in `csmc.c` that expands to `__attribute__((unused))`, or the build breaks. Remove the marker from each declaration once real code uses it.

---

## 6. Person 1 tasks (Megan)

**P1.1: Skeleton commit (do first, then tell Person 2).**

- Includes: `common_threads.h`, `logger.h`, `<semaphore.h>`, `<pthread.h>`, `<stdbool.h>`, `<errno.h>`, `<time.h>`, `<stdio.h>`, `<stdlib.h>`, `<limits.h>`.
- Every global, type, and prototype from section 5, plus stub bodies that compile with zero warnings under the `-std=gnu11` command.
- Mark Person 2's helper stubs that nothing calls yet (`arrival_pop`, `arrival_empty`, `pq_insert`, `pq_remove_best`, `pq_empty`, and any others) with `__attribute__((unused))`. This is temporary; Person 2 removes each attribute once the coordinator or tutor code calls that helper.

**P1.2: Helpers.**

- Checked semaphore wrappers.
- `sleep_us()` with `nanosleep`, retrying on `EINTR` with the remaining time.
- `program_sleep()` (decided): a random **whole number of milliseconds in [2, 10]**, i.e. `2 + rand_r(&seed) % 9`, slept via `sleep_us(ms * 1000)`. Mention this choice in the report.
- Seed each student distinctly, for example `time(NULL) ^ (id * 2654435761u)`.

**P1.3: `run_simulation()` setup.**

- Store the args in globals.
- `calloc` `students`, the student `pthread_t` array, `tutor_ids`, and the tutor `pthread_t` array.
- Call `queues_alloc(num_chairs)`.
- `Pthread_mutex_init(&lock, NULL)`. Init `coord_sem` and `tutor_sem` to 0, and each `students[i].wake` to 0.
- Set `free_chairs = num_chairs`, `next_seq = 0`, `shutting_down = false`, and give each student `id = i + 1` and `visits_done = 0`.
- On any failure, unwind exactly what has been created and return -1. Track how many semaphores have been initialized so you only destroy those.

**P1.4: Thread creation**, in this order: coordinator, tutors, students, with the failure handling from 4.8.

**P1.5: Student thread**, exactly as in 4.5. Key points:

- `visit` is computed from `visits_done + 1` and reused for `NO_CHAIR`, `SEATED`, and `STUDENT_DONE`.
- Chair decrement, `seq` assignment, `SEATED`, and `arrival_push` all happen in **one** critical section.
- Post `coord_sem` after unlocking.
- Never touch `free_chairs` after waking, because the tutor already released the chair.
- Read `assigned_tutor` only after `sem_wait` returns.

**P1.6: Shutdown trigger and joins.** Follow 4.7 steps 1, 3, and 6.

**P1.7: Cleanup.**

- `Sem_destroy` on `coord_sem`, `tutor_sem`, and every `wake`.
- `pthread_mutex_destroy(&lock)`, called directly with its return code checked. There is no helper for it in `common_threads.h`.
- `queues_free()`.
- Free every array.
- Return 0 only on full success.

**Done when:** `./csmc 2 2 2 2` and `./csmc 10 1 1 3` complete with a SUMMARY where `seated = queued = assigned = student_done = tutor_done = expected`. This requires Person 2's part merged.

---

## 7. Person 2 tasks

**P2.1: Arrival FIFO.** A circular buffer with capacity `num_chairs`. It provides push, pop, and empty. Add an `assert` on overflow or underflow while debugging, since invariant 2 says neither can happen.

**P2.2: Priority queue.** It provides insert, `remove_best`, and empty, ordered by `(visit asc, seq asc)` per 4.6. It also provides `queues_alloc` and `queues_free`.

**P2.3: Coordinator thread**, as in 4.5. Pop, insert, and `QUEUED` all happen in one critical section, and `tutor_sem` is posted after unlocking.

**P2.4: Tutor thread**, as in 4.5. Removal, `free_chairs++`, the `assigned_tutor` write, and `ASSIGNED` all happen in **one** critical section. Post the student's `wake` after unlocking, then sleep 0.2 ms, then log `TUTOR_DONE`.

**P2.5: Coordinator and tutor shutdown logic**, per 4.7 steps 4–5. Both loops must check their queue and the flag **after** waking, so neither goes back to `sem_wait` forever.

**Done when:** `./csmc 4 1 4 4` shows correct priority and FIFO order in the trace, and `./csmc 3 5 10 2` terminates cleanly with idle tutors.

---

## 8. Shared tasks

**S1: Design session (together, before coding).** Walk through sections 4–5 and confirm the names. Person 1 pushes the skeleton.

**S2: Integration.** Merge both halves, build on **csgrads1** with the instructions' `-std=gnu11` command, then run the basic tests.

**Build environment (decided):** all building and testing happens on **csgrads1** (and **cs3** for runs with more than 200 threads). Local macOS can't be used, because Apple's toolchain doesn't support unnamed semaphores (`sem_init` is deprecated, which `-Werror` turns into a build failure, and it fails at runtime). Workflow: edit locally, push to GitHub, pull on csgrads1, build and run there.

**S3: Trace checker script** (`check_trace.py` or a shell/awk script; not submitted). It should parse `output.log` and verify:

- **Counts:** the SUMMARY counts all equal `expected = students × help`.
- **Event order per `(student, visit)`:** `SEATED` < `QUEUED` < `ASSIGNED` < both `STUDENT_DONE` and `TUTOR_DONE`.
- **Matching tutors:** the tutor ID matches across `ASSIGNED`, `STUDENT_DONE`, and `TUTOR_DONE`.
- **Chair capacity:** replaying `SEATED` (+1) and `ASSIGNED` (−1) never goes above `chairs` or below 0. This works because those events are logged under the lock, so buffer order reflects the real order.
- **Coordinator order:** the `QUEUED` order equals the `SEATED` order.
- **Selection rule:** at each `ASSIGNED`, the chosen request has the minimum `(visit, seq)` among requests that are `QUEUED` but not yet `ASSIGNED`.
- **Visit sequence:** each student's visits run 1..help with no duplicates.

**S4: Race and deadlock testing.**

- Every suggested run, repeated many times in a loop with `timeout 60s`. Any timeout means a deadlock.
- A ThreadSanitizer build: `gcc -fsanitize=thread -g ... -std=gnu11`. This is our own debug build, not the graded one.
- `valgrind --tool=helgrind` on small configs.
- `valgrind --leak-check=full` for leaks.

**S5: Our own three non-trivial test configs for `report.txt`.** Candidates (pick three, run, and record what happens):

- `./csmc 50 50 1 3`: one chair, many tutors. This tests the chair handoff at assignment.
- `./csmc 1 4 1 25`: one student, many visits. This tests that the visit and priority sequence and the per-student `wake` are reused correctly.
- `./csmc 300 2 3 3` (on cs3, more than 200 threads): very few tutors and chairs, with heavy `NO_CHAIR` traffic and long contention.

**S6: `report.txt`.** Keep it short:

- the three configs, each with its purpose and observations
- a few sentences on the synchronization design (lift from 4.9)
- bugs found and how we fixed them; keep a running log of these while debugging

**S7: Submission.**

- Before submitting, `grep SKELETON_UNUSED csmc.c` must return nothing, and the `#define SKELETON_UNUSED` line must be removed. Rebuild with the gnu11 command afterward (zero warnings).
- One of us copies `csmc.c` and `report.txt` to `/cs4348-xv6/<netid>/P2`.
- **Both** of us put `partner.txt`, containing the other person's name and netid, in our own `/cs4348-xv6/<netid>/P2`.
- Do not submit to eLearning.

**S8: Demo prep.** Each person must be able to explain the **whole** design alone:

- each lock and semaphore
- why the chair frees at `ASSIGNED`
- why there are no lost wakeups
- shutdown under a tutor-mid-session interleaving

Swap halves and explain them to each other out loud.

---

## 9. Suggested professor test runs

| Run | Purpose | What to look for |
| --- | --- | --- |
| `./csmc 2 2 2 2` | Basic correctness | SUMMARY all equal 4 |
| `./csmc 4 1 4 4` | Priority + FIFO | With one tutor, lower visit numbers are always picked first; ties go in seat order |
| `./csmc 5 5 5 1` | One visit, many tutors | Clean exit, no stuck tutors |
| `./csmc 10 1 1 3` | Waiting-room contention | Many `NO_CHAIR`; chairs never over 1 |
| `./csmc 3 5 10 2` | More tutors than students | Idle tutors still exit |
| `./csmc 100 10 100 10` | Capacity ≥ students | `no_chair` likely 0 |
| `./csmc 200 10 20 5` | Repeated contention + termination | Run many times, no hangs |
| `./csmc 2000 50 200 10` | Stress (**cs3**) | Completes; watch thread limits |

---

## 10. Compile gotchas (`-Wall -Wextra -Werror`)

- **Build only with the instructions' command (`-std=gnu11`).** Under `gnu11`, glibc declares `rand_r`, `nanosleep`, and the semaphore functions by default, so no feature-test `#define` is needed. Do not add `_POSIX_C_SOURCE` and do not target `-std=c11`.
- **Unused `static` functions** are errors.
- **Thread-routine parameters:** use the `arg` parameter, or cast it to `(void)`, to avoid unused-parameter errors.
- **Signed/unsigned comparisons:** `-Wsign-compare` is enabled by `-Wextra`. Be careful with `seq` (unsigned long) and counts (int).
- **Thread routines must return** `NULL` on every path.
- **Do not modify** `logger.c`, `logger.h`, or `common_threads.h`. We only submit `csmc.c`, and graders will use their own copies.

---

## 11. Open questions (confirm with TA or professor; don't assume)

**Q1 and Q2 are resolved.** See section 12.

**Q3: resolved.** The programming sleep is a whole number of milliseconds in [2, 10] (see P1.2).

**Q4: resolved.** For `NO_CHAIR`, we pass the visit number the student was attempting, as the README example shows.

**Stack size (decided):** keep the default thread stack size; no `pthread_attr_setstacksize`. If `./csmc 2000 50 200 10` hits thread limits on cs3, `pthread_create` fails and §4.8 applies (`run_simulation` returns -1, no log printed). Note any such result in the report.

**Q5: Course number.** The starter README header says "CS 5348". This is presumably a shared starter with 5348; no action needed unless the TA says otherwise.

---

## 12. Discrepancies between the starter files and the instructions (resolved)

Rule: **the instructions win.** The starter files are a reference only.

| # | Starter file says | Instructions say | Decision |
| --- | --- | --- | --- |
| 1 | README: `gcc -std=c11 -Wall -Wextra -Werror -pthread csmc.c logger.c -o csmc` | `gcc csmc.c logger.c -o csmc -Wall -Wextra -Werror -pthread -std=gnu11` | Use the instructions' `gnu11` command only. No `_POSIX_C_SOURCE` define. |
| 2 | README header: "CS 5348" | CS4348 | Cosmetic; ignore. |
| 3 | `common_threads.h`: `Pthread_create` asserts, so a failure aborts the process | "Handle ... `pthread_create()` failures" (and use the helpers) | Handling failures is the requirement. Create threads with raw `pthread_create()` and an error check. Use the helpers for join, mutex init, lock, and unlock. |

Gaps the starter files fill. These are not conflicts, so we follow the starter files:

- **`NO_CHAIR` visit argument:** the README passes the visit being attempted.
- **Log buffer size:** the README and `logger.h` say the buffer is 5 events per expected visit and the program exits on overflow.
- **Semaphore and mutex-destroy wrappers:** `common_threads.h` has none, so we write our own in `csmc.c`.
- **Including the helpers:** the starter `csmc.c` doesn't include `common_threads.h`, so we add the include.
- **Keeping `run_simulation()`:** we keep its name and signature so `main()` is unchanged.
