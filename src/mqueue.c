/*
#
# Copyright © 2021 Malek Hadj-Ali
# All rights reserved.
#
# This file is part of mood.
#
# mood is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 3
# as published by the Free Software Foundation.
#
# mood is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with mood.  If not, see <http://www.gnu.org/licenses/>.
#
*/


#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "structmember.h"

#include "helpers/helpers.h"

#include <mqueue.h>
#include <signal.h>


#define MQUEUE_PROC_INTERFACE "/proc/sys/fs/mqueue"
#define MQUEUE_DEFAULT_MAXMSG MQUEUE_PROC_INTERFACE "/msg_default"
#define MQUEUE_DEFAULT_MSGSIZE MQUEUE_PROC_INTERFACE "/msgsize_default"


/* MessageQueue */
typedef struct {
    PyObject_HEAD
    PyObject *name;
    int flags;
    mode_t mode;
    struct mq_attr attr;
    mqd_t mqd;
    int owner;
    char *msg;
    PyObject *callback;
} MessageQueue;


/* module state */
typedef struct {
    long default_maxmsg;
    long min_maxmsg;
    long default_msgsize;
    long min_msgsize;
} module_state;


/* fwd decls */
static module_state *_module_get_state(void);


/* --------------------------------------------------------------------------
   helpers
   -------------------------------------------------------------------------- */

static int
_mqueue_get_limit(const char *filename, long *value)
{
    FILE *stream = NULL;
    int res = -1;

    if (!(stream = fopen(filename, "r"))) {
        _PyErr_SetFromErrnoWithFilename(filename);
        return -1;
    }
    int saved_errno = errno;
    errno = 0;
    if ((res = ((fscanf(stream, "%ld", value) != 1) || errno) ? -1 : 0)) {
        if (errno) {
            _PyErr_SetFromErrnoWithFilename(filename);
        }
        else {
            PyErr_Format(PyExc_EOFError,
                         "No number found in file: '%s'", filename);
        }
    }
    errno = saved_errno;
    if (fclose(stream)) {
        _PyErr_SetFromErrnoWithFilenameAndChain(filename);
        return -1;
    }
    return res;
}


/* --------------------------------------------------------------------------
   MessageQueue
   -------------------------------------------------------------------------- */

static inline MessageQueue *
__mq_new(PyTypeObject *type)
{
    MessageQueue *self = NULL;

    if ((self = PyObject_GC_NEW(MessageQueue, type))) {
        self->name = NULL;
        self->flags = 0;
        self->mode = S_IRUSR | S_IWUSR; // ReadWrite by owner;
        self->attr.mq_flags = 0;
        self->attr.mq_maxmsg = -1;
        self->attr.mq_msgsize = -1;
        self->attr.mq_curmsgs = 0;
        self->mqd = -1;
        self->owner = 0;
        self->msg = NULL;
        self->callback = NULL;
        PyObject_GC_Track(self);
    }
    return self;
}


static inline int
__mq_init(MessageQueue *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"name", "flags", "mode", "maxmsg", "msgsize", NULL};
    module_state *state = NULL;
    const char *name = NULL;
    struct stat st = { 0 };

    if (
        !(state = _module_get_state()) ||
        !PyArg_ParseTupleAndKeywords(
            args, kwargs, "O&i|Ill:__new__", kwlist,
            PyUnicode_FSConverter, &self->name,
            &self->flags, &self->mode,
            &self->attr.mq_maxmsg, &self->attr.mq_msgsize
        )
    ) {
        return -1;
    }

    if (self->attr.mq_maxmsg < 0) {
        self->attr.mq_maxmsg = state->default_maxmsg;
    }
    else if (self->attr.mq_maxmsg < state->min_maxmsg) {
        self->attr.mq_maxmsg = state->min_maxmsg;
    }

    if (self->attr.mq_msgsize < 0) {
        self->attr.mq_msgsize = state->default_msgsize;
    }
    else if (self->attr.mq_msgsize < state->min_msgsize) {
        self->attr.mq_msgsize = state->min_msgsize;
    }
    else {
        // XXX: hmm...?
        self->attr.mq_msgsize = ((self->attr.mq_msgsize + 7) & ~7);
    }

    name = PyBytes_AS_STRING(self->name);
    if ((self->flags & O_CREAT)) {
        int flags = self->flags | O_EXCL;
        int saved_errno = errno;
        if (((self->mqd = mq_open(name, flags, self->mode, &self->attr)) == -1)) {
            if ((flags != self->flags) && (errno == EEXIST)) {
                errno = saved_errno;
                self->mqd = mq_open(name, self->flags, self->mode, &self->attr);
            }
        }
        else {
            self->owner = 1;
        }
    }
    else {
        self->mqd = mq_open(name, self->flags, self->mode, &self->attr);
    }
    if (self->mqd == -1) {
        switch (errno) {
            case EACCES:
            case EEXIST:
            case ENAMETOOLONG:
            case ENOENT:
                _PyErr_SetFromErrnoWithFilename(name);
                break;
            default:
                _PyErr_SetFromErrno();
                break;
        }
        return -1;
    }

    if (fstat(self->mqd, &st)) {
        _PyErr_SetFromErrno();
        return -1;
    }
    self->mode = st.st_mode;

    if (mq_getattr(self->mqd, &self->attr)) {
        _PyErr_SetFromErrno();
        return -1;
    }

    if (!(self->msg = PyObject_Malloc(self->attr.mq_msgsize))) {
        PyErr_NoMemory();
        return -1;
    }

    return 0;
}


