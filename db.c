#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "./comm.h"
#include "./db.h"

#define MAXLEN 256

// The root node of the binary tree, unlike all
// other nodes in the tree, this one is never
// freed (it's allocated in the data region).
node_t head = {"", "", 0, 0, PTHREAD_RWLOCK_INITIALIZER};

void lock(pthread_rwlock_t *rwlock, enum locktype lt) {
    // lt of 0 means l_read, while lt of 1 means l_write
    int err;
    assert(lt == l_read || lt == l_write);
    if (lt == l_read) {
        if ((err = pthread_rwlock_rdlock(rwlock)))
            handle_error_en(err, "pthread_rwlock_rdlock");
    } else if ((err = pthread_rwlock_wrlock(rwlock)))
        handle_error_en(err, "pthread_rwlock_wrlock");
}

//------------------------------------------------------------------------------------------------
// Constructor, destructor, and cleanup methods

node_t *node_constructor(char *arg_key, char *arg_value, node_t *arg_left,
                         node_t *arg_right) {
    size_t key_len = strlen(arg_key);
    size_t val_len = strlen(arg_value);

    if (key_len > MAXLEN || val_len > MAXLEN) return 0;

    node_t *new_node = (node_t *)malloc(sizeof(node_t));

    if (new_node == NULL) return 0;

    if ((new_node->key = (char *)malloc(key_len + 1)) == NULL) {
        free(new_node);
        return 0;
    }
    if ((new_node->value = (char *)malloc(val_len + 1)) == NULL) {
        free(new_node->key);
        free(new_node);
        return 0;
    }

    if ((snprintf(new_node->key, MAXLEN, "%s", arg_key)) < 0) {
        free(new_node->value);
        free(new_node->key);
        free(new_node);
        return 0;
    }
    if ((snprintf(new_node->value, MAXLEN, "%s", arg_value)) < 0) {
        free(new_node->value);
        free(new_node->key);
        free(new_node);
        return 0;
    }
    int err;
    if ((err = pthread_rwlock_init(&new_node->rw_lock, 0)) != 0) {
        free(new_node->value);
        free(new_node->key);
        free(new_node);
        handle_error_en(err, "pthread_rwlock_init");
    }
    new_node->lchild = arg_left;
    new_node->rchild = arg_right;
    return new_node;
}

void node_destructor(node_t *node) {
    int err;
    if ((err = pthread_rwlock_destroy(&node->rw_lock)) != 0)
        handle_error_en(err, "pthread_rwlock_destroy");
    if (node->key != NULL) free(node->key);
    if (node->value != NULL) free(node->value);
    free(node);
}

/* Recursively destroys node and all its children. */
void db_cleanup_recurs(node_t *node) {
    if (node == NULL) {
        return;
    }

    db_cleanup_recurs(node->lchild);
    db_cleanup_recurs(node->rchild);

    node_destructor(node);
}

void db_cleanup() {
    db_cleanup_recurs(head.lchild);
    db_cleanup_recurs(head.rchild);
}

//------------------------------------------------------------------------------------------------
// Database modifiers and accessors

node_t *search(char *key, node_t *parent, node_t **parentpp, enum locktype lt) {
    /*
     * TODO:
     * Part 2: Make this thread safe!
     */
    int err;
    node_t *next;
    if (strcmp(key, parent->key) < 0) {
        next = parent->lchild;
    } else {
        next = parent->rchild;
    }

    node_t *result;
    if (next == NULL) {
        result = NULL;
    } else {
        lock(&next->rw_lock, lt);
        if (strcmp(key, next->key) == 0) {
            result = next;
        } else {
            if ((err = pthread_rwlock_unlock(&parent->rw_lock)))
                handle_error_en(err, "pthread_rwlock_unlock");
            return search(key, next, parentpp, lt);
        }
    }

    if (parentpp != NULL) {
        *parentpp = parent;
    } else {
        if ((err = pthread_rwlock_unlock(&parent->rw_lock)))
            handle_error_en(err, "pthread_rwlock_unlock");
        //        return result;
    }
    return result;
}

