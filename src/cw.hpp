#pragma once 
#include "demodulator.hpp"
#include "window.hpp"

class CWReciever
{
    CDecimator   mDecim;
    nco_crcf     mMixer;
    firfilt_crcf mLowpass;
    agc_crcf     mAGC;
    resamp_crcf  mAudio;

public:

    CWReciever (float bandwidth, float tone, float iq_rate, float pcm_rate) : mDecim((int)(iq_rate/pcm_rate),20,60.0f)
    {
        mMixer = nco_crcf_create (LIQUID_NCO);
        nco_crcf_set_frequency (mMixer, 2*M_PI*tone/pcm_rate);
        mLowpass = firfilt_crcf_create_kaiser (10,bandwidth/pcm_rate,40.0f,0.0f);
        mAudio = resamp_crcf_create_default (pcm_rate * mDecim.get_decimation() / iq_rate);
        mAGC = agc_crcf_create ();
        agc_crcf_set_scale (mAGC,0.1f);
        agc_crcf_set_bandwidth (mAGC,0.001f);
    }

  ~CWReciever (void) 
    {
        nco_crcf_destroy     (mMixer);
        firfilt_crcf_destroy (mLowpass);
        agc_crcf_destroy     (mAGC);
        resamp_crcf_destroy  (mAudio);
    }

    array_r execute (array_c inp)
    {
        array_c iq = mDecim.execute (inp);
        complex_t x[py::len(iq)];
        float     y[py::len(iq)];
        array_to_data<complex_t> (iq,x);
        // restrict to bandwidth
        firfilt_crcf_execute_block (mLowpass, x, py::len(iq), x);
        // adjust level
        agc_crcf_execute_block (mAGC, x, py::len(iq), x);
        // offset by center tone
        nco_crcf_mix_block_down (mMixer, x, x, py::len(iq));
        // resample to pcm_rate taking real part
        complex_t z;
        unsigned int nw(0), nd;
        for (unsigned int n = 0; n < py::len(iq); n++) {
            resamp_crcf_execute (mAudio, x[n], &z, &nd);
            if (nd == 1) 
                y[nw++] = real(z);
        }
        return array_from_data<float>(y,nw);
    }
};
