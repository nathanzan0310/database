
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "./comm.h"
#include "./db.h"
#include "./server.h"

client_t *thread_list_head;
pthread_mutex_t thread_list_mutex = PTHREAD_MUTEX_INITIALIZER;
client_control_t client_control = {PTHREAD_MUTEX_INITIALIZER,
                                   PTHREAD_COND_INITIALIZER, 1};
server_control_t server_control = {PTHREAD_MUTEX_INITIALIZER,
                                   PTHREAD_COND_INITIALIZER, 0};
// struct server_accept_control indicates whether the server will accept clients
// 0 is not accepting, 1 is accepting
server_accept_control_t server_accept_control = {PTHREAD_MUTEX_INITIALIZER, 1};

//------------------------------------------------------------------------------------------------
// Client threads' constructor and main method

// Called by listener (in comm.c) to create a new client thread
void client_constructor(FILE *cxstr) {
    /*
     * TODO:
     * Part 1A:
     *  You should create a new client_t struct (see server.h) here and
     * initialize ALL of its fields. Remember that these initializations should
     * be error-checked.
     *
     *  Step 1. Allocate memory for a new client and set its connection stream
     *          to the input argument.
     *  Step 2. Initialize the client's list-related fields to a reasonable
     * default. Step 3. Create the new client thread running the `run_client`
     * routine. Step 4. Detach the new client thread.
     */
    int err;
    client_t *client;
    if ((client = malloc(sizeof(client_t))) == NULL) {
        perror("malloc");
        exit(1);
    }
    if (cxstr == NULL) {
        fprintf(stderr, "Client Constructor: not a valid file\n");
    }
    client->cxstr = cxstr;
    client->next = NULL;
    client->prev = NULL;
    client->thread = 0;
    if ((err = pthread_create(&client->thread, 0, &run_client, client))) {
        handle_error_en(err, "pthread_create");
    }
    if ((err = pthread_detach(client->thread))) {
        handle_error_en(err, "pthread_detach");
    }
}

// Code executed by a client thread
void *run_client(void *arg) {
    /*
     * TODO:
     * Part 1A:
     *  Step 1. For the passed-in client, loop calling `comm_serve` (in comm.c),
     * to output the previous response and then read in the client's next
     * command, until the client disconnects. Execute commands using
     * `interpret_command` (in db.c). Step 2. When the client is done sending
     * commands, call `thread_cleanup`.
     *
     * Part 1B: Before looping, add the passed-in client to the client list. Be
     * sure to protect access to the client list using `thread_list_mutex`.
     *
     * Part 3A: Use `client_control_wait` to stop the client thread from
     * interpreting commands while the server is stopped.
     *
     * Part 3B: Support cancellation of the client thread by instead using
     * cleanup handlers to call `thread_cleanup`.
     *
     * Part 3C: Make sure that the server is still accepting clients before
     * adding a client to the client list (see step 2 in `main`). If not,
     * destroy the passed-in client and return.
     */
    client_t *client = arg;
    if (server_accept_control.accepting == 0) {
        client_destructor(client);
        return (void *)-1;
    }

    char response[BUFLEN];
    char command[BUFLEN];
    memset(response, 0, BUFLEN);
    memset(command, 0, BUFLEN);

    pthread_mutex_lock(&thread_list_mutex);
    if (thread_list_head == NULL)
        client->next = NULL;
    else {
        client->next = thread_list_head;
        thread_list_head->prev = client;
    }
    client->prev = NULL;
    thread_list_head = client;

    pthread_cleanup_push((void *)&thread_cleanup, client);
    pthread_mutex_unlock(&thread_list_mutex);
    pthread_mutex_lock(&server_control.server_mutex);
    server_control.num_client_threads++;
    pthread_mutex_unlock(&server_control.server_mutex);
    while (comm_serve(client->cxstr, response, command) == 0) {
        client_control_wait();
        interpret_command(command, response, BUFLEN);
    }
    int err;
    if ((err = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0)))
        handle_error_en(err, "pthread_setcancelstate");
    pthread_cleanup_pop(1);
    return NULL;
}

//------------------------------------------------------------------------------------------------
// Methods for client thread cleanup, destruction, and cancellation

void client_destructor(client_t *client) {
    /*
     * TODO:
     * Part 1A: Free and close all resources associated with a client.
     * (Take a look at `comm_shutdown` in comm.c)
     */
    comm_shutdown(client->cxstr);
    free(client);
}