static inline int
__mq_close(MessageQueue *self)
{
    const char *name = PyBytes_AS_STRING(self->name);
    int res = 0;

    if (self->mqd != -1) {
        if ((res = mq_close(self->mqd))) {
            _PyErr_SetFromErrno();
        }
        else if (self->owner && (res = mq_unlink(name))) {
            _PyErr_SetFromErrnoWithFilename(name);
        }
        self->mqd = -1;
    }
    return res;
}


static inline int
__mq_getblocking(MessageQueue *self)
{
    return ((self->attr.mq_flags & O_NONBLOCK) == 0);
}

static inline int
__mq_setblocking(MessageQueue *self, int blocking)
{
    struct mq_attr attr = { 0 };

    if (blocking != __mq_getblocking(self)) {
        attr.mq_flags = (blocking) ? 0 : O_NONBLOCK;
        if (mq_setattr(self->mqd, &attr, NULL)) {
            _PyErr_SetFromErrno();
            return -1;
        }
        self->attr.mq_flags = attr.mq_flags;
    }
    return 0;
}


/* -------------------------------------------------------------------------- */

static inline int
__mq_send(MessageQueue *self, const char *buf, Py_ssize_t size, unsigned int priority)
{
    int res = -1;

    Py_BEGIN_ALLOW_THREADS
    res = mq_send(self->mqd, buf, size, priority);
    Py_END_ALLOW_THREADS
    return res;
}


static inline Py_ssize_t
__mq_receive(MessageQueue *self)
{
    Py_ssize_t size = -1;

    Py_BEGIN_ALLOW_THREADS
    size = mq_receive(self->mqd, self->msg, self->attr.mq_msgsize, NULL);
    Py_END_ALLOW_THREADS
    return size;
}


/* MessageQueue_Type -------------------------------------------------------- */

/* MessageQueue_Type.tp_traverse */
static int
MessageQueue_tp_traverse(MessageQueue *self, visitproc visit, void *arg)
{
    Py_VISIT(self->callback);
    Py_VISIT(self->name);
    return 0;
}


/* MessageQueue_Type.tp_clear */
static int
MessageQueue_tp_clear(MessageQueue *self)
{
    Py_CLEAR(self->callback);
    Py_CLEAR(self->name);
    return 0;
}


/* MessageQueue_Type.tp_dealloc */
static void
MessageQueue_tp_dealloc(MessageQueue *self)
{
    if (PyObject_CallFinalizerFromDealloc((PyObject *)self)) {
        return;
    }
    PyObject_GC_UnTrack(self);
    if (self->msg) {
        PyObject_Free(self->msg);
        self->msg = NULL;
    }
    MessageQueue_tp_clear(self);
    PyObject_GC_Del(self);
}


