What is this?

You will find two system calls, file_copy & dir_copy, implemented in myfs.c

file_copy takes a source file, and copies the content to a destination file.
If the destination exists, it will be over-written; otherwise, a new file will
be created with the specified full path.

dir_copy takes a source directory, and recursively copies the content into
the destination directory.
