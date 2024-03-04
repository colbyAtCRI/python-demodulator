#pragma once
#include "demodulator.hpp"
#include "window.hpp"

class AMReciever
{
    CDecimator   mDecim;
    firfilt_crcf mLowpass;
    wdelaycf     mDelay;
    agc_crcf     mAGC;
    nco_crcf     mMixer;
    iirfilt_rrrf mDCBlock;
    resamp_rrrf  mAudio;

    float fade;

public:

    float mAutoThreshold;

   ~AMReciever (void)
    {
        firfilt_crcf_destroy (mLowpass);
        agc_crcf_destroy     (mAGC);
        nco_crcf_destroy     (mMixer);
        iirfilt_rrrf_destroy (mDCBlock);
        resamp_rrrf_destroy  (mAudio);
    }

    AMReciever (float bandwidth, float iq_rate, float pcm_rate) : mDecim((int)(iq_rate/bandwidth),20,60.0)
    {
        float fc = 20.0f/pcm_rate;
        float audio_rate = pcm_rate * mDecim.get_decimation() / iq_rate;
        // AGC
        mAGC = agc_crcf_create (); 
        agc_crcf_set_scale (mAGC, 0.1f);
        agc_crcf_set_bandwidth (mAGC, 0.001f);
        agc_crcf_squelch_disable (mAGC);
        mAutoThreshold = 0.0f;
        fade = 0.0;

        // Mixer 0.01f is about 200 Hz
        mLowpass = firfilt_crcf_create_kaiser (51, 0.01f, 40.0f, 0.0f);
        mDelay = wdelaycf_create (25);
        mMixer = nco_crcf_create (LIQUID_NCO);
        nco_crcf_pll_set_bandwidth (mMixer, 0.001f);
        mDCBlock = iirfilt_rrrf_create_prototype (LIQUID_IIRDES_CHEBY2,LIQUID_IIRDES_HIGHPASS,LIQUID_IIRDES_SOS,3,fc,0.0f,0.5f,20.0f);
        mAudio = resamp_rrrf_create_default (audio_rate);
    }

    void reset (void) 
    {
        mDecim.reset();
        firfilt_crcf_reset (mLowpass);
        iirfilt_rrrf_reset (mDCBlock);
        resamp_rrrf_reset  (mAudio);
    }

    bool get_squelch (void) { return agc_crcf_squelch_is_enabled(mAGC); }
    void set_squelch (bool val) 
    { 
        if (val) {
            agc_crcf_squelch_enable(mAGC);
            float level = agc_crcf_get_rssi (mAGC) + mAutoThreshold;
            agc_crcf_squelch_set_threshold (mAGC,level);
        }
        else {
            agc_crcf_squelch_disable (mAGC);
        }
    }

    float get_threshold (void) { return agc_crcf_squelch_get_threshold (mAGC); }
    void  set_threshold (float val) { agc_crcf_squelch_set_threshold (mAGC, val); }

    float get_level (void) { return agc_crcf_get_rssi (mAGC); }
    void  set_level (float val) { agc_crcf_set_rssi (mAGC,val); }

    array_r execute (array_c inp) 
    {
        array_c iqa = mDecim.execute (inp);
        complex_t x[py::len(iqa)];
        float     y[py::len(iqa)];
        array_to_data<complex_t>(iqa,x);
        for (unsigned int n = 0; n < py::len(iqa); n++)
            y[n] = demod_one (x[n]);
        unsigned int nw;
        resamp_rrrf_execute_block (mAudio,y,py::len(iqa),y,&nw);
        return array_from_data<float>(y,nw);
    }

    float demod_one (complex_t iq)
    {
        complex_t vs, v0;
        float     outp;

        agc_crcf_execute (mAGC, iq, &vs);
        firfilt_crcf_push (mLowpass, vs);
        firfilt_crcf_execute (mLowpass, &v0);
        wdelaycf_push (mDelay, vs);
        wdelaycf_read (mDelay, &vs);
        nco_crcf_mix_down (mMixer, vs, &vs);
        nco_crcf_mix_down (mMixer, v0, &v0);
        nco_crcf_pll_step (mMixer, arg(v0));
        nco_crcf_step (mMixer);
        iirfilt_rrrf_execute (mDCBlock, real(vs), &outp);
        switch (agc_crcf_squelch_get_status(mAGC)) {
            case LIQUID_AGC_SQUELCH_UNKNOWN:
                outp = 0.0f;
                break;
            case LIQUID_AGC_SQUELCH_ENABLED:
                outp = 0.0f;
                break;
            case LIQUID_AGC_SQUELCH_RISE:
                // init fade in.
                fade = 0.0;
                outp = 0.0f;
                break;
            case LIQUID_AGC_SQUELCH_SIGNALHI:
                fade = 0.999 * fade + 0.001;
                outp *= fade;
                break;
            case LIQUID_AGC_SQUELCH_FALL:
            case LIQUID_AGC_SQUELCH_SIGNALLO:
            case LIQUID_AGC_SQUELCH_TIMEOUT:
                outp = 0.0;
                break;
            case LIQUID_AGC_SQUELCH_DISABLED:
                break;
        }
        return outp;        
    }

};
