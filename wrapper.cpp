#include "demodulator.hpp"
#include "window.hpp"
#include "fm.hpp"
#include "am.hpp"
#include "ssb.hpp"
#include "cw.hpp"
#include "nbfm.hpp"

PYBIND11_MODULE (demodulator, m)
{
    py::class_<CDecimator>(m, "CDecimator")
        .def (py::init<int,int,float>(),py::arg("dec"),py::arg("len"),py::arg("As"))
        .def ("reset", &CDecimator::reset)
        .def ("freqresponse", &CDecimator::freqresp)
        .def ("__call__", &CDecimator::execute);

    py::class_<NBFMReciever>(m,"NBFMReciever")
        .def (py::init<float,float,float>(), py::arg("audio_bw")=2000.0,py::arg("iq_rate")=2000000.0,py::arg("pcm_rate")=48000.0)
        .def_readwrite ("auto_threshold", &NBFMReciever::mAutoThreshold)
        .def_property ("squelch", &NBFMReciever::get_squelch, &NBFMReciever::set_squelch)
        .def ("__call__", &NBFMReciever::execute);

    py::class_<FMReciever>(m,"FMReciever")
        .def (py::init<float,float>(),py::arg("iq_rate"),py::arg("pcm_rate")=48000.0f)
        .def_readwrite ("onPilotDetect", &FMReciever::onPilotDetect)
        .def_readwrite ("onPilotLoss", &FMReciever::onPilotLoss)
        .def ("reset", &FMReciever::reset)
        .def ("__call__", &FMReciever::execute);

    py::class_<AMReciever>(m, "AMReciever")
        .def (py::init<float,float,float>(),py::arg("bandwidth"),py::arg("iq_rate"),py::arg("pcm_rate")=48000.0f)
        .def_readonly ("carrier", &AMReciever::mMixerFreq)
        .def_readwrite ("carrier_bw", &AMReciever::mMixerFreqBW)
        .def_readwrite ("auto_threshold", &AMReciever::mAutoThreshold)
        .def_property ("squelch", &AMReciever::get_squelch, &AMReciever::set_squelch)
        .def_property ("threshold", &AMReciever::get_threshold, &AMReciever::set_threshold)
        .def_property ("level", &AMReciever::get_level, &AMReciever::set_level)
        .def ("reset", &AMReciever::reset)
        .def ("__call__", &AMReciever::execute);

    py::class_<SSBReciever>(m,"SSBReciever")
        .def (py::init<std::string,float,float,float>(),py::arg("band"),py::arg("bandwidth"),py::arg("iq_rate"),py::arg("pcm_rate")=48000.0f)
        .def ("__call__", &SSBReciever::execute);

    py::class_<CWReciever>(m,"CWReciever")
        .def (py::init<float,float,float,float>(),py::arg("bandwidth"),py::arg("tone"),py::arg("iq_rate"),py::arg("pcm_rate")=48000.0f)
        .def ("__call__", &CWReciever::execute);
}
