```
 ____        _        _
|  _ \  __ _| |_ __ _| |__   __ _ ___  ___
| | | |/ _` | __/ _` | '_ \ / _` / __|/ _ \
| |_| | (_| | || (_| | |_) | (_| \__ \  __/
|____/ \__,_|\__\__,_|_.__/ \__,_|___/\___|
```

You should also have a README.md explaining the structure of your program and any unsolved bugs. Note that this means your 
README.md must document any additional helper functions you’ve written and any given function signatures that you’ve changed; 
otherwise, you could be deducted for them.

Helper functions: I created a helper function called lock in db.c that takes in a rwlock type and a rwlock, then it 
                  will rdlock or wrlock according to the locktype.

New Struct: I defined a new server_accept_control struct in server.h that indicates whether the server is accepting new clients.
            The struct has a mutex in order to make it thread safe

Signature Changes: I changed the signature of search to include read or write locktype to reflect whether we intend to
                   just modify search through the tree or also modify it.

enum locktype: I defined a new enum locktype in db.h that can take either l_read = 0 or l_write = 1. This is used in
               the lock helper function.

Bugs: None to my knowledge after extensive testing

Program structure: I implemented fine-grained locking in db.c. I also implemented the required functions in server.c
in a way that is threadsafe. In main of server.c, I blocked SIGPIPE and set up the listener and signal thread before I 
coded my mini-shell. If EOF is received, the server will terminate cleanly with the code that I set up after the command 
while loop.