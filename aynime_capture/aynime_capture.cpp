// #include <Python.h>
#include <pybind11/pybind11.h>

namespace py = pybind11;

/*
 * Implements an example function.
 */
PyDoc_STRVAR(hoge_doc, "example(obj, number)\
\
Example function");

PyObject *hoge(PyObject *self, PyObject *args, PyObject *kwargs) {
    /* Shared references that do not need Py_DECREF before returning. */
    PyObject *obj = NULL;
    int number = 0;

    /* Parse positional and keyword arguments */
    static char* keywords[] = { "obj", "number", NULL };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Oi", keywords, &obj, &number)) {
        return NULL;
    }

    /* Function implementation starts here */

    if (number < 0) {
        PyErr_SetObject(PyExc_ValueError, obj);
        return NULL;    /* return NULL indicates error */
    }

    Py_RETURN_NONE;
}

PYBIND11_MODULE(aynime_capture, m) {

    m.def(
        "hoge",
        &hoge,
        R"pbdoc(dummy finction for test)pbdoc"
   );

#if defined(VERSION_INFO)
    m.attr("__version__") = VERSION_INFO;
#else
    m.attr("__version__") = "dev";
#endif

}
