/*
#
# Copyright © 2017 Malek Hadj-Ali
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

#define _PY_INLINE_HELPERS
#include "helpers/helpers.c"

#include <mqueue.h>


#define S_IMODE 07777


/* forward declarations */
static PyModuleDef mqueue_def;


/* ----------------------------------------------------------------------------
 module state
 ---------------------------------------------------------------------------- */

typedef struct {
    Py_ssize_t default_maxmsg;
    Py_ssize_t min_maxmsg;
    Py_ssize_t default_msgsize;
    Py_ssize_t min_msgsize;
} mqueue_state;

#define mqueue_getstate() (mqueue_state *)_PyModuleDef_GetState(&mqueue_def)


static int
_get_mqueue_limit(const char *filename, Py_ssize_t *value)
{
    FILE *stream = NULL;
    int res = -1;

    if (!(stream = fopen(filename, "r"))) {
        _PyErr_SetFromErrnoWithFilename(filename);
        return -1;
    }
    errno = 0;
    if ((res = ((fscanf(stream, "%zd", value) != 1) || errno) ? -1 : 0)) {
        if (errno) {
            _PyErr_SetFromErrnoWithFilename(filename);
        }
        else {
            PyErr_Format(PyExc_EOFError,
                         "No number found in file: '%s'", filename);
        }
    }
    if (fclose(stream)) {
        _PyErr_SetFromErrnoWithFilenameAndChain(filename);
        return -1;
    }
    return res;
}


static int
_init_state(PyObject *module)
{
    mqueue_state *state = NULL;

    if (
        !(state = _PyModule_GetState(module)) ||
        _get_mqueue_limit("/proc/sys/fs/mqueue/msg_default",
                          &state->default_maxmsg) ||
        _get_mqueue_limit("/proc/sys/fs/mqueue/msgsize_default",
                          &state->default_msgsize)
       ) {
        return -1;
    }
    state->min_maxmsg = 1;
    state->min_msgsize = _Py_MIN_ALLOC;
    return 0;
}


/* ----------------------------------------------------------------------------
 MQType helpers
 ---------------------------------------------------------------------------- */

/* MQ */
typedef struct {
    PyObject_HEAD
    PyObject *name;
    PyObject *bytes;
    char *msg;
    int owner;
    Py_ssize_t msgsize;
    Py_ssize_t maxmsg;
    mode_t mode;
    int flags;
    mqd_t mqd;
} MQ;


static int
_MQ_Close(MQ *self)
{
    int res = 0;
    char *filename = PyBytes_AS_STRING(self->bytes);

    if (self->mqd != -1) {
        if ((res = mq_close(self->mqd))) {
            _PyErr_SetFromErrno();
        }
        else if (self->owner && (res = mq_unlink(filename))) {
            _PyErr_SetFromErrnoWithFilename(filename);
        }
        self->mqd = -1;
    }
    return res;
}


