#pragma once
#include "demodulator.hpp"
#include <queue>

// Magnify frequency window, Same rate scaled frequency range

class CMagnifier
{
    std::queue<complex_t> mQueue;
    firdecim_crcf         mDecim;
    firinterp_crcf        mInter;
    int                   mMag;
    bool                  mBypass;
public:

    CMagnifier (int mag) : mMag(mag)
    {
        mBypass = mMag < 2;
        if ( mBypass )
            mMag = 1;
        else {
            complex_t resp;
            mDecim = firdecim_crcf_create_kaiser (mMag, 20, 60.0f);
            firdecim_crcf_freqresp (mDecim,0.0f,&resp);
            firdecim_crcf_set_scale (mDecim,1.0/abs(resp));
            mInter = firinterp_crcf_create_kaiser (mMag, 20, 60.0f);
            //firinterp_crcf_freqresp (mInter,0.0f,&resp);
            //firinterp_crcf_set_scale (mInter,1.0/abs(resp));
        }
    }

   ~CMagnifier (void)
    {
        if ( not mBypass ) {
            firdecim_crcf_destroy (mDecim);
            firinterp_crcf_destroy (mInter);
        }
    }
            
    array_c execute (array_c inp) 
    {
        complex_t x[mMag], y, z[mMag];
        std::vector<complex_t> outp;
        queue_input (inp);
        while (get_frame (x)) {
            firdecim_crcf_execute (mDecim,x,&y);
            firinterp_crcf_execute (mInter,y,z);
            for (auto n = 0; n < mMag; n++)
                outp.push_back (z[n]);
        }
        return array_from_data<complex_t>(&outp.at(0),outp.size());        
    }

    void queue_input (array_c inp) 
    {
        complex_t *x = array_to_ptr<complex_t>(inp);
        for (auto n = 0; n < py::len(inp); n++)
            mQueue.push (x[n]);
    }

    bool get_frame (complex_t *x) 
    {
        if (mMag > mQueue.size())
            return false;
        for (unsigned int n = 0; n < mMag; n++) {
            x[n] = mQueue.front();
            mQueue.pop ();
        }
        return true;    
    }
};

// An efficient IQ integral data decimator 
class CDecimator
{
    std::queue<complex_t>  mQueue;
    firdecim_crcf          mDecim;
    int                    mDec;
    bool                   mBypass;

public:

    CDecimator (int dec, int len, float as) : mQueue (), mDec(dec)
    {
        mBypass = dec < 2;
        if ( mBypass ) 
            mDec = 1;
        else {
            complex_t resp;
            mDecim = firdecim_crcf_create_kaiser (dec, len, as);
            firdecim_crcf_freqresp (mDecim,0.0f,&resp);
            firdecim_crcf_set_scale (mDecim,1.0/abs(resp));
        }
    }

   ~CDecimator (void) 
    {
        if ( not mBypass )
            firdecim_crcf_destroy (mDecim);
    }

    array_c execute (array_c inp)
    {
        if (mBypass)
            return inp;
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

    int get_decimation (void)
    {
        return mDec;
    }

    void reset (void) 
    {
        if ( not mBypass ) {
            firdecim_crcf_reset (mDecim);
            while (not mQueue.empty())
                mQueue.pop();
        }
    }

    complex_t freqresp (float f) 
    {
        complex_t resp;
        if ( mBypass )
            resp = complex_t(1.0,0.0);
        else 
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
