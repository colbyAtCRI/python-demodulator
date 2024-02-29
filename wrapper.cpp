#include "demodulator.hpp"
#include "window.hpp"
PYBIND11_MODULE (demodulator, m)
{
    py::class_<CDecimator>(m, "CDecimator")
        .def (py::init<int,int,float>(),py::arg("dec"),py::arg("len"),py::arg("As"))
        .def ("reset", &CDecimator::reset)
        .def ("freqresponse", &CDecimator::freqresp)
        .def ("__call__", &CDecimator::execute);

}
