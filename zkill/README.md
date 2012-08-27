A program to find and kill zombie processes; i.e., processes that have been forked
by a parent processes, have terminated, but their exit status has not been collected
by the parent process.

This program is implemented as a kernel module. Using a ring buffer, one
producer thread finds zombies in the Linux task_list and adds them to the buffer.
Two consumer threads traverse the ring buffer, and release the zombie processes that
is still waiting for the parent.

Not guaranteed to work all the time. Expect plenty of synchronization issues.