// Cleanup routine for client threads, called on cancels and exit.
void thread_cleanup(void *arg) {
    /*
     * TODO:
     * Part 1A: Call `client_destructor` on the passed-in client.
     *
     * Part 1B: Remove the passed-in client from the client list before
     * destroying it. Note that the client must be in the list before this
     * routine is ever run. Be sure to protect access to the client list using
     * `thread_list_mutex`.
     */
    int err;
    pthread_mutex_lock(&thread_list_mutex);
    client_t *client = arg;
    client_t *next = client->next;
    client_t *prev = client->prev;
    if (client == thread_list_head) {
        thread_list_head = NULL;
    }
    if (next) {
        next->prev = prev;
    }
    if (prev) {
        prev->next = next;
    }
    pthread_mutex_unlock(&thread_list_mutex);

    client_destructor(client);

    pthread_mutex_lock(&server_control.server_mutex);
    server_control.num_client_threads--;
    if (!server_control.num_client_threads)
        if ((err = pthread_cond_broadcast(&server_control.server_cond)))
            handle_error_en(err, "pthread_cond_broadcast");
    pthread_mutex_unlock(&server_control.server_mutex);
}

void delete_all() {
    /*
     * TODO:
     * Part 3C: Cancel every thread in the client thread list with using
     * `pthread_cancel`.
     */
    int err;
    pthread_mutex_lock(&thread_list_mutex);
    client_t *cur = thread_list_head;
    while (cur != NULL) {
        client_t *next = cur->next;
        if ((err = pthread_cancel(cur->thread)))
            handle_error_en(err, "pthread_cancel");
        cur = next;
    }
    pthread_mutex_unlock(&thread_list_mutex);
}

//------------------------------------------------------------------------------------------------
// Methods for stop/go server commands

// Called by client threads to wait until progress is permitted
void client_control_wait() {
    /*
     * TODO:
     * Part 3A: Block the calling thread until the main thread calls
     * `client_control_release`. See the `client_control_t` struct.
     *
     * Part 3B: Support thread-safe cancellation of a client thread by
     * using cleanup handlers. (Remember that `pthread_cond_wait` is a
     * cancellation point!)
     */
    int err;
    pthread_mutex_lock(&client_control.go_mutex);
    pthread_cleanup_push((void *)&pthread_mutex_unlock,
                         &client_control.go_mutex);
    while (!client_control.stopped) {
        if ((err = pthread_cond_wait(&client_control.go,
                                     &client_control.go_mutex)))
            handle_error_en(err, "pthread_cond_wait");
    }
    pthread_cleanup_pop(1);
}

// Called by main thread to stop client threads
void client_control_stop() {
    /*
     * TODO:
     * Part 3A: Ensure that the next time client threads call
     * `client_control_wait` in `run_client`, they will block. See the
     * `client_control_t` struct.
     */
    pthread_mutex_lock(&client_control.go_mutex);
    client_control.stopped = 0;
    pthread_mutex_unlock(&client_control.go_mutex);
}

// Called by main thread to resume client threads
void client_control_release() {
    /*
     * TODO:
     * Part 3A: Allow clients that are blocked within `client_control_wait`
     * to continue. See the `client_control_t` struct.
     */
    int err;
    pthread_mutex_lock(&client_control.go_mutex);
    client_control.stopped = 1;
    if ((err = pthread_cond_broadcast(&client_control.go)))
        handle_error_en(err, "pthread_cond_broadcast");
    pthread_mutex_unlock(&client_control.go_mutex);
}

//------------------------------------------------------------------------------------------------
// SIGINT signal handling

// Code executed by the signal handler thread. 'man 7 signal' and 'man sigwait'
// are both helpful for implementing this function.
// All of the server's client threads should terminate on SIGINT; the server
// (this includes the listener thread), however, should not!
void *monitor_signal(void *arg) {
    /*
     * TODO:
     * Part 3D: Continually wait for a SIGINT to be sent to the server process
     * and cancel all client threads when one arrives. This thread will be
     * canceled by `sig_handler_destructor` - note that `sigwait` is a
     * cancellation point.
     */
    int sig = 0;
    while (1) {
        if (sigwait((sigset_t *)arg, &sig)) {
            perror("sigwait");
            exit(1);
        }
        if (sig == SIGINT) {
            printf("SIGINT received, cancelling all clients\n");
            delete_all();
        }
    }
}

sig_handler_t *sig_handler_constructor() {
    /*
     * TODO:
     * Part 3D: Create a thread to handle SIGINT. Make sure that the thread that
     * this function creates is the ONLY thread that ever responds to SIGINT
     * (use `pthread_sigmask`!). Be sure to take a look at sig_hander_t in
     * server.h.
     */
    sig_handler_t *sig_thread = (sig_handler_t *)malloc(sizeof(sig_handler_t));
    if (sig_thread == NULL) {
        perror("malloc failed");
        exit(1);
    }
    sigemptyset(&sig_thread->set);
    sigaddset(&sig_thread->set, SIGINT);
    if (pthread_sigmask(SIG_BLOCK, &sig_thread->set, 0))
        handle_error_en(errno, "pthread_sigmask");
    if (pthread_create(&sig_thread->thread, 0, monitor_signal,
                       &sig_thread->set))
        handle_error_en(errno, "pthread_create");
    if (pthread_detach(sig_thread->thread))
        handle_error_en(errno, "pthread_detach");
    return sig_thread;
}

