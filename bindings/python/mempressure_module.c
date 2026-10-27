#define PY_SSIZE_T_CLEAN

#include "mempressure.h"

#include <Python.h>
#include <errno.h>
#include <string.h>

static PyObject *g_handles = NULL;
static int g_started = 0;

static PyObject *mp_error(const char *what, int rc) {
    return PyErr_Format(PyExc_RuntimeError, "%s: %s (%d)", what, strerror(-rc), rc);
}

static void py_trampoline(mp_level_t level, void *userdata) {
#if PY_VERSION_HEX >= 0x030D0000
    if (Py_IsFinalizing()) {
        return;
    }
#endif
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject *cb = (PyObject *)userdata;
    Py_INCREF(cb);
    PyObject *arg = PyLong_FromLong((long)level);
    if (arg) {
        PyObject *result = PyObject_CallFunctionObjArgs(cb, arg, NULL);
        if (!result) {
            PyErr_Print();
        }
        Py_XDECREF(result);
        Py_DECREF(arg);
    }
    Py_DECREF(cb);
    PyGILState_Release(gstate);
}

static PyObject *py_start(PyObject *self, PyObject *args, PyObject *kwds) {
    (void)self;
    double low = 5.0, moderate = 15.0, critical = 40.0, interval = 0.5;
    int hysteresis = 2;
    const char *psi_path = NULL;
    static char *kwlist[] = {"low", "moderate", "critical", "interval", "hysteresis", "psi_path", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ddddiz", kwlist, &low, &moderate, &critical,
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
        return mp_error("mp_init failed", rc);
    }
    g_started = 1;
    Py_RETURN_TRUE;
}

static PyObject *py_stop(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    int rc = 0;
    Py_BEGIN_ALLOW_THREADS
    rc = mp_shutdown();
    Py_END_ALLOW_THREADS
    if (rc == -EDEADLK) {
        PyErr_SetString(PyExc_RuntimeError, "stop() cannot be called from a pressure callback");
        return NULL;
    }
    if (g_handles) {
        PyDict_Clear(g_handles);
    }
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
        return mp_error("mp_psi failed (not started?)", rc);
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
            return mp_error("mp_init failed", rc);
        }
        g_started = 1;
    }
    Py_INCREF(callback);
    int handle = mp_subscribe(py_trampoline, callback);
    if (handle < 0) {
        Py_DECREF(callback);
        return mp_error("mp_subscribe failed", handle);
    }
    PyObject *key = PyLong_FromLong(handle);
    if (!key || PyDict_SetItem(g_handles, key, callback) != 0) {
        Py_XDECREF(key);
        Py_BEGIN_ALLOW_THREADS
        mp_unsubscribe(handle);
        Py_END_ALLOW_THREADS
        Py_DECREF(callback);
        if (!PyErr_Occurred()) {
            PyErr_SetString(PyExc_RuntimeError, "failed to register callback");
        }
        return NULL;
    }
    Py_DECREF(key);
    Py_DECREF(callback);  // dict owns the strong ref now
    PyObject *result = PyLong_FromLong(handle);
    return result;
}

static PyObject *py_unsubscribe(PyObject *self, PyObject *args) {
    (void)self;
    int handle = 0;
    if (!PyArg_ParseTuple(args, "i", &handle)) {
        return NULL;
    }
    int rc = 0;
    Py_BEGIN_ALLOW_THREADS
    rc = mp_unsubscribe(handle);
    Py_END_ALLOW_THREADS
    if (rc == -ENOENT) {
        PyErr_SetString(PyExc_ValueError, "unknown handle");
        return NULL;
    }
    if (rc != 0) {
        return mp_error("mp_unsubscribe failed", rc);
    }
    PyObject *key = PyLong_FromLong(handle);
    if (!key) {
        return NULL;
    }
    PyObject *cb = PyDict_GetItemWithError(g_handles, key);
    if (cb) {
        Py_INCREF(cb);
        if (PyDict_DelItem(g_handles, key) != 0) {
            PyErr_Clear();
        }
        Py_DECREF(cb);
    } else if (PyErr_Occurred()) {
        PyErr_Clear();
    }
    Py_DECREF(key);
    Py_RETURN_NONE;
}

static PyMethodDef methods[] = {
    {"start", (PyCFunction)(void (*)(void))py_start, METH_VARARGS | METH_KEYWORDS,
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

static int register_atexit_stop(PyObject *module) {
    PyObject *atexit_mod = PyImport_ImportModule("atexit");
    if (!atexit_mod) {
        return -1;
    }
    PyObject *stop = PyObject_GetAttrString(module, "stop");
    PyObject *res = stop ? PyObject_CallMethod(atexit_mod, "register", "O", stop) : NULL;
    Py_XDECREF(stop);
    Py_DECREF(atexit_mod);
    if (!res) {
        return -1;
    }
    Py_DECREF(res);
    return 0;
}

PyMODINIT_FUNC PyInit_mempressure(void) {
    if (!g_handles) {
        g_handles = PyDict_New();
        if (!g_handles) {
            return NULL;
        }
    }
    PyObject *module = PyModule_Create(&module_def);
    if (!module) {
        Py_CLEAR(g_handles);
        return NULL;
    }
    PyObject *levels = PyDict_New();
    if (!levels) {
        goto fail;
    }
    static const struct {
        const char *name;
        long value;
    } level_entries[] = {
        {"NONE", MP_LEVEL_NONE},
        {"LOW", MP_LEVEL_LOW},
        {"MODERATE", MP_LEVEL_MODERATE},
        {"CRITICAL", MP_LEVEL_CRITICAL},
    };
    for (size_t i = 0; i < sizeof level_entries / sizeof level_entries[0]; i++) {
        PyObject *num = PyLong_FromLong(level_entries[i].value);
        if (!num || PyDict_SetItemString(levels, level_entries[i].name, num) != 0) {
            Py_XDECREF(num);
            Py_DECREF(levels);
            goto fail;
        }
        Py_DECREF(num);
    }
    if (PyModule_AddObject(module, "LEVELS", levels) != 0) {
        Py_DECREF(levels);
        goto fail;
    }
    if (PyModule_AddStringConstant(module, "__version__", MP_VERSION) != 0) {
        goto fail;
    }
    if (register_atexit_stop(module) != 0) {
        goto fail;
    }
    return module;

fail:
    Py_DECREF(module);
    return NULL;
}
