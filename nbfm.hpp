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

    float        fade;

public:

    float mAutoThreshold;

    NBFMReciever (float audio_bw, float iq_rate, float pcm_rate) : 
        mDec((int)(iq_rate/pcm_rate)), mDecim(mDec,20,60.0) 
    {
        mAutoThreshold = 0.0f;
        mAgc   = agc_crcf_create ();
        agc_crcf_set_scale(mAgc,0.1f);
        agc_crcf_set_bandwidth (mAgc,0.001f);
        agc_crcf_squelch_disable (mAgc);
        mResam = resamp_rrrf_create_default (mDec*pcm_rate/iq_rate);
        mDemod = freqdem_create (2.0);
        mAudio = firfilt_rrrf_create_kaiser (20,audio_bw/pcm_rate,60.0f,0.0f);
    }

   ~NBFMReciever (void)
    {
        agc_crcf_destroy     (mAgc);
        resamp_rrrf_destroy  (mResam);
        freqdem_destroy      (mDemod);
        firfilt_rrrf_destroy (mAudio);
    }

    bool get_squelch (void) { return agc_crcf_squelch_is_enabled(mAgc); }
    void set_squelch (bool val) 
    { 
        if (val) {
            agc_crcf_squelch_enable(mAgc);
            float level = agc_crcf_get_rssi (mAgc) + mAutoThreshold;
            agc_crcf_squelch_set_threshold (mAgc,level);
        }
        else {
            agc_crcf_squelch_disable (mAgc);
        }
    }

    array_r execute (array_c inp) 
    {
        array_c iq = mDecim.execute (inp);
        complex_t x[py::len(iq)];
        float     y[py::len(iq)];
        uint32_t  nw;
        array_to_data<complex_t>(iq,x);
        fm_agc_block (x,py::len(iq),y);
        resamp_rrrf_execute_block (mResam,y,py::len(iq),y,&nw);
        firfilt_rrrf_execute_block (mAudio,y,nw,y);
        return array_from_data<float>(y,nw);
    }

    void fm_agc_block (complex_t *x, unsigned int nx, float *y)
    {
        for (unsigned int n = 0; n < nx; n++) {
            agc_crcf_execute (mAgc, x[n], &x[n]);
            freqdem_demodulate (mDemod, x[n], &y[n]);
            switch (agc_crcf_squelch_get_status (mAgc)) {
                case LIQUID_AGC_SQUELCH_UNKNOWN:
                case LIQUID_AGC_SQUELCH_ENABLED:
                case LIQUID_AGC_SQUELCH_FALL:
                case LIQUID_AGC_SQUELCH_SIGNALLO:
                case LIQUID_AGC_SQUELCH_TIMEOUT:
                    y[n] = 0.0f;
                    break;
                case LIQUID_AGC_SQUELCH_RISE:
                    fade = 0.0f;
                    y[n] = 0.0f;
                    break;
                case LIQUID_AGC_SQUELCH_SIGNALHI:
                    fade = 0.999 * fade + 0.001;
                    y[n] *= fade;
                    break;
                case LIQUID_AGC_SQUELCH_DISABLED:
                    break;
            }
        }
    }
};