static int
_MQ_New(MQ *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"name", "flags", "mode", "maxmsg", "msgsize", NULL};
    mqueue_state *state = NULL;
    PyObject *name = NULL, *bytes = NULL;
    char *filename = NULL;
    struct mq_attr attr = { .mq_maxmsg = -1, .mq_msgsize = -1 };
    struct stat st;

    if (!(state = mqueue_getstate()) ||
        !PyArg_ParseTupleAndKeywords(args, kwargs, "Ui|Ill:__new__", kwlist,
                                     &name, &self->flags, &self->mode,
                                     &attr.mq_maxmsg, &attr.mq_msgsize) ||
        !PyUnicode_FSConverter(name, &bytes)) {
        return -1;
    }

    /* self->name, self->bytes */
    _Py_SET_MEMBER(self->name, name);
    _Py_SET_MEMBER(self->bytes, bytes);
    Py_DECREF(bytes);

    /* self->mqd */
    if (attr.mq_maxmsg < 0) {
        attr.mq_maxmsg = state->default_maxmsg;
    }
    else if (attr.mq_maxmsg < state->min_maxmsg) {
        attr.mq_maxmsg = state->min_maxmsg;
    }

    if (attr.mq_msgsize < 0) {
        attr.mq_msgsize = state->default_msgsize;
    }
    else if (attr.mq_msgsize < state->min_msgsize) {
        attr.mq_msgsize = state->min_msgsize;
    }
    else {
        // XXX: hmm...?
        attr.mq_msgsize = ((attr.mq_msgsize + 7) & ~7);
    }

    filename = PyBytes_AS_STRING(self->bytes);
    if ((self->flags & O_CREAT)) {
        int flags = self->flags | O_EXCL;
        int saved_errno = errno;
        if (((self->mqd = mq_open(filename, flags, self->mode, &attr)) == -1)) {
            if ((flags != self->flags) && (errno == EEXIST)) {
                errno = saved_errno;
                self->mqd = mq_open(filename, self->flags, self->mode, &attr);
            }
        }
        else {
            self->owner = 1;
        }
    }
    else {
        self->mqd = mq_open(filename, self->flags, self->mode, &attr);
    }
    if (self->mqd == -1) {
        switch (errno) {
            case EACCES:
            case EEXIST:
            case ENAMETOOLONG:
            case ENOENT:
                _PyErr_SetFromErrnoWithFilename(filename);
                break;
            default:
                _PyErr_SetFromErrno();
                break;
        }
        return -1;
    }

    /* self->mode */
    memset(&st, 0, sizeof(struct stat));
    if (fstat(self->mqd, &st)) {
        _PyErr_SetFromErrno();
        return -1;
    }
    self->mode = st.st_mode;
    //self->mode = (st.st_mode & S_IMODE);

    /* self->maxmsg, self->msgsize */
    memset(&attr, 0, sizeof(struct mq_attr));
    if (mq_getattr(self->mqd, &attr)) {
        _PyErr_SetFromErrno();
        return -1;
    }
    self->maxmsg = attr.mq_maxmsg;
    self->msgsize = attr.mq_msgsize;

    /* self->msg */
    if (!(self->msg = PyObject_Malloc(self->msgsize))) {
        PyErr_NoMemory();
        return -1;
    }

    return 0;
}


static MQ *
_MQ_Alloc(PyTypeObject *type)
{
    MQ *self = NULL;

    if ((self = __PyObject_Alloc(MQ, type))) {
        self->msgsize = -1;
        self->maxmsg = -1;
        self->mode = S_IRUSR | S_IWUSR; // ReadWrite by owner;
        self->owner = 0;
        self->mqd = -1;
        PyObject_GC_Track(self);
    }
    return self;
}


/* ----------------------------------------------------------------------------
 MQType
 ---------------------------------------------------------------------------- */

/* MQType.tp_doc */
PyDoc_STRVAR(MQ_tp_doc,
"MessageQueue(name, flags[, mode=0o600, maxmsg=-1, msgsize=-1])");


/* MQType.tp_finalize */
static void
MQ_tp_finalize(MQ *self)
{
    PyObject *exc_type, *exc_value, *exc_traceback;

    PyErr_Fetch(&exc_type, &exc_value, &exc_traceback);
    if (_MQ_Close(self)) {
        PyErr_WriteUnraisable((PyObject *)self);
    }
    PyErr_Restore(exc_type, exc_value, exc_traceback);
}


/* MQType.tp_traverse */
static int
MQ_tp_traverse(MQ *self, visitproc visit, void *arg)
{
    Py_VISIT(self->bytes);
    Py_VISIT(self->name);
    return 0;
}


/* MQType.tp_clear */
static int
MQ_tp_clear(MQ *self)
{
    Py_CLEAR(self->bytes);
    Py_CLEAR(self->name);
    return 0;
}


/* MQType.tp_dealloc */
static void
MQ_tp_dealloc(MQ *self)
{
    if (PyObject_CallFinalizerFromDealloc((PyObject *)self)) {
        return;
    }
    PyObject_GC_UnTrack(self);
    if (self->msg) {
        PyObject_Free(self->msg);
        self->msg = NULL;
    }
    MQ_tp_clear(self);
    PyObject_GC_Del(self);
}


/* MQType.tp_repr */
static PyObject *
MQ_tp_repr(MQ *self)
{
    return PyUnicode_FromFormat(
        "<%s('%U', %d, mode=%u, maxmsg=%zd, msgsize=%zd)>",
        Py_TYPE(self)->tp_name, self->name, self->flags, self->mode,
        self->maxmsg, self->msgsize);
}


/* MQ.close() */
PyDoc_STRVAR(MQ_close_doc,
"close()");

static PyObject *
MQ_close(MQ *self)
{
    if (_MQ_Close(self)) {
        return NULL;
    }
    Py_RETURN_NONE;
}