/* MessageQueue_Type.tp_repr */
static PyObject *
MessageQueue_tp_repr(MessageQueue *self)
{
    PyObject *result = NULL, *name = NULL;

    if ((name = PyUnicode_DecodeFSDefault(PyBytes_AS_STRING(self->name)))) {
        result = PyUnicode_FromFormat(
            "<%s('%U', %d, mode=%u, maxmsg=%ld, msgsize=%ld)>",
            Py_TYPE(self)->tp_name, name, self->flags, self->mode,
            self->attr.mq_maxmsg, self->attr.mq_msgsize
        );
        Py_DECREF(name);
    }
    return result;
}


/* len() */
static Py_ssize_t
MessageQueue_sq_length(MessageQueue *self)
{
    if (mq_getattr(self->mqd, &self->attr)) {
        _PyErr_SetFromErrno();
        return -1;
    }
    return self->attr.mq_curmsgs;
}


/* MessageQueue_Type.tp_as_sequence */
static PySequenceMethods MessageQueue_tp_as_sequence = {
    .sq_length = (lenfunc)MessageQueue_sq_length,
};


/* MessageQueue.close() */
PyDoc_STRVAR(MessageQueue_close_doc,
"close()");

static PyObject *
MessageQueue_close(MessageQueue *self)
{
    return (__mq_close(self)) ? NULL : __Py_INCREF(Py_None);
}


/* MessageQueue.fileno() */
PyDoc_STRVAR(MessageQueue_fileno_doc,
"fileno() -> int\n\
Returns the underlying file descriptor.");

static PyObject *
MessageQueue_fileno(MessageQueue *self)
{
    return PyLong_FromLong(self->mqd);
}


/* MessageQueue.send(msg[, priority]) */
PyDoc_STRVAR(MessageQueue_send_doc,
"send(msg[, priority]) -> int\n\
Sends 1 message. Returns the number of bytes sent.");

static PyObject *
MessageQueue_send(MessageQueue *self, PyObject *args)
{
    Py_buffer msg;
    unsigned int priority = 0;
    Py_ssize_t size = 0;

    if (!PyArg_ParseTuple(args, "y*|I:send", &msg, &priority)) {
        return NULL;
    }
    size = Py_MIN(msg.len, self->attr.mq_msgsize);
    if (__mq_send(self, msg.buf, size, priority)) {
        PyBuffer_Release(&msg);
        return _PyErr_SetFromErrno();
    }
    PyBuffer_Release(&msg);
    return PyLong_FromSsize_t(size);
}


/* MessageQueue.sendall(msg[, priority]) */
PyDoc_STRVAR(MessageQueue_sendall_doc,
"sendall(msg[, priority])\n\
Sends 1 message. This calls send() repeatedly until all data is sent.");

static PyObject *
MessageQueue_sendall(MessageQueue *self, PyObject *args)
{
    Py_buffer msg;
    unsigned int priority = 0;
    Py_ssize_t size = 0;
    const char *buf = NULL;
    Py_ssize_t len = 0;

    if (!PyArg_ParseTuple(args, "y*|I:sendall", &msg, &priority)) {
        return NULL;
    }
    buf = msg.buf;
    len = msg.len;
    do {
        size = Py_MIN(len, self->attr.mq_msgsize);
        if (__mq_send(self, buf, size, priority)) {
            PyBuffer_Release(&msg);
            return _PyErr_SetFromErrno();
        }
        buf += size;
        len -= size;
    } while (len > 0);
    PyBuffer_Release(&msg);
    Py_RETURN_NONE;
}


/* MessageQueue.receive() */
PyDoc_STRVAR(MessageQueue_receive_doc,
"receive() -> bytes\n\
Receives 1 message.");

static PyObject *
MessageQueue_receive(MessageQueue *self)
{
    Py_ssize_t size = -1;

    if ((size = __mq_receive(self)) < 0) {
        return _PyErr_SetFromErrno();
    }
    return PyBytes_FromStringAndSize(self->msg, size);
}


/* -------------------------------------------------------------------------- */

static void
__mq_callback(union sigval sv)
{
    PyGILState_STATE gstate = PyGILState_Ensure();
    MessageQueue *self = (MessageQueue *)sv.sival_ptr;
    PyObject *result = NULL;

    if ((result = PyObject_CallFunctionObjArgs(self->callback, self, NULL))) {
        Py_DECREF(result);
    }
    else {
        PyErr_WriteUnraisable(self->callback);
    }
    Py_CLEAR(self->callback);
    PyGILState_Release(gstate);
}


