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

#include "./server.h"
#include "./comm.h"
#include "./db.h"

client_t *thread_list_head;
pthread_mutex_t thread_list_mutex = PTHREAD_MUTEX_INITIALIZER;

//------------------------------------------------------------------------------------------------
// Client threads' constructor and main method

// Called by listener (in comm.c) to create a new client thread
void client_constructor(FILE *cxstr) {
    /*
     * TODO:
     * Part 1A:
     *  You should create a new client_t struct (see server.h) here and initialize 
     *  ALL of its fields. Remember that these initializations should be
     *  error-checked.
     * 
     *  Step 1. Allocate memory for a new client and set its connection stream
     *          to the input argument.
     *  Step 2. Initialize the client's list-related fields to a reasonable default.
     *  Step 3. Create the new client thread running the `run_client` routine.
     *  Step 4. Detach the new client thread.
     */
}

// Code executed by a client thread
void *run_client(void *arg) {
    /*
     * TODO:
     * Part 1A:
     *  Step 1. For the passed-in client, loop calling `comm_serve` (in comm.c), to output 
     *          the previous response and then read in the client's next command, until the 
     *          client disconnects. Execute commands using `interpret_command` (in db.c).
     *  Step 2. When the client is done sending commands, call `thread_cleanup`.
     * 
     * Part 1B: Before looping, add the passed-in client to the client list. Be sure to 
     * protect access to the client list using `thread_list_mutex`.
     * 
     * Part 3A: Use `client_control_wait` to stop the client thread from interpreting
     * commands while the server is stopped.
     * 
     * Part 3B: Support cancellation of the client thread by instead using cleanup 
     * handlers to call `thread_cleanup`.
     * 
     * Part 3C: Make sure that the server is still accepting clients before adding a 
     * client to the client list (see step 2 in `main`). If not, destroy the passed-in 
     * client and return.
     */
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
}

// Cleanup routine for client threads, called on cancels and exit.
void thread_cleanup(void *arg) {
    /*
     * TODO:
     * Part 1A: Call `client_destructor` on the passed-in client.
     * 
     * Part 1B: Remove the passed-in client from the client list before destroying it. 
     * Note that the client must be in the list before this routine is ever run.
     * Be sure to protect access to the client list using `thread_list_mutex`.
     */
}

void delete_all() {
    /*
     * TODO:
     * Part 3C: Cancel every thread in the client thread list with using
     * `pthread_cancel`.
     */
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
}

// Called by main thread to stop client threads
void client_control_stop() {
    /*
     * TODO:
     * Part 3A: Ensure that the next time client threads call `client_control_wait`
     * in `run_client`, they will block. See the `client_control_t` struct.
     */
}

// Called by main thread to resume client threads
void client_control_release() {
    /*
     * TODO:
     * Part 3A: Allow clients that are blocked within `client_control_wait`
     * to continue. See the `client_control_t` struct.
     */
}

//------------------------------------------------------------------------------------------------
// SIGINT signal handling

// Code executed by the signal handler thread. 'man 7 signal' and 'man sigwait' 
// are both helpful for implementing this function.
// All of the server's client threads should terminate on SIGINT; the server (this 
// includes the listener thread), however, should not!
void *monitor_signal(void *arg) {
    /* 
     * TODO:
     * Part 3D: Continually wait for a SIGINT to be sent to the server process 
     * and cancel all client threads when one arrives. This thread will be canceled
     * by `sig_handler_destructor` - note that `sigwait` is a cancellation point.
     */
    return NULL;
}

sig_handler_t *sig_handler_constructor() {
    /* 
     * TODO:
     * Part 3D: Create a thread to handle SIGINT. Make sure that the thread that 
     * this function creates is the ONLY thread that ever responds to SIGINT 
     * (use `pthread_sigmask`!). Be sure to take a look at sig_hander_t in server.h.
     */
    return NULL;
}

void sig_handler_destructor(sig_handler_t *sighandler) {
    /* 
     * TODO:
     * Part 3D: Free any resources allocated in sig_handler_constructor, and
     * cancel and join with the signal handler's thread. 
     */
}

//------------------------------------------------------------------------------------------------
// Main function

// The arguments to the server should be the port number.
int main(int argc, char *argv[]) {
    /*
     * TODO:
     * Part 1A:
     *  Step 1. Block SIGPIPE using `pthread_sigmask` so that the server does not 
     *          abort when a client disconnects.
     *  Step 2. Start a listener thread for clients (see `start_listener` in comm.c).
     *  Step 3. Join with the listener thread.
     * 
     * Part 3A: Before joining the listener thread, loop for command line input 
     * and handle any print, stop, and go command requests.
     * 
     * Part 3C:
     *  Step 1. Modify the command line loop to break on receiving EOF.
     *  Step 2. After receiving EOF, use a thread-safe mechanism to indicate that the server
     *          is no longer accepting clients, and then cancel all client threads using 
     *          `delete_all`.
     *          Think carefully about what happens at the start of `run_client` and ensure that 
     *          your mechanism does not allow any way for a thread to add itself to the thread
     *          list after your mechanism is activated.
     *  Step 3. After calling `delete_all`, make sure that the thread list is empty using the 
     *          `server_control_t` struct. (Note that you will need to modify other functions 
     *          for the struct to accurately keep track of the number of threads in the list - 
     *          where does it make sense to modify the `num_client_threads` field?)
     *  Step 4. Once the thread list is empty, cleanup the database, and then cancel 
     *          and join with the listener thread.
     * 
     * Part 3D: 
     *  Step 1. After blocking SIGPIPE, create a SIGINT signal handler using 
     *          `sig_handler_constructor`.
     *  Step 2. Destroy the signal handler using `sig_handler_destructor` right after the server
     *          receives EOF.
     */

    return 0;
}
