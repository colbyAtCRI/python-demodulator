#pragma once
#include "demodulator.hpp"
#include "window.hpp"

using namespace std;

class AMDemod
{
    iirfilt_crcf  mLowpass;
    agc_crcf      mAGC;
    msresamp_rrrf mAudio;
public:
    int mSampleRate;

    AMDemod (float bandwidth, float iq_rate, float pcm_rate)
    {
        mSampleRate = (int)iq_rate;
        mLowpass = iirfilt_crcf_create_lowpass (9, bandwidth/2.0/iq_rate);
        mAGC = agc_crcf_create();
        agc_crcf_set_scale (mAGC, 0.1f);
        agc_crcf_set_bandwidth (mAGC, 0.001f);
        mAudio = msresamp_rrrf_create (pcm_rate/iq_rate,60.0f);
    }

   ~AMDemod (void)
    {
        iirfilt_crcf_destroy (mLowpass);
        agc_crcf_destroy (mAGC);
        msresamp_rrrf_destroy (mAudio);
    }

    array_r execute (vector<complex<float>> iq)
    {
        unsigned int nw;
        complex<float> *v = iq.data();
        vector<float> ret(iq.size());
        for (int n = 0; n < iq.size(); n++) {
            iirfilt_crcf_execute(mLowpass, v[n], &v[n]);
            agc_crcf_execute (mAGC, v[n], &v[n]);
            ret[n] = abs(v[n]);
        }
        msresamp_rrrf_execute (mAudio, ret.data(), ret.size(), ret.data(), &nw);
        return array_from_data<float>(ret.data(),nw);
    }
};

class SSBDemod
{
    nco_crcf      mMixer;
    iirfilt_crcf  mLowpass;
    agc_crcf      mAGC;
    msresamp_rrrf mAudio;

public:
    int mSampleRate;

    SSBDemod (string band, float bandwidth, float iq_rate, float pcm_rate)
    {
        float shift(0.0f);
        float hbw = bandwidth/2.0/iq_rate;
        mSampleRate = (int)iq_rate;
        if (band == "lsb")
            shift = 2.0f*M_PI*hbw;
        else if (band == "usb")
            shift = -2.0f*M_PI*hbw;
        else if (band == "ssb")
            shift = 0.0f;
        mMixer = nco_crcf_create (LIQUID_NCO);
        nco_crcf_set_frequency (mMixer,shift);
        mLowpass = iirfilt_crcf_create_lowpass (9, hbw);
        mAGC = agc_crcf_create();
        agc_crcf_set_scale (mAGC,0.1f);
        agc_crcf_set_bandwidth (mAGC, 0.001f);
        mAudio = msresamp_rrrf_create (pcm_rate/iq_rate, 60.0f);
    }

   ~SSBDemod (void)
    {
        nco_crcf_destroy      (mMixer);
        iirfilt_crcf_destroy  (mLowpass);
        agc_crcf_destroy      (mAGC);
        msresamp_rrrf_destroy (mAudio);
    }

    array_r execute (vector<complex<float>> iq)
    {
        unsigned int nw;
        complex<float> *v = iq.data();
        vector<float> ret(iq.size());
        for (int n = 0; n < iq.size(); n++) {
            nco_crcf_mix_up (mMixer,v[n],&v[n]);
            iirfilt_crcf_execute(mLowpass, v[n], &v[n]);
            nco_crcf_mix_down (mMixer,v[n],&v[n]);
            nco_crcf_step (mMixer);
            agc_crcf_execute (mAGC, v[n], &v[n]);
            ret[n] = real(v[n]);
        }
        msresamp_rrrf_execute (mAudio, ret.data(), ret.size(), ret.data(), &nw);
        return array_from_data<float>(ret.data(),nw);
    }
};

class AMReciever
{
    CDecimator   mDecim;
    firfilt_crcf mLowpass;
    wdelaycf     mDelay;
    agc_crcf     mAGC;
    nco_crcf     mMixer;
    iirfilt_rrrf mDCBlock;
    msresamp_rrrf  mAudio;

    float fscale;
    float fade;

public:

    int   mIq_rate;
    float mMixerFreq;
    float mMixerFreqBW;
    float mAutoThreshold;

   ~AMReciever (void)
    {
        firfilt_crcf_destroy (mLowpass);
        agc_crcf_destroy     (mAGC);
        nco_crcf_destroy     (mMixer);
        iirfilt_rrrf_destroy (mDCBlock);
        msresamp_rrrf_destroy  (mAudio);
    }

    AMReciever (float bandwidth, float iq_rate, float pcm_rate) : mDecim((int)(iq_rate/bandwidth),20,60.0)
    {
        mIq_rate = int(iq_rate);
        float fc = 20.0f/pcm_rate;
        float audio_rate = pcm_rate * mDecim.get_decimation() / iq_rate;
        fscale = iq_rate / mDecim.get_decimation() / 2 / M_PI;
        mMixerFreqBW = 0.001;
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
        mAudio = msresamp_rrrf_create (audio_rate,60.0f);
    }

    void reset (void) 
    {
        mDecim.reset();
        firfilt_crcf_reset (mLowpass);
        iirfilt_rrrf_reset (mDCBlock);
        msresamp_rrrf_reset  (mAudio);
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
        mMixerFreq = (1.0 - mMixerFreqBW) * mMixerFreq + mMixerFreqBW * nco_crcf_get_frequency (mMixer) * fscale;
        unsigned int nw;
        msresamp_rrrf_execute (mAudio,y,py::len(iqa),y,&nw);
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
