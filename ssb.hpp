#pragma once
#include "demodulator.hpp"
#include "window.hpp"
#include <queue>
#include <vector>

class SSBReciever
{
    bool         mLSB;
    nco_crcf     mShift;
    CDecimator   mDecim;
    agc_crcf     mAGC;
    firhilbf     mHilbert;
    firfilt_crcf mLowpass;
    resamp_rrrf  mAudio;

public:

    SSBReciever (std::string band, float bandwidth, float iq_rate, float pcm_rate) : mDecim((int)(0.5*iq_rate/pcm_rate),20,60.0f)
    {
        mLSB = band == "lsb";
        // iq_rate -> 2 * pcm_rate (nominal 96000)
        float irate = iq_rate / mDecim.get_decimation();
        float arate = pcm_rate / irate; 
        mHilbert = firhilbf_create (55,60.0f);
        mAGC = agc_crcf_create ();
        agc_crcf_set_bandwidth (mAGC, 0.01f);
        agc_crcf_set_scale (mAGC,0.1f);
        mLowpass = firfilt_rrrf_create_kaiser (20, bandwidth/irate, 60.0f, 0.0f);
        mAudio = resamp_rrrf_create_default (arate);
        mShift = nco_crcf_create (LIQUID_NCO);
        float sign = (mLSB) ? -1.0f : 1.0f;
        nco_crcf_set_frequency (mShift, sign * M_PI * (bandwidth+40.0)/irate);
    }

   ~SSBReciever (void) 
    {
        nco_crcf_destroy     (mShift);
        agc_crcf_destroy     (mAGC);
        firhilbf_destroy     (mHilbert);
        firfilt_crcf_destroy (mLowpass);
        resamp_rrrf_destroy  (mAudio);
    }

    array_r execute (array_c inp) 
    {
        array_c xa = mDecim.execute (inp);
        complex_t x[py::len(xa)], y[py::len(xa)];
        float     usb[py::len(xa)], lsb[py::len(xa)];
        array_to_data<complex_t>(xa,x);
        for (unsigned int n = 0; n < py::len(xa); n++) {
            nco_crcf_mix_up (mShift, x[n], &x[n]);
            firfilt_crcf_execute_one (mLowpass, x[n], &x[n]);
            agc_crcf_execute (mAGC, x[n], &x[n]);
            nco_crcf_mix_down (mShift,x[n],&x[n]);
            firhilbf_c2r_execute (mHilbert, x[n], &lsb[n], &usb[n]);
            nco_crcf_step (mShift);
        }
        if (mLSB) {
            unsigned int nw;
            resamp_rrrf_execute_block (mAudio, lsb, py::len(xa), lsb, &nw);
            return array_from_data<float>(lsb,nw);
        }
        else {
            unsigned int nw;
            resamp_rrrf_execute_block (mAudio, usb, py::len(xa), usb, &nw);
            return array_from_data<float>(usb,nw);
        }
    }
};