/* MQ.fileno() */
PyDoc_STRVAR(MQ_fileno_doc,
"fileno() -> int\n\
Return the underlying file descriptor.");

static PyObject *
MQ_fileno(MQ *self)
{
    return PyLong_FromLong(self->mqd);
}


/* MQ.send(msg) */
PyDoc_STRVAR(MQ_send_doc,
"send(msg) -> int\n\
Send 1 message. Return the number of bytes sent.");

static PyObject *
MQ_send(MQ *self, PyObject *args)
{
    Py_buffer msg;
    Py_ssize_t size = 0;
    int res = -1;

    if (!PyArg_ParseTuple(args, "y*:send", &msg)) {
        return NULL;
    }
    size = Py_MIN(msg.len, self->msgsize);
    res = mq_send(self->mqd, msg.buf, size, 0);
    PyBuffer_Release(&msg);
    return res ? _PyErr_SetFromErrno() : PyLong_FromSsize_t(size);
}


/* MQ.recv() */
PyDoc_STRVAR(MQ_recv_doc,
"recv() -> msg\n\
Receive 1 message.");

static PyObject *
MQ_recv(MQ *self)
{
    Py_ssize_t rcvd = -1;

    if ((rcvd = mq_receive(self->mqd, self->msg, self->msgsize, NULL)) < 0) {
        return _PyErr_SetFromErrno();
    }
    return PyBytes_FromStringAndSize(self->msg, rcvd);
}


/* MQ._fill(buf) */
PyDoc_STRVAR(MQ__fill_doc,
"_fill(buf)\n\
Fill the queue with messages from buf.");

static PyObject *
MQ__fill(MQ *self, PyObject *args)
{
    Py_ssize_t size = 0, bufsize = 0;
    PyByteArrayObject *buf = NULL;

    if (!PyArg_ParseTuple(args, "Y:_fill", &buf)) {
        return NULL;
    }
    while ((bufsize = Py_SIZE(buf))) {
        size = Py_MIN(bufsize, self->msgsize);
        if (mq_send(self->mqd, PyByteArray_AS_STRING(buf), size, 0)) {
            return _PyErr_SetFromErrno();
        }
        if (__PyByteArray_Shrink(buf, size)) {
            return NULL;
        }
    }
    Py_RETURN_NONE;
}


/* MQ._drain(buf) */
PyDoc_STRVAR(MQ__drain_doc,
"_drain(buf) -> closed\n\
Drain all messages from the queue into buf.");

static PyObject *
MQ__drain(MQ *self, PyObject *args)
{
    Py_ssize_t rcvd = -1;
    PyByteArrayObject *buf = NULL;
    struct mq_attr attr = { 0 };
    long i, mq_len = 0;

    if (!PyArg_ParseTuple(args, "Y:_drain", &buf)) {
        return NULL;
    }
    if (mq_getattr(self->mqd, &attr)) {
        return _PyErr_SetFromErrno();
    }
    mq_len = attr.mq_curmsgs ? attr.mq_curmsgs : 1;
    for (i = 0; i < mq_len; ++i) {
        if ((rcvd = mq_receive(self->mqd, self->msg, self->msgsize, NULL)) < 0) {
            return _PyErr_SetFromErrno();
        }
        if (!rcvd) {
            break;
        }
        if (__PyByteArray_Grow(buf, rcvd, self->msg, self->msgsize)) {
            return NULL;
        }
    }
    return PyBool_FromLong((rcvd == 0));
}


/* MQType.tp_methods */
static PyMethodDef MQ_tp_methods[] = {
    {"close", (PyCFunction)MQ_close, METH_NOARGS, MQ_close_doc},
    {"fileno", (PyCFunction)MQ_fileno, METH_NOARGS, MQ_fileno_doc},
    {"send", (PyCFunction)MQ_send, METH_VARARGS, MQ_send_doc},
    {"recv", (PyCFunction)MQ_recv, METH_NOARGS, MQ_recv_doc},
    {"_fill", (PyCFunction)MQ__fill, METH_VARARGS, MQ__fill_doc},
    {"_drain", (PyCFunction)MQ__drain, METH_VARARGS, MQ__drain_doc},
    {NULL}  /* Sentinel */
};