/* -------------------------------------------------------------------------- */

/* MessageQueue.notify([callback]) */
PyDoc_STRVAR(MessageQueue_notify_doc,
"notify([callback])\n\
Register or unregister for notification.");

static PyObject *
MessageQueue_notify(MessageQueue *self, PyObject *args)
{
    struct sigevent sev = { .sigev_notify = -1 };
    struct sigevent *sevp = NULL;
    PyObject *callback = NULL;

    if (!PyArg_ParseTuple(args, "|O:notify", &callback)) {
        return NULL;
    }
    if (callback) {
        if (callback == Py_None) {
            sev.sigev_notify = SIGEV_NONE;
        }
        else if (PyLong_Check(callback)) {
            int signum = _PyLong_AsInt(callback);
            if (signum == -1 && PyErr_Occurred()) {
                return NULL;
            }
            if (signum < 1 || signum >= NSIG) {
                PyErr_SetString(PyExc_ValueError, "signal number out of range");
                return NULL;
            }
            sev.sigev_notify = SIGEV_SIGNAL;
            sev.sigev_signo = signum;
        }
        else if (PyCallable_Check(callback)) {
            sev.sigev_notify = SIGEV_THREAD;
            sev.sigev_notify_function = __mq_callback;
            sev.sigev_notify_attributes = NULL;
            sev.sigev_value.sival_ptr = self;
        }
        else {
            PyErr_SetString(
                PyExc_TypeError,
                "a callable, a signal number or None is required"
            );
            return NULL;
        }
        sevp = &sev;
    }
    if (mq_notify(self->mqd, sevp)) {
        return _PyErr_SetFromErrno();
    }
    if (sev.sigev_notify == SIGEV_THREAD) {
        _Py_SET_MEMBER(self->callback, callback);
    }
    else {
        Py_CLEAR(self->callback);
    }
    Py_RETURN_NONE;
}


/* -------------------------------------------------------------------------- */

static inline int
__buf_exported(PyByteArrayObject *buf)
{
    if (buf->ob_exports > 0) {
        PyErr_SetString(
            PyExc_BufferError,
            "Existing exports of data: object cannot be re-sized"
        );
        return -1;
    }
    return 0;
}


static inline void
__buf_shrink(PyByteArrayObject *buf, Py_ssize_t size)
{
    // XXX: very bad shortcut ¯\_(ツ)_/¯
    Py_SIZE(buf) = size;
    buf->ob_start[size] = '\0';
}


static inline int
__buf_realloc(PyByteArrayObject *buf, Py_ssize_t nalloc)
{
    Py_ssize_t alloc = 0;
    void *bytes = NULL;

    if (buf->ob_alloc < nalloc) {
        alloc = Py_MAX(nalloc, (buf->ob_alloc << 1));
        if (!(bytes = PyObject_Realloc(buf->ob_bytes, alloc))) {
            return -1;
        }
        buf->ob_start = buf->ob_bytes = bytes;
        buf->ob_alloc = alloc;
    }
    return 0;
}


static inline int
__buf_resize(PyByteArrayObject *buf, size_t size)
{
    if ((size >= PY_SSIZE_T_MAX) || __buf_realloc(buf, (size + 1))) {
        PyErr_NoMemory();
        return -1;
    }
    return 0;
}


static inline int
__buf_grow(PyByteArrayObject *buf, const char *bytes, size_t size)
{
    size_t start = Py_SIZE(buf), nsize = start + size;

    memcpy((buf->ob_bytes + start), bytes, size);
    Py_SIZE(buf) = nsize;
    buf->ob_bytes[nsize] = '\0';
    return 0;
}


/* -------------------------------------------------------------------------- */

/* MessageQueue.fill(buf[, priority]) */
PyDoc_STRVAR(MessageQueue_fill_doc,
"fill(buf[, priority])\n\
Fills the queue with messages from buf.");

