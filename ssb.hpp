#pragma once
#include "demodulator.hpp"
#include "window.hpp"
#include <queue>
#include <vector>

class SSBReciever
{
    bool         mLSB;
    nco_crcf     mShift;
    agc_crcf     mAGC;
    firhilbf     mHilbert;
    iirfilt_crcf mLowpass;
    resamp_crcf  mAudio;

public:

    SSBReciever (std::string band, float bandwidth, float iq_rate, float pcm_rate) 
    {
        mLSB = band == "lsb";
        mHilbert = firhilbf_create (55,60.0f);
        mAGC = agc_crcf_create ();
        agc_crcf_set_bandwidth (mAGC, 0.01f);
        agc_crcf_set_scale (mAGC,0.1f);
        mLowpass = iirfilt_crcf_create_lowpass (8, bandwidth/iq_rate);
        mAudio = resamp_crcf_create_default (pcm_rate/iq_rate);
        mShift = nco_crcf_create (LIQUID_NCO);
        float sign = (mLSB) ? 1.0f : -1.0f;
        nco_crcf_set_frequency (mShift, sign * M_PI * (bandwidth+40.0f)/iq_rate);
    }

   ~SSBReciever (void) 
    {
        nco_crcf_destroy     (mShift);
        agc_crcf_destroy     (mAGC);
        firhilbf_destroy     (mHilbert);
        iirfilt_crcf_destroy (mLowpass);
        resamp_crcf_destroy  (mAudio);
    }

    array_r execute (array_c inp) 
    {
        complex_t x[py::len(inp)], y[py::len(inp)];
        float     usb[py::len(inp)], lsb[py::len(inp)];
        unsigned int nw;
        array_to_data<complex_t>(inp,x);
        // apply lowpass agc and HT to full iq rate
        for (unsigned int n = 0; n < py::len(inp); n++) {
            nco_crcf_mix_up (mShift, x[n], &x[n]);
            iirfilt_crcf_execute (mLowpass, x[n], &x[n]);
            nco_crcf_mix_down (mShift,x[n],&x[n]);
            nco_crcf_step (mShift);
            agc_crcf_execute (mAGC, x[n], &x[n]);
        }
        resamp_crcf_execute_block (mAudio, x, py::len(inp), x, &nw);
        for (auto n = 0; n < nw; n++)
            firhilbf_c2r_execute (mHilbert, x[n], &lsb[n], &usb[n]);
        if (mLSB) {
            return array_from_data<float>(lsb,nw);
        }
        else {
            return array_from_data<float>(usb,nw);
        }
    }
};