void sig_handler_destructor(sig_handler_t *sighandler) {
    /*
     * TODO:
     * Part 3D: Free any resources allocated in sig_handler_constructor, and
     * cancel and join with the signal handler's thread.
     */
    if (pthread_cancel(sighandler->thread))
        handle_error_en(errno, "pthread_cancel");
    free(sighandler);
}

//------------------------------------------------------------------------------------------------
// Main function

// The arguments to the server should be the port number.
int main(int argc, char *argv[]) {
    /*
     * TODO:
     * Part 1A:
     *  Step 1. Block SIGPIPE using `pthread_sigmask` so that the server does
     * not abort when a client disconnects. Step 2. Start a listener thread for
     * clients (see `start_listener` in comm.c). Step 3. Join with the listener
     * thread.
     */
    int err;
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    sig_handler_t *sig_handler = sig_handler_constructor();
    if ((err = pthread_sigmask(SIG_BLOCK, &set, 0)) != 0)
        handle_error_en(err, "pthread_sigmask");
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(1);
    }
    int port = (int)strtol(argv[1], 0, 10);
    pthread_t lThread = start_listener(port, &client_constructor);

    /*
     * Part 3A: Before joining the listener thread, loop for command line input
     * and handle any print, stop, and go command requests.
     */
    char buf[BUFLEN];
    memset(buf, 0, BUFLEN);
    ssize_t bytesRead;
    while ((bytesRead = read(STDIN_FILENO, buf, BUFLEN)) > 0) {
        if (buf[0] == 'p') {
            char *file = strtok(&buf[1], " \t\n");
            db_print(file);
        } else if (buf[0] == 's') {
            client_control_stop();
            if (printf("All clients stopped\n") < 0) {
                perror("printf");
                exit(0);
            }
        } else if (buf[0] == 'g') {
            client_control_release();
            if (printf("All clients resumed\n") < 0) {
                perror("printf");
                exit(0);
            }
        }
        memset(buf, 0, BUFLEN);
    }
    /*
     * Part 3C:
     *  Step 1. Modify the command line loop to break on receiving EOF.
     *  Step 2. After receiving EOF, use a thread-safe mechanism to indicate
     * that the server is no longer accepting clients, and then cancel all
     * client threads using `delete_all`. Think carefully about what happens at
     * the start of `run_client` and ensure that your mechanism does not allow
     * any way for a thread to add itself to the thread list after your
     * mechanism is activated. Step 3. After calling `delete_all`, make sure
     * that the thread list is empty using the `server_control_t` struct. (Note
     * that you will need to modify other functions for the struct to accurately
     * keep track of the number of threads in the list - where does it make
     * sense to modify the `num_client_threads` field?) Step 4. Once the thread
     * list is empty, cleanup the database, and then cancel and join with the
     * listener thread.
     */
    if (bytesRead == EOF) {
        server_accept_control.accepting = 0;
        if (printf("Zero client connections, cleaning up database\n") < 0) {
            perror("printf");
            exit(0);
        }
    }
    sig_handler_destructor(sig_handler);
    delete_all();
    pthread_mutex_lock(&server_control.server_mutex);
    while (server_control.num_client_threads)
        if (pthread_cond_wait(&server_control.server_cond,
                              &server_control.server_mutex))
            handle_error_en(errno, "pthread_cond_wait");
    pthread_mutex_unlock(&server_control.server_mutex);
    db_cleanup();
    if (printf("Database clean complete\n") < 0) {
        perror("printf");
        exit(0);
    }
    if (pthread_cancel(lThread)) handle_error_en(errno, "pthread_cancel");
    if (pthread_join(lThread, 0)) handle_error_en(errno, "pthread_join");
    if (pthread_mutex_destroy(&server_control.server_mutex))
        handle_error_en(errno, "pthread_mutex_destroy");
    if (pthread_cond_destroy(&server_control.server_cond))
        handle_error_en(errno, "pthread_cond_destroy");
    if (pthread_mutex_destroy(&client_control.go_mutex))
        handle_error_en(errno, "pthread_mutex_destroy");
    if (pthread_cond_destroy(&client_control.go))
        handle_error_en(errno, "pthread_cond_destroy");
    if (pthread_mutex_destroy(&thread_list_mutex))
        handle_error_en(errno, "pthread_mutex_destroy");
    pthread_exit(0);

    /*
     * Part 3D:
     *  Step 1. After blocking SIGPIPE, create a SIGINT signal handler using
     *          `sig_handler_constructor`.
     *  Step 2. Destroy the signal handler using `sig_handler_destructor` right
     * after the server receives EOF.
     */
}