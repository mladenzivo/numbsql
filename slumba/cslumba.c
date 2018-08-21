#include "Python.h"
#include "sqlite3.h"

#define UNUSED(x) (void)(x)

/* Assume the pysqlite_Connection object's first non-PyObject member is the
 * sqlite3 database */
typedef struct
{
  PyObject_HEAD sqlite3* db;
} Connection;

typedef void (*scalarfunc)(sqlite3_context* ctx,
                           int narg,
                           sqlite3_value** arguments);
typedef void (*stepfunc)(sqlite3_context* ctx,
                         int narg,
                         sqlite3_value** arguments);
typedef void (*finalizefunc)(sqlite3_context* ctx);
typedef void (*valuefunc)(sqlite3_context* ctx);
typedef void (*inversefunc)(sqlite3_context* ctx,
                            int narg,
                            sqlite3_value** arguments);

static int
check_function_pointer_address(Py_ssize_t address, const char* name)
{
  return address > 0 ||
         PyErr_Format(
           PyExc_ValueError,
           "%s function pointer address must be greater than 0, got %zi",
           name,
           address);
}

static const int DETERMINISM[] = {0, SQLITE_DETERMINISTIC};

static PyObject*
register_scalar_function(PyObject* self, PyObject* args)
{
  UNUSED(self);
  PyObject* con = NULL;
  const char* name = NULL;
  int narg;
  Py_ssize_t address;
  int deterministic;

  if (!PyArg_ParseTuple(
        args, "Oyinp", &con, &name, &narg, &address, &deterministic)) {
    return NULL;
  }

  Py_XINCREF(con);

  if (narg < -1) {
    PyErr_SetString(
      PyExc_ValueError,
      "narg < -1, must be between -1 and SQLITE_LIMIT_FUNCTION_ARG");
    goto error;
  }

  if (narg > SQLITE_LIMIT_FUNCTION_ARG) {
    PyErr_SetString(PyExc_ValueError, "narg > SQLITE_LIMIT_FUNCTION_ARG");
    goto error;
  }

  if (!check_function_pointer_address(address, "scalar")) {
    goto error;
  }

  sqlite3* db = ((Connection*)con)->db;

  int result = sqlite3_create_function(
    db,
    name,
    narg,
    SQLITE_UTF8 | DETERMINISM[deterministic],
    NULL,
    (scalarfunc)address,
    NULL,
    NULL);

  if (result != SQLITE_OK) {
    PyErr_SetString(PyExc_RuntimeError, sqlite3_errmsg(db));
    goto error;
  }

  Py_XDECREF(con);
  Py_RETURN_NONE;

error:
  Py_XDECREF(con);
  return NULL;
}

static PyObject*
register_aggregate_function(PyObject* self, PyObject* args)
{
  UNUSED(self);
  PyObject* con = NULL;
  const char* name = NULL;
  int narg;

  /* step and finalize are function pointer addresses */
  Py_ssize_t step;
  Py_ssize_t finalize;

  int deterministic;

  if (!PyArg_ParseTuple(
        args, "Oyinnp", &con, &name, &narg, &step, &finalize, &deterministic)) {
    return NULL;
  }

  Py_XINCREF(con);

  if (narg < -1) {
    PyErr_SetString(
      PyExc_ValueError,
      "narg < -1, must be between -1 and SQLITE_LIMIT_FUNCTION_ARG");
    goto error;
  }

  if (narg > SQLITE_LIMIT_FUNCTION_ARG) {
    PyErr_SetString(PyExc_ValueError, "narg > SQLITE_LIMIT_FUNCTION_ARG");
    goto error;
  }

  if (!check_function_pointer_address(step, "step")) {
    goto error;
  }

  if (!check_function_pointer_address(finalize, "finalize")) {
    goto error;
  }

  sqlite3* db = ((Connection*)con)->db;

  int result = sqlite3_create_function(
    db,
    name,
    narg,
    SQLITE_UTF8 | DETERMINISM[deterministic],
    NULL,
    NULL,
    (stepfunc)step,
    (finalizefunc)finalize);

  if (result != SQLITE_OK) {
    PyErr_SetString(PyExc_RuntimeError, sqlite3_errmsg(db));
    goto error;
  }

  Py_XDECREF(con);
  Py_RETURN_NONE;

error:
  Py_XDECREF(con);
  return NULL;
}

