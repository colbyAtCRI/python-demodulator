#pragma once
#include "demodulator.hpp"
#include "window.hpp"

class AMReciever
{
    CDecimator   mDecim;
    agc_crcf     mAGC;
    nco_crcf     mMixer;
    iirfilt_rrrf mDCBlock;
    resamp_rrrf  mAudio;

public:

   ~AMReciever (void)
    {
        agc_crcf_destroy     (mAGC);
        nco_crcf_destroy     (mMixer);
        iirfilt_rrrf_destroy (mDCBlock);
        resamp_rrrf_destroy  (mAudio);
    }

    AMReciever (float bandwidth, float iq_rate, float pcm_rate) : mDecim((int)(iq_rate/bandwidth),20,60.0)
    {
        float fc = 20.0f/pcm_rate;
        float audio_rate = pcm_rate * mDecim.get_decimation() / iq_rate;
        mAGC = agc_crcf_create (); 
        agc_crcf_set_scale (mAGC, 0.1f);
        agc_crcf_set_bandwidth (mAGC, 0.001f);
        mMixer = nco_crcf_create (LIQUID_NCO);
        nco_crcf_pll_set_bandwidth (mMixer, 0.01f);
        mDCBlock = iirfilt_rrrf_create_prototype (LIQUID_IIRDES_CHEBY2,LIQUID_IIRDES_HIGHPASS,LIQUID_IIRDES_SOS,3,fc,0.0f,0.5f,20.0f);
        mAudio = resamp_rrrf_create_default (audio_rate);
    }

    void reset (void) 
    {
        mDecim.reset();
        agc_crcf_reset (mAGC);
        iirfilt_rrrf_reset (mDCBlock);
        resamp_rrrf_reset  (mAudio);
    }

    bool get_squelch (void) { return agc_crcf_squelch_is_enabled(mAGC); }
    void set_squelch (bool val) 
    { 
        if (val) {
            agc_crcf_squelch_enable(mAGC);
            float level = agc_crcf_get_rssi (mAGC) + 5.0;
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
        complex_t *iq = array_to_ptr<complex_t>(iqa);
        float x[py::len(iqa)], y[py::len(iqa)];
        for (unsigned int n = 0; n < py::len(iqa); n++)
            x[n] = demod_one (iq[n]);
        unsigned int nw;
        resamp_rrrf_execute_block (mAudio,x,py::len(iqa),y,&nw);
        return array_from_data<float>(y,nw);
    }

    float demod_one (complex_t iq)
    {
        complex_t v;
        float     outp;
        float     fade;

        agc_crcf_execute (mAGC, iq, &iq);
        nco_crcf_mix_down (mMixer, iq, &v);
        nco_crcf_pll_step (mMixer, arg(v));
        nco_crcf_step (mMixer);
        iirfilt_rrrf_execute (mDCBlock, real(v), &outp);
        switch (agc_crcf_squelch_get_status(mAGC)) {
            case LIQUID_AGC_SQUELCH_UNKNOWN:
                return 0.0f;
            case LIQUID_AGC_SQUELCH_ENABLED:
                return 0.0f;
            case LIQUID_AGC_SQUELCH_RISE:
                // init fade in.
                fade = 0.0;
                return 0.0f;
            case LIQUID_AGC_SQUELCH_SIGNALHI:
                fade = 0.99 * fade + 0.01;
                outp *= fade;
                return outp;
            case LIQUID_AGC_SQUELCH_FALL:
            case LIQUID_AGC_SQUELCH_SIGNALLO:
            case LIQUID_AGC_SQUELCH_TIMEOUT:
                return 0.0;
            case LIQUID_AGC_SQUELCH_DISABLED:
                return outp;
        }
        return outp;        
    }

};