static PyObject *
MessageQueue_fill(MessageQueue *self, PyObject *args)
{
    PyByteArrayObject *buf = NULL;
    unsigned int priority = 0;
    Py_ssize_t len, size = 0;

    if (!PyArg_ParseTuple(args, "Y|I:fill", &buf, &priority) ||
        __buf_exported(buf)) {
        return NULL;
    }
    while ((len = Py_SIZE(buf)) > 0) {
        size = Py_MIN(len, self->attr.mq_msgsize);
        if (__mq_send(self, buf->ob_start, size, priority)) {
            return _PyErr_SetFromErrno();
        }
        // XXX: very bad shortcut ¯\_(ツ)_/¯
        buf->ob_start += size;
        __buf_shrink(buf, (len - size));
    }
    Py_RETURN_NONE;
}


/* MessageQueue.drain(buf) */
PyDoc_STRVAR(MessageQueue_drain_doc,
"drain(buf) -> bool\n\
Drains all messages from the queue into buf.\n\
Stops when receiving an empty message.\n\
Returns whether the last message received was empty.");

static PyObject *
MessageQueue_drain(MessageQueue *self, PyObject *args)
{
    PyByteArrayObject *buf = NULL;
    long i, len = 0;
    Py_ssize_t size = -1;

    if (!PyArg_ParseTuple(args, "Y:drain", &buf) ||
        __buf_exported(buf)) {
        return NULL;
    }
    if (mq_getattr(self->mqd, &self->attr)) {
        return _PyErr_SetFromErrno();
    }
    len = self->attr.mq_curmsgs ? self->attr.mq_curmsgs : 1;
    for (i = 0; i < len; ++i) {
        if ((size = __mq_receive(self)) < 0) {
            return _PyErr_SetFromErrno();
        }
        if (!size) {
            break;
        }
        if ((i == 0 && __buf_resize(buf, (len * self->attr.mq_msgsize))) ||
            __buf_grow(buf, self->msg, size)) {
            return NULL;
        }
    }
    return PyBool_FromLong((size == 0));
}


/* MessageQueue_Type.tp_methods */
static PyMethodDef MessageQueue_tp_methods[] = {
    {
        "close", (PyCFunction)MessageQueue_close,
        METH_NOARGS, MessageQueue_close_doc
    },
    {
        "fileno", (PyCFunction)MessageQueue_fileno,
        METH_NOARGS, MessageQueue_fileno_doc
    },
    {
        "send", (PyCFunction)MessageQueue_send,
        METH_VARARGS, MessageQueue_send_doc
    },
    {
        "sendall", (PyCFunction)MessageQueue_sendall,
        METH_VARARGS, MessageQueue_sendall_doc
    },
    {
        "receive", (PyCFunction)MessageQueue_receive,
        METH_NOARGS, MessageQueue_receive_doc
    },
    {
        "notify", (PyCFunction)MessageQueue_notify,
        METH_VARARGS, MessageQueue_notify_doc
    },
    {
        "fill", (PyCFunction)MessageQueue_fill,
        METH_VARARGS, MessageQueue_fill_doc
    },
    {
        "drain", (PyCFunction)MessageQueue_drain,
        METH_VARARGS, MessageQueue_drain_doc
    },
    {NULL}  /* Sentinel */
};


/* MessageQueue_Type.tp_members */
static PyMemberDef MessageQueue_tp_members[] = {
    {
        "flags", T_INT, offsetof(MessageQueue, flags),
        READONLY, NULL
    },
    {
        "mode", T_UINT, offsetof(MessageQueue, mode),
        READONLY, NULL
    },
    {
        "maxmsg", T_LONG, offsetof(MessageQueue, attr.mq_maxmsg),
        READONLY, NULL
    },
    {
        "msgsize", T_LONG, offsetof(MessageQueue, attr.mq_msgsize),
        READONLY, NULL
    },
    {NULL}  /* Sentinel */
};


/* MessageQueue.name */
static PyObject *
MessageQueue_name_get(MessageQueue *self, void *closure)
{
    return PyUnicode_DecodeFSDefault(PyBytes_AS_STRING(self->name));
}


/* MessageQueue.closed */
static PyObject *
MessageQueue_closed_get(MessageQueue *self, void *closure)
{
    return PyBool_FromLong((self->mqd == -1));
}


/* MessageQueue.blocking */
static PyObject *
MessageQueue_blocking_get(MessageQueue *self, void *closure)
{
    return PyBool_FromLong(__mq_getblocking(self));
}