/* MQType.tp_members */
static PyMemberDef MQ_tp_members[] = {
    {"name", T_OBJECT_EX, offsetof(MQ, name), READONLY, NULL},
    {"flags", T_INT, offsetof(MQ, flags), READONLY, NULL},
    {"mode", T_UINT, offsetof(MQ, mode), READONLY, NULL},
    {"maxmsg", T_PYSSIZET, offsetof(MQ, maxmsg), READONLY, NULL},
    {"msgsize", T_PYSSIZET, offsetof(MQ, msgsize), READONLY, NULL},
    {NULL}  /* Sentinel */
};


/* MQ.closed */
static PyObject *
MQ_closed_get(MQ *self, void *closure)
{
    _Py_RETURN_OBJECT(((self->mqd == -1) ? Py_True : Py_False));
}


/* MQType.tp_getsets */
static PyGetSetDef MQ_tp_getsets[] = {
    {"closed", (getter)MQ_closed_get, NULL, NULL, NULL},
    {NULL}  /* Sentinel */
};


/* MQType.tp_new */
static PyObject *
MQ_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    MQ *self = NULL;

    if ((self = _MQ_Alloc(type)) && _MQ_New(self, args, kwargs)) {
        Py_CLEAR(self);
    }
    return (PyObject *)self;
}


/* MQType */
static PyTypeObject MQType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "mood.mqueue.MessageQueue",               /*tp_name*/
    sizeof(MQ),                               /*tp_basicsize*/
    0,                                        /*tp_itemsize*/
    (destructor)MQ_tp_dealloc,                /*tp_dealloc*/
    0,                                        /*tp_print*/
    0,                                        /*tp_getattr*/
    0,                                        /*tp_setattr*/
    0,                                        /*tp_compare*/
    (reprfunc)MQ_tp_repr,                     /*tp_repr*/
    0,                                        /*tp_as_number*/
    0,                                        /*tp_as_sequence*/
    0,                                        /*tp_as_mapping*/
    0,                                        /*tp_hash */
    0,                                        /*tp_call*/
    0,                                        /*tp_str*/
    0,                                        /*tp_getattro*/
    0,                                        /*tp_setattro*/
    0,                                        /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_HAVE_FINALIZE, /*tp_flags*/
    MQ_tp_doc,                                /*tp_doc*/
    (traverseproc)MQ_tp_traverse,             /*tp_traverse*/
    (inquiry)MQ_tp_clear,                     /*tp_clear*/
    0,                                        /*tp_richcompare*/
    0,                                        /*tp_weaklistoffset*/
    0,                                        /*tp_iter*/
    0,                                        /*tp_iternext*/
    MQ_tp_methods,                            /*tp_methods*/
    MQ_tp_members,                            /*tp_members*/
    MQ_tp_getsets,                            /*tp_getsets*/
    0,                                        /*tp_base*/
    0,                                        /*tp_dict*/
    0,                                        /*tp_descr_get*/
    0,                                        /*tp_descr_set*/
    0,                                        /*tp_dictoffset*/
    0,                                        /*tp_init*/
    0,                                        /*tp_alloc*/
    MQ_tp_new,                                /*tp_new*/
    0,                                        /*tp_free*/
    0,                                        /*tp_is_gc*/
    0,                                        /*tp_bases*/
    0,                                        /*tp_mro*/
    0,                                        /*tp_cache*/
    0,                                        /*tp_subclasses*/
    0,                                        /*tp_weaklist*/
    0,                                        /*tp_del*/
    0,                                        /*tp_version_tag*/
    (destructor)MQ_tp_finalize,               /*tp_finalize*/
};


/* ----------------------------------------------------------------------------
 module
 ---------------------------------------------------------------------------- */

/* mqueue_def.m_doc */
PyDoc_STRVAR(mqueue_m_doc,
"Python POSIX message queues interface (Linux only)");


/* mqueue_def */
static PyModuleDef mqueue_def = {
    PyModuleDef_HEAD_INIT,
    "mqueue",                                 /* m_name */
    mqueue_m_doc,                             /* m_doc */
    sizeof(mqueue_state),                     /* m_size */
};


/* module initialization */
PyMODINIT_FUNC
PyInit_mqueue(void)
{
    PyObject *module = NULL;

    if ((module = PyState_FindModule(&mqueue_def))) {
        Py_INCREF(module);
    }
    else if (
             (module = PyModule_Create(&mqueue_def)) &&
             (
              _init_state(module) ||
              _PyModule_AddType(module, "MessageQueue", &MQType) ||
              PyModule_AddStringConstant(module, "__version__", PKG_VERSION)
             )
            ) {
        Py_CLEAR(module);
    }
    return module;
}
