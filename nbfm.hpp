#pragma once
#include "demodulator.hpp"
#include "window.hpp"

class NBFMReciever
{
    int          mDec;
    CDecimator   mDecim;
    freqdem      mDemod;
    resamp_rrrf  mResam;
    firfilt_rrrf mAudio;
    agc_crcf     mAgc;

public:
    NBFMReciever (float audio_bw, float iq_rate, float pcm_rate) : 
        mDec((int)(iq_rate/pcm_rate)), mDecim(mDec,20,60.0) 
    {
        mAgc   = agc_crcf_create ();
        agc_crcf_set_scale(mAgc,0.1f);
        mResam = resamp_rrrf_create_default (mDec*pcm_rate/iq_rate);
        mDemod = freqdem_create (1.0);
        mAudio = firfilt_rrrf_create_kaiser (20,audio_bw/pcm_rate,60.0f,0.0f);
    }

   ~NBFMReciever (void)
    {
        agc_crcf_destroy     (mAgc);
        resamp_rrrf_destroy  (mResam);
        freqdem_destroy      (mDemod);
        firfilt_rrrf_destroy (mAudio);
    }

    array_r execute (array_c inp) 
    {
        array_c iq = mDecim.execute (inp);
        complex_t x[py::len(iq)];
        float     y[py::len(iq)];
        uint32_t  nw;
        array_to_data<complex_t>(iq,x);
        agc_crcf_execute_block (mAgc, x, py::len(iq), x);
        freqdem_demodulate_block (mDemod,x,py::len(iq),y);
        resamp_rrrf_execute_block (mResam,y,py::len(iq),y,&nw);
        firfilt_rrrf_execute_block (mAudio,y,nw,y);
        return array_from_data<float>(y,nw);
    }
};