static int
MessageQueue_blocking_set(MessageQueue *self, PyObject *value, void *closure)
{
    int blocking = -1;

    _Py_PROTECTED_ATTRIBUTE(value, -1);
    if ((blocking = PyObject_IsTrue(value)) < 0) {
        return -1;
    }
    return __mq_setblocking(self, blocking);
}


/* MessageQueue_Type.tp_getsets */
static PyGetSetDef MessageQueue_tp_getsets[] = {
    {
        "name", (getter)MessageQueue_name_get,
        _Py_READONLY_ATTRIBUTE, NULL, NULL
    },
    {
        "closed", (getter)MessageQueue_closed_get,
        _Py_READONLY_ATTRIBUTE, NULL, NULL
    },
    {
        "blocking", (getter)MessageQueue_blocking_get,
        (setter)MessageQueue_blocking_set, NULL, NULL
    },
    {NULL}  /* Sentinel */
};


/* MessageQueue_Type.tp_new */
static PyObject *
MessageQueue_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    MessageQueue *self = NULL;

    if ((self = __mq_new(type)) && __mq_init(self, args, kwargs)) {
        Py_CLEAR(self);
    }
    return (PyObject *)self;
}


/* MessageQueue_Type.tp_finalize */
static void
MessageQueue_tp_finalize(MessageQueue *self)
{
    PyObject *exc_type, *exc_value, *exc_traceback;

    PyErr_Fetch(&exc_type, &exc_value, &exc_traceback);
    if (__mq_close(self)) {
        PyErr_WriteUnraisable((PyObject *)self);
    }
    PyErr_Restore(exc_type, exc_value, exc_traceback);
}


static PyTypeObject MessageQueue_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "mood.mqueue.MessageQueue",
    .tp_basicsize = sizeof(MessageQueue),
    .tp_dealloc = (destructor)MessageQueue_tp_dealloc,
    .tp_repr = (reprfunc)MessageQueue_tp_repr,
    .tp_as_sequence = &MessageQueue_tp_as_sequence,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_HAVE_FINALIZE,
    .tp_doc = "MessageQueue(name, flags[, mode=0o600, maxmsg=-1, msgsize=-1])",
    .tp_traverse = (traverseproc)MessageQueue_tp_traverse,
    .tp_clear = (inquiry)MessageQueue_tp_clear,
    .tp_methods = MessageQueue_tp_methods,
    .tp_members = MessageQueue_tp_members,
    .tp_getset = MessageQueue_tp_getsets,
    .tp_new = MessageQueue_tp_new,
    .tp_finalize = (destructor)MessageQueue_tp_finalize,
};


/* --------------------------------------------------------------------------
   module
   -------------------------------------------------------------------------- */

/* mqueue_def */
static PyModuleDef mqueue_def = {
    PyModuleDef_HEAD_INIT,
    .m_name = "mqueue",
    .m_doc = "Python POSIX message queues interface (Linux only)",
    .m_size = sizeof(module_state),
};


/* get module state */
static module_state *
_module_get_state(void)
{
    return (module_state *)_PyModuleDef_GetState(&mqueue_def);
}


static inline int
_module_state_init(PyObject *module)
{
    module_state *state = NULL;

    if (
        !(state = _PyModule_GetState(module)) ||
        _mqueue_get_limit(MQUEUE_DEFAULT_MAXMSG, &state->default_maxmsg) ||
        _mqueue_get_limit(MQUEUE_DEFAULT_MSGSIZE, &state->default_msgsize)
    ) {
        return -1;
    }
    state->min_maxmsg = 1;
    state->min_msgsize = 8;
    return 0;
}


static inline int
_module_init(PyObject *module)
{
    if (
        _module_state_init(module) ||
        PyModule_AddStringConstant(module, "__version__", PKG_VERSION) ||
        _PyModule_AddType(module, "MessageQueue", &MessageQueue_Type)
    ) {
        return -1;
    }
    return 0;
}


/* module initialization */
PyMODINIT_FUNC
PyInit_mqueue(void)
{
    PyObject *module = NULL;

    if ((module = PyState_FindModule(&mqueue_def))) {
        Py_INCREF(module);
    }
    else if ((module = PyModule_Create(&mqueue_def)) && _module_init(module)) {
        Py_CLEAR(module);
    }
    return module;
}
