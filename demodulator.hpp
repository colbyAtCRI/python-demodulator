#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <pybind11/cast.h>
#include <pybind11/embed.h>
#include <string>
#include <iostream>
#include <complex>
#include <vector>
#include <map>
#include <liquid/liquid.h>

namespace py = pybind11;

template<class T>
T *array_to_ptr (py::array_t<T> a)
{
    return static_cast<T*>(a.request().ptr);
}

typedef std::complex<float>    complex_t;
typedef py::array_t<complex_t> array_c;
typedef py::array_t<float>     array_r; 
