#pragma once
#include "demodulator.hpp"
#include "window.hpp"

class FMReciever 
{
    CDecimator   mDecim;
    iirfilt_rrrf mPilot;
    nco_crcf     mMixer;
    freqdem      mDemod;
    iirfilt_rrrf mEmphL;
    iirfilt_rrrf mEmphR;
    resamp_rrrf  mAudioL;
    resamp_rrrf  mAudioR;

    // keep track of pilot tone.
    enum pilot_detect_t {
        MONORO,
        STEREO
    } mPilotDetect;

    float        mPilotU;
    float        mPilotC;
    float        mPilotL;
    unsigned int mCount;

    float phase_error;
    float lock_delta;

public:

    float pilot_detect_level;

    // python event callback objects
    py::object onPilotDetect;
    py::object onPilotLoss;

   ~FMReciever (void)
    {
        iirfilt_rrrf_destroy (mPilot);
        nco_crcf_destroy     (mMixer);
        freqdem_destroy      (mDemod);
        iirfilt_rrrf_destroy (mEmphL);
        iirfilt_rrrf_destroy (mEmphR);
        resamp_rrrf_destroy  (mAudioL);
        resamp_rrrf_destroy  (mAudioR);
    }

    FMReciever (float iq_rate, float pcm_rate) : mDecim((int)(iq_rate/200000.0f),20,60.0f)
    {
        onPilotDetect = py::none();
        onPilotLoss   = py::none();
        float mB[1], mA[2];
        int   dec  = mDecim.get_decimation ();
        float rate = iq_rate / dec;
 
        mPilotU = 2 * M_PI * 23000.0f / rate;
        mPilotC = 2 * M_PI * 19000.0f / rate;
        mPilotL = 2 * M_PI * 15000.0f / rate;

        mPilot = iirfilt_rrrf_create_prototype (
            LIQUID_IIRDES_CHEBY2,
            LIQUID_IIRDES_BANDPASS,
            LIQUID_IIRDES_SOS,
            7,
            15000.0f/rate,
            19000.0f/rate,
            0.5f,
            30.0f);

        // standard US 75 us de-emphasis filter
        mA[0] = 1.0;
        mA[1] = -exp(-1.0 / (125.0E-6 * pcm_rate));
        mB[0] = 1.0 + mA[1];

        mMixer    = nco_crcf_create (LIQUID_NCO);
        nco_crcf_pll_set_bandwidth (mMixer,0.001f);
        nco_crcf_set_frequency (mMixer, mPilotC);
        pilot_detect_level = 0.001;
        mDemod    = freqdem_create (1.0);
        mEmphL    = iirfilt_rrrf_create (mB,1,mA,2);
        mEmphR    = iirfilt_rrrf_create (mB,1,mA,2);
        mAudioL   = resamp_rrrf_create_default (pcm_rate/rate);
        mAudioR   = resamp_rrrf_create_default (pcm_rate/rate);
    }

    void reset (void) {
        mPilotDetect = MONORO;
        iirfilt_rrrf_reset (mPilot);
        resamp_rrrf_reset  (mAudioL);
        resamp_rrrf_reset  (mAudioR);
    }

    array_r execute (array_c inp) {
        // decimate iq_rate
        array_c   iq = mDecim.execute (inp);
        // move iqs into usable array
        complex_t x[py::len(iq)];
        array_to_data<complex_t>(iq,x);
        // allocate space for audio data
        float y[2*py::len(iq)];
        // demodulate into left-right samples
        unsigned int nw(0), nd; 
        for (auto n = 0; n < py::len(iq); n++) {
            nd = demod_one (x[n],&y[nw],&y[nw+1]);
            if (nd == 2) {
                nw += 2;
            }
        }
        return array_from_data<float> (y,nw);
    }

    unsigned int demod_one (complex_t x, float *left, float *right) {
        complex_t v, tc, sc;
        float s, t;

        // demodulate full real signal
        freqdem_demodulate (mDemod,x,&s);

        // bandpass filter pilot tone
        iirfilt_rrrf_execute (mPilot, s, &t);

        // Compute the phase error 
        phase_error = 2 * nco_crcf_sin (mMixer) * t;

        // mix L-R signal down by 38 kHz or base band
        nco_crcf_mix_down (mMixer, s,  &sc);  // down by 19 kHz
        nco_crcf_mix_down (mMixer, sc, &sc);  // down by 38 kHz

        // adjust mixer phase
        nco_crcf_pll_step (mMixer, phase_error);

        // step mixer 
        nco_crcf_step (mMixer);

        // real(sc) should contain L-R while s has L+R
        unsigned int nl, nr;
        resamp_rrrf_execute (mAudioL, s+real(sc),  left, &nl);
        resamp_rrrf_execute (mAudioR, s-real(sc), right, &nr);
        
        if (nl+nr == 2) {
            // Apply de-emphasis 75us filter
            iirfilt_rrrf_execute (mEmphL, *left,  left);
            iirfilt_rrrf_execute (mEmphR, *right, right);
            stereo_detect (left, right);
        }


        return nl+nr;
    }

    void stereo_detect (float *left, float *right) 
    {
        // real current pll frequency
        float pf = nco_crcf_get_frequency (mMixer);

        // bump 1/2 second timer
        mCount = mCount + 1;

        bool pilot_out_of_bounds = pf < mPilotL or pf > mPilotU;

        if ( mPilotDetect == MONORO ) {
            // reset counter and freq if out of bounds
            if ( pilot_out_of_bounds ) {
                nco_crcf_set_frequency (mMixer, mPilotC);
                mCount = 0;
            }
            // in bounds for 1/2 second, we got stereo
            if ( mCount > 24000 ) {
                mPilotDetect = STEREO;
                if ( py::hasattr (onPilotDetect,"__call__") )
                    onPilotDetect ();
            }
        }
        else {
            // if pilot wanders out of bounds, change to monoro
            if ( pilot_out_of_bounds ) {
                mPilotDetect = MONORO;
                nco_crcf_set_frequency (mMixer, mPilotC);
                mCount = 0;
                if ( py::hasattr(onPilotLoss,"__call__") )
                    onPilotLoss ();
            }
            // reset counter just because the universe is finite
            if ( mCount > 24000 )
                mCount = 0;
        }

        // make detect monoro
        if ( mPilotDetect == MONORO ) {
            *right = (*left+*right)/2.0;
            *left  = *right;
        }
    }
};

