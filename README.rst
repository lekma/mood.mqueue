mood.mqueue
===========

Python POSIX message queues interface (Linux only)

POSIX message queues allow processes to exchange data in the form of messages.

**See also:** `mq_overview(7) - overview of POSIX message queues
<https://linux.die.net/man/7/mq_overview>`_


-----


MessageQueue(name, flags[, mode=\ stat.S_IRUSR_ | stat.S_IWUSR_, maxmsg=-1, msgsize=-1])
    * name (str)
        Must start with '/'.

    * flags (int)
        Exactly one of the following must be specified in *flags*:

        * os.O_RDONLY_
            Open the queue to receive messages only.

        * os.O_WRONLY_
            Open the queue to send messages only.

        * os.O_RDWR_
            Open the queue to both send and receive messages.

        Zero or more of the following flags can additionally be ORed in *flags*:

        * os.O_NONBLOCK_
            Open the queue in nonblocking mode. In circumstances where `recv()`_
            and `send()`_ would normally block, these methods instead raise
            BlockingIOError_.

        * os.O_CREAT_
            Create the message queue if it does not exist.

        * os.O_EXCL_
            If os.O_CREAT_ was specified in *flags*, and a queue with the given
            *name* already exists, then raise FileExistsError_.

        If os.O_CREAT_ is specified in *flags*, then three additional arguments
        can be supplied:

        * mode (int: stat.S_IRUSR_ | stat.S_IWUSR_)
            TODO.

        * maxmsg (int: -1)
            TODO.

        * msgsize (int: -1)
            TODO.


    close()
        TODO.


    fileno() -> int
        TODO.


    .. _send():

    send(message) -> int
        * message (`bytes-like object`_)
            TODO.

        TODO.


    .. _recv():

    recv() -> bytes
        TODO.


    name
        *read only*

        TODO.


    flags
        *read only*

        TODO.


    mode
        *read only*

        TODO.


    maxmsg
        *read only*

        TODO.


    msgsize
        *read only*

        TODO.


    closed
        *read only*

        TODO.


.. _bytes-like object: https://docs.python.org/3.5/glossary.html#term-bytes-like-object
.. _os.O_RDONLY: https://docs.python.org/3.5/library/os.html#os.O_RDONLY
.. _os.O_WRONLY: https://docs.python.org/3.5/library/os.html#os.O_WRONLY
.. _os.O_RDWR: https://docs.python.org/3.5/library/os.html#os.O_RDWR
.. _os.O_NONBLOCK: https://docs.python.org/3.5/library/os.html#os.O_NONBLOCK
.. _os.O_CREAT: https://docs.python.org/3.5/library/os.html#os.O_CREAT
.. _os.O_EXCL: https://docs.python.org/3.5/library/os.html#os.O_EXCL
.. _stat.S_IRUSR: https://docs.python.org/3.5/library/stat.html#stat.S_IRUSR
.. _stat.S_IWUSR: https://docs.python.org/3.5/library/stat.html#stat.S_IWUSR
.. _errno.EAGAIN: https://docs.python.org/3.5/library/errno.html#errno.EAGAIN
.. _errno.EEXIST: https://docs.python.org/3.5/library/errno.html#errno.EEXIST
.. _BlockingIOError: https://docs.python.org/3.5/library/exceptions.html#BlockingIOError
.. _FileExistsError: https://docs.python.org/3.5/library/exceptions.html#FileExistsError