void db_query(char *key, char *result, int len) {
    /*
     * TODO:
     * Part 2: Make this thread safe!
     */
    lock(&head.rw_lock, l_read);
    node_t *target = search(key, &head, NULL, 0);
    if (target == NULL) {
        snprintf(result, len, "not found");
    } else {
        snprintf(result, len, "%s", target->value);
        int err;
        if ((err = pthread_rwlock_unlock(&target->rw_lock)))
            handle_error_en(err, "pthread_rwlock_unlock");
    }
}

int db_add(char *key, char *value) {
    /*
     * TODO:
     * Part 2: Make this thread safe!
     */
    int err;
    node_t *parent;
    node_t *target;
    lock(&head.rw_lock, 1);
    if ((target = search(key, &head, &parent, 1)) != NULL) {
        if ((err = pthread_rwlock_unlock(&target->rw_lock)))
            handle_error_en(err, "pthread_rwlock_unlock");
        if ((err = pthread_rwlock_unlock(&parent->rw_lock)))
            handle_error_en(err, "pthread_rwlock_unlock");
        return 0;
    }

    node_t *newnode = node_constructor(key, value, NULL, NULL);

    if (strcmp(key, parent->key) < 0)
        parent->lchild = newnode;
    else
        parent->rchild = newnode;

    if ((err = pthread_rwlock_unlock(&parent->rw_lock)))
        handle_error_en(err, "pthread_rwlock_unlock");

    return 1;
}

int db_remove(char *key) {
    /*
     * TODO:
     * Part 2: Make this thread safe!
     */
    int err;
    node_t *parent;  // parent of the node to delete
    node_t *dnode;   // node to delete

    lock(&head.rw_lock, 1);
    // first, find the node to be removed
    if ((dnode = search(key, &head, &parent, 1)) == NULL) {
        // it's not there
        if ((err = pthread_rwlock_unlock(&parent->rw_lock)))
            handle_error_en(err, "pthread_rwlock_unlock");
        return 0;
    }

    // We found it. If the target has no right child, then we can simply replace
    // its parent's pointer to the target with the target's own left child.

    if (dnode->rchild == NULL) {
        if (strcmp(dnode->key, parent->key) < 0)
            parent->lchild = dnode->lchild;
        else
            parent->rchild = dnode->lchild;
        if ((err = pthread_rwlock_unlock(&dnode->rw_lock)))
            handle_error_en(err, "pthread_rwlock_unlock");
        if ((err = pthread_rwlock_unlock(&parent->rw_lock)))
            handle_error_en(err, "pthread_rwlock_unlock");
        // done with dnode
        node_destructor(dnode);
    } else if (dnode->lchild == NULL) {
        // ditto if the target has no left child
        if (strcmp(dnode->key, parent->key) < 0)
            parent->lchild = dnode->rchild;
        else
            parent->rchild = dnode->rchild;
        if ((err = pthread_rwlock_unlock(&dnode->rw_lock)))
            handle_error_en(err, "pthread_rwlock_unlock");
        if ((err = pthread_rwlock_unlock(&parent->rw_lock)))
            handle_error_en(err, "pthread_rwlock_unlock");
        // done with dnode
        node_destructor(dnode);
    } else {
        // Find the lexicographically smallest node in the right subtree and
        // replace the node to be deleted with that node. This new node thus is
        // lexicographically smaller than all nodes in its right subtree, and
        // greater than all nodes in its left subtree
        if ((err = pthread_rwlock_unlock(&parent->rw_lock)))
            handle_error_en(err, "pthread_rwlock_unlock");

        node_t *next = dnode->rchild;
        lock(&next->rw_lock, 1);
        node_t **pnext = &dnode->rchild;

        while (next->lchild != NULL) {
            // work our way down the lchild chain, finding the smallest node
            // in the subtree.
            node_t *nextl = next->lchild;
            lock(&nextl->rw_lock, 1);
            pnext = &next->lchild;
            if ((err = pthread_rwlock_unlock(&next->rw_lock)))
                handle_error_en(err, "pthread_rwlock_unlock");
            next = nextl;
        }

        // replace next's position on right subtree with its right child
        *pnext = next->rchild;

        // replace dnode with the contents of next
        dnode->key = realloc(dnode->key, strlen(next->key) + 1);
        dnode->value = realloc(dnode->value, strlen(next->value) + 1);

        snprintf(dnode->key, MAXLEN, "%s", next->key);
        snprintf(dnode->value, MAXLEN, "%s", next->value);

        if ((err = pthread_rwlock_unlock(&next->rw_lock)))
            handle_error_en(err, "pthread_rwlock_unlock");

        node_destructor(next);

        if ((err = pthread_rwlock_unlock(&dnode->rw_lock)))
            handle_error_en(err, "pthread_rwlock_unlock");
    }

    return 1;
}

