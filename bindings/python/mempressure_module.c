#define PY_SSIZE_T_CLEAN

#include "mempressure.h"

#include <Python.h>
#include <errno.h>

static PyObject *g_handles = NULL;
static int g_started = 0;

static void py_trampoline(mp_level_t level, void *userdata) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject *cb = (PyObject *)userdata;
    PyObject *arg = PyLong_FromLong((long)level);
    if (arg) {
        PyObject *result = PyObject_CallFunctionObjArgs(cb, arg, NULL);
        if (!result) {
            PyErr_Print();
        }
        Py_XDECREF(result);
        Py_DECREF(arg);
    }
    PyGILState_Release(gstate);
}

static PyObject *py_start(PyObject *self, PyObject *args, PyObject *kwds) {
    (void)self;
    double low = 5.0, moderate = 15.0, critical = 40.0, interval = 0.5;
    int hysteresis = 2;
    const char *psi_path = NULL;
    static char *kwlist[] = {"low", "moderate", "critical", "interval", "hysteresis", "psi_path", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ddddis", kwlist, &low, &moderate, &critical,
                                     &interval, &hysteresis, &psi_path)) {
        return NULL;
    }
    mp_config_t cfg = {0};
    cfg.low_threshold = low;
    cfg.moderate_threshold = moderate;
    cfg.critical_threshold = critical;
    cfg.poll_interval_sec = interval;
    cfg.hysteresis = hysteresis;
    cfg.psi_path = psi_path;
    int rc = mp_init(&cfg);
    if (rc == -EALREADY) {
        Py_RETURN_TRUE;
    }
    if (rc != 0) {
        PyErr_Format(PyExc_RuntimeError, "mp_init failed: %d", rc);
        return NULL;
    }
    g_started = 1;
    Py_RETURN_TRUE;
}

static PyObject *py_stop(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    mp_shutdown();
    g_started = 0;
    Py_RETURN_NONE;
}

static PyObject *py_current_level(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    return PyLong_FromLong((long)mp_current_level());
}

static PyObject *py_level_name(PyObject *self, PyObject *args) {
    (void)self;
    int level = 0;
    if (!PyArg_ParseTuple(args, "i", &level)) {
        return NULL;
    }
    return PyUnicode_FromString(mp_level_name((mp_level_t)level));
}

static PyObject *py_psi(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    mp_psi_t psi = {0};
    int rc = mp_psi(&psi);
    if (rc != 0) {
        PyErr_Format(PyExc_RuntimeError, "mp_psi failed: %d (not started?)", rc);
        return NULL;
    }
    return Py_BuildValue("{s:d,s:d,s:d,s:d,s:d,s:d}", "some_avg10", psi.some_avg10,
                         "some_avg60", psi.some_avg60, "some_avg300", psi.some_avg300,
                         "full_avg10", psi.full_avg10, "full_avg60", psi.full_avg60,
                         "full_avg300", psi.full_avg300);
}

static PyObject *py_subscribe(PyObject *self, PyObject *callback) {
    (void)self;
    if (!PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "callback must be callable");
        return NULL;
    }
    if (!g_started) {
        int rc = mp_init(NULL);
        if (rc != 0 && rc != -EALREADY) {
            PyErr_Format(PyExc_RuntimeError, "mp_init failed: %d", rc);
            return NULL;
        }
        g_started = 1;
    }
    Py_INCREF(callback);
    int handle = mp_subscribe(py_trampoline, callback);
    if (handle < 0) {
        Py_DECREF(callback);
        PyErr_Format(PyExc_RuntimeError, "mp_subscribe failed: %d", handle);
        return NULL;
    }
    PyObject *key = PyLong_FromLong(handle);
    PyDict_SetItem(g_handles, key, callback);
    Py_DECREF(key);
    Py_DECREF(callback);  // dict owns the strong ref now
    return PyLong_FromLong(handle);
}

static PyObject *py_unsubscribe(PyObject *self, PyObject *args) {
    (void)self;
    int handle = 0;
    if (!PyArg_ParseTuple(args, "i", &handle)) {
        return NULL;
    }
    int rc = mp_unsubscribe(handle);
    if (rc == -ENOENT) {
        PyErr_SetString(PyExc_ValueError, "unknown handle");
        return NULL;
    }
    PyObject *key = PyLong_FromLong(handle);
    PyObject *cb = PyDict_GetItemWithError(g_handles, key);
    if (cb) {
        Py_INCREF(cb);
        PyDict_DelItem(g_handles, key);
        Py_DECREF(cb);
    } else if (PyErr_Occurred()) {
        PyErr_Clear();
    }
    Py_DECREF(key);
    Py_RETURN_NONE;
}

static PyMethodDef methods[] = {
    {"start", (PyCFunction)py_start, METH_VARARGS | METH_KEYWORDS,
     "start(low=5.0, moderate=15.0, critical=40.0, interval=0.5, hysteresis=2, psi_path=None)\n\n"
     "Start the pressure monitor. Re-starting with a live monitor is a no-op."},
    {"stop", py_stop, METH_NOARGS, "Stop the monitor and release everything."},
    {"current_level", py_current_level, METH_NOARGS, "Current pressure level (0..3)."},
    {"level_name", py_level_name, METH_VARARGS, "Name of a level code."},
    {"psi", py_psi, METH_NOARGS, "Last observed PSI values as a dict."},
    {"subscribe", py_subscribe, METH_O, "subscribe(callback) -> handle; fires on level change."},
    {"unsubscribe", py_unsubscribe, METH_VARARGS, "unsubscribe(handle)."},
    {NULL, NULL, 0, NULL},
};

static void free_module_state(void *module) {
    (void)module;
    Py_CLEAR(g_handles);
}

static struct PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT,
    "mempressure",
    "Memory-pressure events from the kernel's PSI, an onTrimMemory for Linux.",
    -1,
    methods,
    NULL,
    NULL,
    NULL,
    free_module_state,
};

PyMODINIT_FUNC PyInit_mempressure(void) {
    g_handles = PyDict_New();
    if (!g_handles) {
        return NULL;
    }
    PyObject *module = PyModule_Create(&module_def);
    if (!module) {
        Py_DECREF(g_handles);
        return NULL;
    }
    PyModule_AddStringConstant(module, "__version__", MP_VERSION);
    PyObject *levels = PyDict_New();
    PyDict_SetItemString(levels, "NONE", PyLong_FromLong(MP_LEVEL_NONE));
    PyDict_SetItemString(levels, "LOW", PyLong_FromLong(MP_LEVEL_LOW));
    PyDict_SetItemString(levels, "MODERATE", PyLong_FromLong(MP_LEVEL_MODERATE));
    PyDict_SetItemString(levels, "CRITICAL", PyLong_FromLong(MP_LEVEL_CRITICAL));
    PyModule_AddObject(module, "LEVELS", levels);
    return module;
}
