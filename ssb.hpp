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

    bool         mAGCLock;

public:

    SSBReciever (std::string band, float bandwidth, float iq_rate, float pcm_rate) 
    {
        mAGCLock = false;
        mLSB = band == "lsb";
        mAGC = agc_crcf_create ();
        agc_crcf_set_bandwidth (mAGC, 0.1f);
        agc_crcf_set_scale (mAGC,0.01f);
        mLowpass = iirfilt_crcf_create_lowpass (5, bandwidth/(4*iq_rate));
        mAudio = resamp_crcf_create_default (pcm_rate/iq_rate);
        mShift = nco_crcf_create (LIQUID_NCO);
        float sign = (mLSB) ? 1.0f : -1.0f;
        float fs = sign * M_PI * (bandwidth/2.0 + 100.0f) / iq_rate;;
        nco_crcf_set_frequency (mShift, fs);
    }

   ~SSBReciever (void) 
    {
        nco_crcf_destroy     (mShift);
        agc_crcf_destroy     (mAGC);
        iirfilt_crcf_destroy (mLowpass);
        resamp_crcf_destroy  (mAudio);
    }

    void set_agc_scale (float sc) 
    {
        agc_crcf_set_scale (mAGC,sc);
    }

    float get_agc_scale (void)
    {
        return agc_crcf_get_scale (mAGC);
    }

    void set_agc_bandwidth (float tc) 
    {
        agc_crcf_set_bandwidth (mAGC, tc);
    }

    float get_agc_bandwidth (void)
    {
        return agc_crcf_get_bandwidth (mAGC);
    }

    void set_agc_lock (bool val)
    {
        if (val)
            agc_crcf_lock (mAGC);
        else
            agc_crcf_unlock (mAGC);
        mAGCLock = val;
    }

    bool get_agc_lock (void)
    {
        return mAGCLock;
    }

    array_r execute (array_c inp) 
    {
        complex_t   x[py::len(inp)];
        float     pcm[py::len(inp)];
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
            pcm[n] = x[n].real();
        return array_from_data<float> (pcm,nw);
    }
};
