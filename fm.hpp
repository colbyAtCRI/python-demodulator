#pragma once
#include "demodulator.hpp"
#include "window.hpp"

class FMReciever 
{
    CDecimator   mDecim;
    iirfilt_rrrf mPilot;
    nco_crcf     mMixer;
    agc_rrrf     mPilotAGC;
    float        mPilotFreq;
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

    float phase_error;
    float freq_error;

public:

    // python event callback objects
    py::object onPilotDetect;
    py::object onPilotLoss;

   ~FMReciever (void)
    {
        iirfilt_rrrf_destroy (mPilot);
        nco_crcf_destroy     (mMixer);
        agc_rrrf_destroy     (mPilotAGC);
        freqdem_destroy      (mDemod);
        iirfilt_rrrf_destroy (mEmphL);
        iirfilt_rrrf_destroy (mEmphR);
        resamp_rrrf_destroy  (mAudioL);
        resamp_rrrf_destroy  (mAudioR);
    }

    FMReciever (float iq_rate, float pcm_rate) : mDecim((int)(iq_rate/106000.0f),20,60.0f)
    {
        onPilotDetect = py::none();
        onPilotLoss   = py::none();
        float mB[1], mA[2];
        int   dec  = mDecim.get_decimation ();
        float rate = iq_rate / dec;
 
        mPilotFreq = 2 * M_PI * 19000.0f / rate;

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
        mA[1] = -exp(-dec / (75.0E-6 * rate));
        mB[0] = 1.0 + mA[1];

        mMixer    = nco_crcf_create (LIQUID_NCO);
        nco_crcf_pll_set_bandwidth (mMixer,0.001f);
        mPilotAGC = agc_rrrf_create ();
        mDemod    = freqdem_create (8.0);
        mEmphL    = iirfilt_rrrf_create (mB,1,mA,2);
        mEmphR    = iirfilt_rrrf_create (mB,1,mA,2);
        mAudioL   = resamp_rrrf_create_default (pcm_rate/rate);
        mAudioR   = resamp_rrrf_create_default (pcm_rate/rate);

        // setup pilot detect parameters
        agc_rrrf_set_scale (mPilotAGC,1.0f);
        agc_rrrf_set_bandwidth (mPilotAGC, 0.01f);
        
    }

    void reset (void) {
        mPilotDetect = MONORO;
        nco_crcf_set_frequency (mMixer,mPilotFreq);
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
            if (nd == 2)
                nw += 2;
        }
        return array_from_data<float> (y,nw);
    }

    float lock_error (void) 
    {
        float delta = (nco_crcf_get_frequency(mMixer) - mPilotFreq)/mPilotFreq;
        delta = delta * delta;
        freq_error = 0.999 * freq_error + 0.001 * delta;
        return freq_error;
    }

    unsigned int demod_one (complex_t x, float *left, float *right) {
        complex_t v, tc, sc;
        float s, t;

        // demodulate full real signal
        freqdem_demodulate (mDemod,x,&s);

        // compute mixer phase error
        iirfilt_rrrf_execute (mPilot, s, &t);
        agc_rrrf_execute (mPilotAGC, t, &t);
        phase_error = 2 * nco_crcf_sin (mMixer) * t;

        // mix L-R signal down by 38 kHz or base band
        nco_crcf_mix_down (mMixer, s,  &sc);  // down by 19 kHz
        nco_crcf_mix_down (mMixer, sc, &sc);  // down by 38 kHz

        // adjust mixer phase
        nco_crcf_pll_step (mMixer, phase_error);

        // step mixer 
        nco_crcf_step (mMixer);

        // Apply de-emphasis 75us filter
        iirfilt_rrrf_execute (mEmphL, s+real(sc), left);
        iirfilt_rrrf_execute (mEmphR, s-real(sc), right);

        // real(sc) should contain L-R while s has L+R
        unsigned int nl, nr;
        resamp_rrrf_execute (mAudioL, *left,  left,  &nl);
        resamp_rrrf_execute (mAudioR, *right, right, &nr);

        //stereo_detect (left, right);

        return nl+nr;
    }

    void stereo_detect (float *left, float *right) 
    {
        pilot_detect_t state;
        // for stereo braodcast FM, there is an 8kHz wide
        // band in the real demodulated signal containing
        // a solitary 19kHz pilot tone. The mPilot bandpass
        // filter removes all but this 8kHz band. 
        if ( lock_error() < 0.0001 ) {
            state = STEREO;
        }
        else {
            state = MONORO;
            *right = (*left+*right)/2.0;
            *left  = *right;
        }
        if ( not (state == mPilotDetect) ) {
            mPilotDetect = state;
            if ( mPilotDetect == STEREO ) {
                if ( py::hasattr(onPilotDetect,"__call__" ))
                    onPilotDetect();
            }
            else {
                if (py::hasattr(onPilotLoss,"__call__"))
                    onPilotLoss();
            }
        }
    }
};

