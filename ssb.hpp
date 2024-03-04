#pragma once
#include "demodulator.hpp"
#include "window.hpp"

class SSBReciever
{
    bool        mLSB;
    CDecimator  mDecim;
    agc_crcf    mAGC;
    firhilbf    mHilbert;
    resamp_rrrf mAudio;
public:

    SSBReciever (std::string band, float bandwidth, float iq_rate, float pcm_rate) : mDecim((int)(iq_rate/bandwidth),20,60.0f)
    {
        mLSB = band == "lsb";
        float rate = mDecim.get_decimation() * pcm_rate / iq_rate;
        mHilbert = firhilbf_create (25,60.0f);
        mAGC = agc_crcf_create ();
        agc_crcf_set_bandwidth (mAGC, 0.001f);
        agc_crcf_set_scale (mAGC,0.1f);
        std::cout << rate << std::endl;
        mAudio = resamp_rrrf_create_default (rate);
    }

   ~SSBReciever (void) 
    {
        firhilbf_destroy (mHilbert);
    }

    array_r execute (array_c inp) 
    {
        array_c xa = mDecim.execute (inp);
        complex_t x[py::len(xa)], y[py::len(xa)];
        float     usb[py::len(xa)], lsb[py::len(xa)];
        array_to_data<complex_t>(xa,x);
        agc_crcf_execute_block (mAGC, x, py::len(xa), y);
        for (unsigned int n = 0; n < py::len(xa); n++)
            firhilbf_c2r_execute (mHilbert, y[n], &lsb[n], &usb[n]);
        if (mLSB) {
            unsigned int nw;
            resamp_rrrf_execute_block (mAudio, lsb, py::len(xa), usb, &nw);
            return array_from_data<float>(usb,nw);
        }
        else {
            unsigned int nw;
            resamp_rrrf_execute_block (mAudio, usb, py::len(xa), lsb, &nw);
            return array_from_data<float>(lsb,nw);
        }
    }
};