//------------------------------------------------------------------------------------------------
// Printing methods and their helpers

static inline void print_spaces(int lvl, FILE *out) {
    for (int i = 0; i < lvl; i++) {
        fprintf(out, " ");
    }
}

/* helper function for db_print */
void db_print_recurs(node_t *node, int lvl, FILE *out) {
    /*
     * TODO:
     * Part 2: Make this thread safe!
     */
    int err;
    print_spaces(lvl, out);  // print spaces to differentiate levels
    // print node's key/value, or (root) if it's the root
    if (node == NULL) {
        fprintf(out, "(null)\n");
        return;
    }

    lock(&node->rw_lock, 0);

    if (node == &head)
        fprintf(out, "(root)\n");
    else
        fprintf(out, "%s %s\n", node->key, node->value);

    db_print_recurs(node->lchild, lvl + 1, out);
    db_print_recurs(node->rchild, lvl + 1, out);
    if ((err = pthread_rwlock_unlock(&node->rw_lock)))
        handle_error_en(err, "pthread_rwlock_unlock");
}

int db_print(char *filename) {
    FILE *out;
    if (filename == NULL) {
        db_print_recurs(&head, 0, stdout);
        return 0;
    }

    // skip over leading whitespace
    while (isspace(*filename)) {
        filename++;
    }

    if (*filename == '\0') {
        db_print_recurs(&head, 0, stdout);
        return 0;
    }

    if ((out = fopen(filename, "w+")) == NULL) {
        return -1;
    }

    db_print_recurs(&head, 0, out);
    fclose(out);

    return 0;
}

//------------------------------------------------------------------------------------------------
// Command interpreting

/*
 * Interprets the given command string and writes up to len bytes into response,
 * where len is the buffer size.
 */
void interpret_command(char *command, char *response, int len) {
    char value[MAXLEN];
    char ibuf[MAXLEN];
    char name[MAXLEN];
    int sscanf_ret;

    if (strlen(command) <= 1) {
        snprintf(response, len, "ill-formed command");
        return;
    }

    // which command is it?
    switch (command[0]) {
        case 'q':
            // Query
            sscanf_ret = sscanf(&command[1], "%255s", name);
            if (sscanf_ret < 1) {
                snprintf(response, len, "ill-formed command");
                return;
            }
            db_query(name, response, len);
            if (strlen(response) == 0) {
                snprintf(response, len, "not found");
            }
            return;

        case 'a':
            // Add to the database
            sscanf_ret = sscanf(&command[1], "%255s %255s", name, value);
            if (sscanf_ret < 2) {
                snprintf(response, len, "ill-formed command");
                return;
            }
            if (db_add(name, value)) {
                snprintf(response, len, "added");
            } else {
                snprintf(response, len, "already in database");
            }
            return;

        case 'd':
            // Delete from the database
            sscanf_ret = sscanf(&command[1], "%255s", name);
            if (sscanf_ret < 1) {
                snprintf(response, len, "ill-formed command");
                return;
            }
            if (db_remove(name)) {
                snprintf(response, len, "removed");
            } else {
                snprintf(response, len, "not in database");
            }
            return;

        case 'f':
            // process the commands in a file (silently)
            sscanf_ret = sscanf(&command[1], "%255s", name);
            if (sscanf_ret < 1) {
                snprintf(response, len, "ill-formed command");
                return;
            }

            FILE *finput = fopen(name, "r");
            if (!finput) {
                snprintf(response, len, "bad file name");
                return;
            }
            while (fgets(ibuf, sizeof(ibuf), finput) != 0) {
                pthread_testcancel();  // fgets is not a cancellation point
                interpret_command(ibuf, response, len);
            }
            fclose(finput);
            snprintf(response, len, "file processed");
            return;

        default:
            snprintf(response, len, "ill-formed command");
            return;
    }
}
