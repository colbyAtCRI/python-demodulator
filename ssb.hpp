#pragma once
#include "demodulator.hpp"
#include "window.hpp"

class SSBReciever
{
    bool         mLSB;
    CDecimator   mDecim;
    agc_crcf     mAGC;
    firhilbf     mHilbert;
    firfilt_rrrf mLowpass;
    resamp_rrrf  mAudio;

public:

    SSBReciever (std::string band, float bandwidth, float iq_rate, float pcm_rate) : mDecim((int)(0.5*iq_rate/pcm_rate),20,60.0f)
    {
        mLSB = band == "lsb";
        // iq_rate -> 2 * pcm_rate (nominal 96000)
        float irate = iq_rate / mDecim.get_decimation();
        float arate = pcm_rate / irate; 
        mHilbert = firhilbf_create (25,60.0f);
        mAGC = agc_crcf_create ();
        agc_crcf_set_bandwidth (mAGC, 0.001f);
        agc_crcf_set_scale (mAGC,0.01f);
        mLowpass = firfilt_rrrf_create_kaiser (20, bandwidth/irate, 60.0f, 0.0f);
        mAudio = resamp_rrrf_create_default (arate);
    }

   ~SSBReciever (void) 
    {
        agc_crcf_destroy     (mAGC);
        firhilbf_destroy     (mHilbert);
        firfilt_rrrf_destroy (mLowpass);
        resamp_rrrf_destroy  (mAudio);
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
            firfilt_rrrf_execute_block (mLowpass,lsb,py::len(xa),usb);
            resamp_rrrf_execute_block (mAudio, usb, py::len(xa), lsb, &nw);
            return array_from_data<float>(lsb,nw);
        }
        else {
            unsigned int nw;
            firfilt_rrrf_execute_block (mLowpass,usb,py::len(xa),lsb);
            resamp_rrrf_execute_block (mAudio, lsb, py::len(xa), usb, &nw);
            return array_from_data<float>(usb,nw);
        }
    }
};