#if SQLITE_VERSION_NUMBER >= 3025000
static PyObject*
register_window_function(PyObject* self, PyObject* args)
{
  UNUSED(self);
  PyObject* con = NULL;
  const char* name = NULL;
  int narg;

  /* step, finalize, value and inverse are function pointer addresses */
  Py_ssize_t step;
  Py_ssize_t finalize;
  Py_ssize_t value;
  Py_ssize_t inverse;

  int deterministic;

  if (!PyArg_ParseTuple(args,
                        "Oyinnnnp",
                        &con,
                        &name,
                        &narg,
                        &step,
                        &finalize,
                        &value,
                        &inverse,
                        &deterministic)) {
    return NULL;
  }

  Py_XINCREF(con);

  if (narg < -1) {
    PyErr_SetString(
      PyExc_ValueError,
      "narg < -1, must be between -1 and SQLITE_LIMIT_FUNCTION_ARG");
    goto error;
  }

  if (narg > SQLITE_LIMIT_FUNCTION_ARG) {
    PyErr_SetString(PyExc_ValueError, "narg > SQLITE_LIMIT_FUNCTION_ARG");
    goto error;
  }

  if (!check_function_pointer_address(step, "step")) {
    goto error;
  }

  if (!check_function_pointer_address(finalize, "finalize")) {
    goto error;
  }

  if (!check_function_pointer_address(value, "value")) {
    goto error;
  }

  if (!check_function_pointer_address(inverse, "inverse")) {
    goto error;
  }

  sqlite3* db = ((Connection*)con)->db;

  int result = sqlite3_create_window_function(
    db,
    name,
    narg,
    SQLITE_UTF8 | DETERMINISM[deterministic],
    NULL,
    (stepfunc)step,
    (finalizefunc)finalize,
    (valuefunc)value,
    (inversefunc)inverse,
    NULL);

  if (result != SQLITE_OK) {
    PyErr_SetString(PyExc_RuntimeError, sqlite3_errmsg(db));
    goto error;
  }

  Py_XDECREF(con);
  Py_RETURN_NONE;

error:
  Py_XDECREF(con);
  return NULL;
}
#else
static PyObject*
register_window_function(PyObject* self, PyObject* args)
{
  UNUSED(self);
  UNUSED(args);
  return PyErr_Format(PyExc_RuntimeError,
                      "SQLite version %s does not support window functions. "
                      "Window functions were added in 3.25.0",
                      SQLITE_VERSION);
}
#endif

static PyMethodDef cslumba_methods[] = {
  { "register_scalar_function",
    (PyCFunction)register_scalar_function,
    METH_VARARGS,
    "Register a numba generated SQLite user-defined function" },
  { "register_aggregate_function",
    (PyCFunction)register_aggregate_function,
    METH_VARARGS,
    "Register a numba generated SQLite user-defined aggregate function" },
  { "register_window_function",
    (PyCFunction)register_window_function,
    METH_VARARGS,
    "Register a numba generated SQLite user-defined window function" },
  { NULL, NULL, 0, NULL } /* sentinel */
};

static struct PyModuleDef cslumbamodule = {
  PyModuleDef_HEAD_INIT, "cslumba", NULL, -1, cslumba_methods,
};

PyMODINIT_FUNC
PyInit_cslumba(void)
{
  PyObject* module = PyModule_Create(&cslumbamodule);
  if (module == NULL) {
    return NULL;
  }

  if (PyModule_AddStringMacro(module, SQLITE_VERSION) == -1) {
    return PyErr_Format(
      PyExc_RuntimeError,
      "Unable to add SQLITE_VERSION string constant with value %s",
      SQLITE_VERSION);
  }
  return module;
}
