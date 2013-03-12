/*
    py_java_thread.h

    Copyright (C) 2010, 2011, 2012  Red Hat, Inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#ifndef SATYR_PY_JAVA_THREAD_H
#define SATYR_PY_JAVA_THREAD_H

/**
 * @file
 * @brief Python bindings for Java thread.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <Python.h>
#include <structmember.h>

PyTypeObject sr_py_java_thread_type;

struct sr_py_java_thread
{
    PyObject_HEAD
    PyObject *frames;
    struct sr_java_thread *thread;
};

/* helpers */
int java_thread_prepare_linked_list(struct sr_py_java_thread *thread);
PyObject *java_frame_linked_list_to_python_list(struct sr_java_thread *thread);
int java_thread_rebuild_python_list(struct sr_py_java_thread *thread);

/* constructor */
PyObject *sr_py_java_thread_new(PyTypeObject *object,
                                PyObject *args,
                                PyObject *kwds);

/* destructor */
void sr_py_java_thread_free(PyObject *object);

/* str */
PyObject *sr_py_java_thread_str(PyObject *self);

/* getters & setters */
PyObject *sr_py_java_thread_get_name(PyObject *self, PyObject *args);
PyObject *sr_py_java_thread_set_name(PyObject *self, PyObject *args);

/* methods */
PyObject *sr_py_java_thread_dup(PyObject *self, PyObject *args);
PyObject *sr_py_java_thread_cmp(PyObject *self, PyObject *args);
PyObject *sr_py_java_thread_quality_counts(PyObject *self, PyObject *args);
PyObject *sr_py_java_thread_quality(PyObject *self, PyObject *args);
PyObject *sr_py_java_thread_format_funs(PyObject *self, PyObject *args);

#ifdef __cplusplus
}
#endif

#endif