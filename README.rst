mood.mqueue
===========

Python POSIX message queues interface (Linux only)

POSIX message queues allow processes to exchange data in the form of messages.

**See also:** `mq_overview - overview of POSIX message queues
<http://man7.org/linux/man-pages/man7/mq_overview.7.html>`_


-----


MessageQueue(name, flags[, mode=0o600, maxmsg=-1, msgsize=-1])
    * name (str)
        Each message queue is identified by a *name* of the form ``/somename``;
        that is, a string consisting of an initial slash, followed by one or
        more characters, none of which are slashes. Two processes can operate on
        the same queue by passing the same *name*.

    * flags (int)
        Exactly one of the following must be specified in *flags*:

        * O_RDONLY_
            Open the queue to receive messages only.

        * O_WRONLY_
            Open the queue to send messages only.

        * O_RDWR_
            Open the queue to both send and receive messages.

        Zero or more of the following flags can additionally be ORed in *flags*:

        * O_CLOEXEC_
            Enable the ``close-on-exec`` flag for the new file descriptor.

            **See:** `Inheritance of File Descriptors
            <https://docs.python.org/3.5/library/os.html#fd-inheritance>`_ and
            `Secure File Descriptor Handling
            <http://udrepper.livejournal.com/20407.html>`_.

        * O_NONBLOCK_
            Open the queue in nonblocking mode. In circumstances where `recv()`_
            and `send()`_ would normally block, these methods instead raise
            BlockingIOError_.

        * O_CREAT_
            Create the message queue if it does not exist.

        * O_EXCL_
            If O_CREAT_ was specified in *flags*, and a queue with the given
            *name* already exists, then raise FileExistsError_.

        If O_CREAT_ is specified in *flags*, then three additional optional
        arguments can be supplied:

        * mode (int: 0o600)
            The *mode* argument specifies the permissions to be placed on the
            new queue. The permissions settings are masked against the process
            umask.
            *mode* may take one of the following values or bitwise ORed
            combinations of them:

            * S_IRWXU_
            * S_IRUSR_
            * S_IWUSR_
            * S_IXUSR_
            * S_IRWXG_
            * S_IRGRP_
            * S_IWGRP_
            * S_IXGRP_
            * S_IRWXO_
            * S_IROTH_
            * S_IWOTH_
            * S_IXOTH_

        * maxmsg (int: -1)
            *maxmsg* is an upper limit on the number of messages that may be
            placed on the queue using `send()`_.
            If omitted or specified as a negative number, the value in
            ``/proc/sys/fs/mqueue/msg_default`` is used.
            The maximum value for *maxmsg* is defined in
            ``/proc/sys/fs/mqueue/msg_max``.

        * msgsize (int: -1)
            *msgsize* is an upper limit on the size of messages that may be
            placed on the queue.
            If omitted or specified as a negative number, the value in
            ``/proc/sys/fs/mqueue/msgsize_default`` is used.
            The maximum value for *msgsize* is defined in
            ``/proc/sys/fs/mqueue/msgsize_max``.


    .. _close():

    close()
        Closes the message queue.


    fileno() -> int
        Returns the underlying file descriptor of the message queue.


    .. _send():

    send(message) -> int
        Sends one bytes-like_ *message*. Returns the number of bytes sent.


    .. _sendall():

    sendall(message)
        Sends one bytes-like_ *message*. This calls `send()`_ repeatedly until
        all data is sent.


    .. _recv():

    recv() -> bytes
        Receives and returns one message.


    name (*read only*)
        This queue's *name*.


    flags (*read only*)
        *flags* argument passed to the constructor.


    mode (*read only*)
        File mode. The constants and functions in the stat_ module can be used
        to interpret it.


    maxmsg (*read only*)
        Maximum number of messages.


    msgsize (*read only*)
        Maximum message size (in bytes).


    closed (*read only*)
        ``True`` if the message queue is closed (i.e. `close()`_ has been
        called). ``False`` otherwise.


    blocking
        Get/set the blocking mode of the queue.
        Set to ``False`` if you want to put the queue in nonblocking mode.
        The initial blocking mode is set by the presence/absence of O_NONBLOCK_
        in the *flags* argument passed to the constructor.


.. _bytes-like: https://docs.python.org/3.5/glossary.html#term-bytes-like-object
.. _O_RDONLY: https://docs.python.org/3.5/library/os.html#os.O_RDONLY
.. _O_WRONLY: https://docs.python.org/3.5/library/os.html#os.O_WRONLY
.. _O_RDWR: https://docs.python.org/3.5/library/os.html#os.O_RDWR
.. _O_CLOEXEC: https://docs.python.org/3.5/library/os.html#os.O_CLOEXEC
.. _O_NONBLOCK: https://docs.python.org/3.5/library/os.html#os.O_NONBLOCK
.. _O_CREAT: https://docs.python.org/3.5/library/os.html#os.O_CREAT
.. _O_EXCL: https://docs.python.org/3.5/library/os.html#os.O_EXCL
.. _errno.EAGAIN: https://docs.python.org/3.5/library/errno.html#errno.EAGAIN
.. _errno.EEXIST: https://docs.python.org/3.5/library/errno.html#errno.EEXIST
.. _errno.EINVAL: https://docs.python.org/3.5/library/errno.html#errno.EINVAL
.. _BlockingIOError: https://docs.python.org/3.5/library/exceptions.html#BlockingIOError
.. _FileExistsError: https://docs.python.org/3.5/library/exceptions.html#FileExistsError
.. _OSError: https://docs.python.org/3.5/library/exceptions.html#OSError
.. _stat: https://docs.python.org/3.5/library/stat.html#module-stat
.. _S_IRWXU: https://docs.python.org/3.5/library/stat.html#stat.S_IRWXU
.. _S_IRUSR: https://docs.python.org/3.5/library/stat.html#stat.S_IRUSR
.. _S_IWUSR: https://docs.python.org/3.5/library/stat.html#stat.S_IWUSR
.. _S_IXUSR: https://docs.python.org/3.5/library/stat.html#stat.S_IXUSR
.. _S_IRWXG: https://docs.python.org/3.5/library/stat.html#stat.S_IRWXG
.. _S_IRGRP: https://docs.python.org/3.5/library/stat.html#stat.S_IRGRP
.. _S_IWGRP: https://docs.python.org/3.5/library/stat.html#stat.S_IWGRP
.. _S_IXGRP: https://docs.python.org/3.5/library/stat.html#stat.S_IXGRP
.. _S_IRWXO: https://docs.python.org/3.5/library/stat.html#stat.S_IRWXO
.. _S_IROTH: https://docs.python.org/3.5/library/stat.html#stat.S_IROTH
.. _S_IWOTH: https://docs.python.org/3.5/library/stat.html#stat.S_IWOTH
.. _S_IXOTH: https://docs.python.org/3.5/library/stat.html#stat.S_IXOTH

