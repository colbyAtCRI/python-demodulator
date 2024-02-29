#pragma once
#include "demodulator.hpp"
#include <queue>

// An efficient IQ integral data decimator 
class CDecimator
{
    std::queue<complex_t>  mQueue;
    firdecim_crcf          mDecim;
    int                    mDec;

public:

    CDecimator (int dec, int len, float as) : mQueue (), mDec(dec)
    {
        complex_t resp;
        mDecim = firdecim_crcf_create_kaiser (dec, len, as);
        firdecim_crcf_freqresp (mDecim,0.0f,&resp);
        firdecim_crcf_set_scale (mDecim,1.0/abs(resp));
    }

   ~CDecimator (void) 
    {
        firdecim_crcf_destroy (mDecim);
    }

    array_c execute (array_c inp)
    {
        std::vector<complex_t> outp;
        queue_all (inp);
        complex_t xb[mDec], y;

        while (get_frame (xb)) {
            firdecim_crcf_execute (mDecim, xb, &y);
            outp.push_back (y);
        }
        array_c ret(outp.size());
        complex_t *z = array_to_ptr<complex_t>(ret);
        for (unsigned int n = 0; n < outp.size(); n++)
            z[n] = outp[n];
        return ret;
    }

    void reset (void) 
    {
        firdecim_crcf_reset (mDecim);
        while (not mQueue.empty())
            mQueue.pop();
    }

    complex_t freqresp (float f) 
    {
        complex_t resp;
        firdecim_crcf_freqresp (mDecim, f, &resp);
        return resp;
    }

private:

    void queue_all (array_c inp)
    {
        complex_t *x = array_to_ptr<complex_t> (inp);
        for (unsigned int n = 0; n < py::len(inp); n++) 
            mQueue.push (x[n]);
    }

    bool get_frame (complex_t *x) {
        if (mDec > mQueue.size())
            return false;
        for (unsigned int n = 0; n < mDec; n++) {
            x[n] = mQueue.front();
            mQueue.pop ();
        }
        return true;    
    }
};
