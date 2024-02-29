#define PYBIND11_DETAILED_ERROR_MESSAGES 
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <pybind11/cast.h>
#include <pybind11/embed.h>
#include <string>
#include <iostream>
#include <complex>
#include <vector>
#include <map>
#include <liquid/liquid.h>

namespace py = pybind11;

template<class T>
T *array_to_ptr (py::array_t<T> a)
{
    return static_cast<T*>(a.request().ptr);
}

class Delay 
{
    int      mDelay;
    wdelayf  mRealDelay;
    wdelaycf mComplexDelay;
public:

    Delay (int nd) {
        mDelay = nd;
        mRealDelay = wdelayf_create (nd);
        mComplexDelay = wdelaycf_create (nd);
    }

   ~Delay (void) {
        wdelayf_destroy (mRealDelay);
        wdelaycf_destroy (mComplexDelay);
    }

    int get_delay (void) {
        return mDelay;
    }

    void set_delay (int nd) {
        wdelayf_destroy (mRealDelay);
        wdelaycf_destroy (mComplexDelay);
        mDelay = nd;
        mRealDelay = wdelayf_create (nd);
        mComplexDelay = wdelaycf_create (nd);
    }
 
    py::object call (py::object inp) {
        auto tp = py::getattr (inp,"dtype");
        if ( tp.is(py::dtype ("complex64")) ) {
            py::array_t<std::complex<float>> ret(py::len(inp));
            std::complex<float> *z = array_to_ptr<std::complex<float>>(inp);
            std::complex<float> *y = array_to_ptr<std::complex<float>>(ret);
            for (auto n = 0; n < (int)py::len(inp); n++) {
                wdelaycf_read (mComplexDelay, &y[n]);
                wdelaycf_push (mComplexDelay, z[n]);
            }
            return ret;
        }
        if ( tp.is(py::dtype("float32")) ) {
            py::array_t<float> ret(py::len(inp));
            float *z = array_to_ptr<float>(inp);
            float *y = array_to_ptr<float>(ret);
            for (auto n = 0; n < (int)py::len(inp); n++) {
                wdelayf_read (mRealDelay, &y[n]);
                wdelayf_push (mRealDelay,  z[n]);
            }
            return ret;
        }
        return py::none();
    }
};

class PopFilter
{

public:

    bool         mEnable;
    float        mPeakPowerFilter;
    int          mDelayTime;
    int          mBlankWidth;
    float        mTriggerFraction;

    iirfilt_rrrf mHighpass;
    wdelaycf     mDelay;

    PopFilter (void) {
        mEnable = false;
        mDelayTime = 3;
        mBlankWidth = 10;
        mPeakPowerFilter = 100.0;
        mTriggerFraction = 1.0;
        mHighpass = nullptr;
        updateHighpass ();
        mDelay = nullptr;
        set_delay(mDelayTime);
        set_powerFilter (mPeakPowerFilter);
        mProcess = &PopFilter::ready;
    }

    ~PopFilter (void) {
        iirfilt_rrrf_destroy (mHighpass);
        wdelaycf_destroy (mDelay);
    }

    int get_delay (void) {
        return mDelayTime;
    }

    void set_delay (int dt) {
        if (mDelay) wdelaycf_destroy (mDelay);
        mDelay = wdelaycf_create(dt);
    }

    float get_Fc (void) {
        return hp_fc;
    }

    void set_Fc (float fc) {
        hp_fc = fc;
        updateHighpass ();
    }

    float get_powerFilter (void) {
        return mPeakPowerFilter;
    }

    void set_powerFilter (float N) {
        mPeakPowerFilter = N;
        mW0 = (N - 1.0) / N;
        mW1 = 1.0 / N;
    }

    py::array_t<std::complex<float>> call (py::array_t<std::complex<float>> inp) {
        if ( not mEnable )
            return inp;
        int np (py::len(inp));
        py::array_t<std::complex<float>> ret(np);
        std::complex<float> *zin = array_to_ptr<std::complex<float>>(inp);
        std::complex<float> *zout = array_to_ptr<std::complex<float>>(ret);
        for (auto n = 0; n < np; n++) {
            std::complex<float> z;
            powerMonitor (zin[n]);
            wdelaycf_push (mDelay,zin[n]);
            wdelaycf_read (mDelay,&z);
            zout[n] = (this->*mProcess)(z);
        }
        return ret;
    }        

private:

    // high pass filter parameters
    liquid_iirdes_filtertype hp_ftype  = LIQUID_IIRDES_BUTTER;
    liquid_iirdes_bandtype   hp_btype  = LIQUID_IIRDES_HIGHPASS;
    liquid_iirdes_format     hp_format = LIQUID_IIRDES_SOS;
    int                      hp_order  = 4;
    float                    hp_fc     = 0.05f;
    float                    hp_f0     = 0.40f; // ignored
    float                    hp_Ap     = 0.70f;
    float                    hp_As     = 60.0f;
    
    typedef std::complex<float> (PopFilter::*StateFunction)(std::complex<float>);
    StateFunction mProcess;
    float         mPeak;
    float         mLevel;
    float         mW0;
    float         mW1;

    int           mCounter;

    void updateHighpass (void) {
        if (mHighpass) iirfilt_rrrf_destroy (mHighpass);
        mHighpass = iirfilt_rrrf_create_prototype (
                        hp_ftype,
                        hp_btype,
                        hp_format,
                        hp_order,
                        hp_fc,
                        hp_f0,
                        hp_Ap,
                        hp_As);
    }

    // computes 
    void powerMonitor (std::complex<float> zin) {
        float pwr = (zin * std::conj(zin)).real();
        iirfilt_rrrf_execute (mHighpass, pwr, &pwr);
        if ( pwr > mPeak )  
            mPeak = pwr;
        else
            mPeak = mW0 * mPeak + mW1 * pwr;
        mLevel = mTriggerFraction * mPeak;
    }

    std::complex<float> ready (std::complex<float> zin) {
        float pwr = (zin * std::conj(zin)).real();
        if ( pwr > mLevel ) {
            mProcess = &PopFilter::blanking;
            mCounter = mBlankWidth;
            return std::complex<float>(0.0,0.0);
        }
        return zin;
    }

    std::complex<float> blanking (std::complex<float> zin) {
        mCounter = mCounter - 1;
        if ( mCounter > 0) {
            return std::complex<float> (0.0,0.0);
        }
        mProcess = &PopFilter::ready;
        return zin;
    }
};

py::array_t<liquid_float_complex> bytes_to_iq ( py::bytes byts )
{
    std::string data = py::cast<std::string>(byts);
    std::complex<short> *x = (std::complex<short>*) data.c_str();
    std::vector<std::complex<float>> y(data.size()/4);
    for (unsigned int n = 0; n < data.size()/4; n++) 
        y[n] = std::complex<float> ((float)x[n].real()/32767.0f,(float)x[n].imag()/32767.0f);
    return py::cast(y);
}

std::map<std::string,liquid_iirdes_filtertype> filter_type_map =
{
    {"butter", LIQUID_IIRDES_BUTTER},
    {"cheby1", LIQUID_IIRDES_CHEBY1},
    {"cheby2", LIQUID_IIRDES_CHEBY2},
    {"ellip",  LIQUID_IIRDES_ELLIP},
    {"bessel", LIQUID_IIRDES_BESSEL}
};

std::map<std::string,liquid_iirdes_bandtype> band_type_map = 
{
    {"lowpass", LIQUID_IIRDES_LOWPASS},
    {"highpass", LIQUID_IIRDES_HIGHPASS},
    {"bandpass", LIQUID_IIRDES_BANDPASS},
    {"bandstop", LIQUID_IIRDES_BANDSTOP}
};

// I'm going to do just LIQUID_IIRDES_TF, or transfer function format
class IIRfilter
{
public:

    std::string  mFt;
    std::string  mBt;
    int          mOrder;
    float        mFc;
    float        mF0;
    float        mAp;
    float        mAs;

    iirfilt_crcf mFilter;

    IIRfilter (std::string filter_type, std::string band_type, int order, float fc, float f0, float Ap, float As) {
        mOrder = order;
        mFc = fc;
        mF0 = f0;
        mAp = Ap;
        mAs = As;

        liquid_iirdes_filtertype ft(LIQUID_IIRDES_BUTTER);
        if ( filter_type_map.find(filter_type) != filter_type_map.end() ) {
            mFt = filter_type;
            ft = filter_type_map[filter_type];
        }
        liquid_iirdes_bandtype bt(LIQUID_IIRDES_LOWPASS);
        if ( band_type_map.find(band_type) != band_type_map.end() ) {
            mBt = band_type;
            bt = band_type_map[band_type];
        }
        mFilter = iirfilt_crcf_create_prototype (ft,bt,LIQUID_IIRDES_SOS,order,fc,f0,Ap,As);
    }

    ~IIRfilter (void) {
        iirfilt_crcf_destroy (mFilter);
    }

    void print (void) {
        iirfilt_crcf_print (mFilter);
    }

    std::complex<float> response (float freq) {
        std::complex<float> ret;
        iirfilt_crcf_freqresponse (mFilter, freq, &ret);
        return ret;
    }

    py::array_t<std::complex<float>> execute (py::array_t<std::complex<float>> x) {
        py::array_t<std::complex<float>> ret(py::len(x));
        std::complex<float> *z = array_to_ptr<std::complex<float>>(x);
        std::complex<float> *y = array_to_ptr<std::complex<float>>(ret);
        for (auto n = 0; n < (int)py::len(x); n++)
            iirfilt_crcf_execute (mFilter, z[n], &y[n]);
        return ret;
    }
};

class HilbertTransform 
{
    firhilbf mHt_c2r;
    firhilbf mHt_r2c;

public:

    HilbertTransform (int m, float As) {
        mHt_c2r = firhilbf_create (m, As);
        mHt_r2c = firhilbf_create (m, As);
    }

   ~HilbertTransform (void) {
        firhilbf_destroy (mHt_c2r);
        firhilbf_destroy (mHt_r2c);
    }

    py::object call (py::object inp) {
        auto tp = py::getattr (inp,"dtype");
        if ( tp.is(py::dtype ("complex64")) ) {
            py::array_t<float> ret(py::len(inp));
            std::complex<float> *z = array_to_ptr<std::complex<float>>(inp);
            float *y = array_to_ptr<float>(ret);
            for (auto n = 0; n < (int)py::len(inp); n++)
                firhilbf_interp_execute (mHt_c2r, z[n], &y[n]);
            return ret;
        }
        if ( tp.is(py::dtype("float32")) ) {
            py::array_t<std::complex<float>> ret(py::len(inp));
            float *z = array_to_ptr<float>(inp);
            std::complex<float> *y = array_to_ptr<std::complex<float>>(ret);
            for (auto n = 0; n < (int)py::len(inp); n++)
                firhilbf_decim_execute (mHt_r2c, &z[n], &y[n]);
            return ret;
        }
        return py::none();
    }
};

class DeemphasisFilter
{
    float        mB[1];
    float        mA[2];
    iirfilt_rrrf mFilt;

public:

    DeemphasisFilter (float sr) {
        float x = exp(-1.0/(75.0E-6 * sr));
        mA[0] = 1.0;
        mA[1] = -x;
        mB[0] = 1.0 - x;
        mFilt = iirfilt_rrrf_create (mB,1,mA,2);
    }

   ~DeemphasisFilter (void) {
        iirfilt_rrrf_destroy (mFilt);
    }

    std::complex<float> response (float freq) {
        std::complex<float> ret;
        iirfilt_rrrf_freqresponse (mFilt, freq, &ret);
        return ret;
    }

    py::array_t<float> execute (py::array_t<float> data) {
        py::array_t<float> ret(py::len(data));
        float *x = array_to_ptr<float>(data);
        float *y = array_to_ptr<float>(ret);
        for (auto n = 0; n < py::len(data); n++)
            iirfilt_rrrf_execute (mFilt, x[n], &y[n]);
        return ret;
    }
};

class FMDem
{
    float               mKd;
    std::complex<float> mLastIQ;

public:

    FMDem (float kd) : mLastIQ(0.0,0.0) {
        mKd = kd;
    }

   ~FMDem (void) {
    }

    void reset (void) {
    }

    void print (void) {
    }

    py::array_t<float> demod (py::array_t<std::complex<float>> inp) {
        py::array_t<float> ret(py::len(inp));
        std::complex<float> *x = array_to_ptr<std::complex<float>>(inp);
        float *y = array_to_ptr<float>(ret);
        for (auto n = 0; n < py::len(inp); n++) {
            y[n] = std::arg(x[n] * std::conj(mLastIQ)) * mKd;
            mLastIQ = x[n];
        }
        return ret;
    }
};

std::map<std::string,liquid_ampmodem_type> ampmodem_type_map = 
{
    {"dsb",LIQUID_AMPMODEM_DSB},
    {"usb",LIQUID_AMPMODEM_USB},
    {"lsb",LIQUID_AMPMODEM_LSB}
};

class AmpModem
{
public:

    float       mModulation;
    std::string mType;
    bool        mCarrier;

    ampmodem mModem;


    AmpModem (float mod, std::string type, bool car) : mModulation(mod), mCarrier(car) {
        mModem = makeFromArgs (mod, type, car);
    }

    AmpModem (void) {
    }

   ~AmpModem (void) {
        ampmodem_destroy (mModem);
    }

    void set_type (std::string type) {
        if ( type == "dsb" or type == "usb" or type == "lsb" ) {
            mType = type;
            ampmodem_destroy (mModem);
            mModem = makeFromArgs (mModulation, mType, mCarrier);
        }
    }

    std::string get_type (void) {
        return mType;
    }

    void set_modulation (float mod) {
        mModulation = mod;
        ampmodem_destroy (mModem);
        mModem = makeFromArgs (mModulation, mType, mCarrier);
    }

    float get_modulation (void) {
        return mModulation;
    }

    void set_carrier (bool val) {
        mCarrier = val;
        ampmodem_destroy (mModem);
        mModem = makeFromArgs (mModulation, mType, mCarrier);
    }

    bool get_carrier (void) {
        return mCarrier;
    }

    void print (void) {
        ampmodem_print (mModem);
    }

    void reset (void) {
        ampmodem_reset (mModem);
    }

    py::array_t<float> demod (py::array_t<liquid_float_complex> inp) {
        py::array_t<float> ret(py::len(inp));
        liquid_float_complex *x = array_to_ptr<liquid_float_complex>(inp);
        float *y = array_to_ptr<float>(ret);
        ampmodem_demodulate_block (mModem, x, py::len(inp), y);
        return ret;
    }   
    
    ampmodem makeFromArgs (float mod, std::string type, bool car) {
        liquid_ampmodem_type mt(LIQUID_AMPMODEM_DSB);
        int suppress = (car) ? 0 : 1;
        if ( ampmodem_type_map.find(type) != ampmodem_type_map.end() ) {
            mType = type;
            mt = ampmodem_type_map[type];
        }   
        return ampmodem_create (mod,mt,suppress);
    }
};

// liquids NCO is a phase lock loop. What we need is simply a local 
// oscillator to shift frequency up and down for fine tunning.
class LO 
{
public:
    float               mFreq;
    std::complex<float> mPhase;
    std::complex<float> mPhaseStep;

    LO (float fr) : mPhase(1.0,0.0) {
        set_freq (fr);
    }

    ~LO (void) {}

    float get_freq (void) {
        return mFreq;
    }

    void set_freq (float fr) {
        mFreq = fr;
        mPhaseStep = std::polar(1.0,2*M_PI*fr);
    }

    py::array_t<liquid_float_complex> shift (py::array_t<liquid_float_complex> inp) {
        py::array_t<liquid_float_complex> ret(py::len(inp));
        liquid_float_complex *y = array_to_ptr<liquid_float_complex>(ret);
        for (auto n = 0; n < py::len(inp); n++) {
            y[n] = mPhase * inp.at(n);
            mPhase = mPhase * mPhaseStep;
        }
        mPhase = mPhase / std::abs(mPhase);
        return ret;
    }   
};

std::map<std::string,liquid_ncotype> nco_type_map =
{
    {"nco", LIQUID_NCO},    
    {"vco", LIQUID_VCO}
};

struct NCO 
{
    nco_crcf    mNCO;
    std::string mType;

    NCO (std::string type) {
        if ( type == "nco" ) { 
            mType = "nco";
            mNCO = nco_crcf_create (LIQUID_NCO);
        }
        else {
            mType = "vco";
            mNCO = nco_crcf_create (LIQUID_VCO);
        }
    }

   ~NCO (void) {
        nco_crcf_destroy (mNCO);
    }

    float frequency (void) {
        return nco_crcf_get_frequency (mNCO);
    }

    void set_frequency (float fr) {
        nco_crcf_set_frequency (mNCO,fr);
    }

    void adjust_frequency (float df) {
        nco_crcf_adjust_frequency (mNCO,df);
    }

    float phase (void) {
        return nco_crcf_get_phase (mNCO);
    }

    void set_phase (float phs) {
        nco_crcf_set_phase (mNCO,phs);
    }

    void adjust_phase (float dphs) {
        nco_crcf_adjust_phase (mNCO,dphs);
    }

    void set_pll_bandwidth (float bw) {
        nco_crcf_pll_set_bandwidth (mNCO, bw);
    }

    void pll_step (float dph) {
        nco_crcf_pll_step (mNCO, dph);
    }

    void print (void) {
        nco_crcf_print (mNCO);
    }

    py::array_t<liquid_float_complex> mix_up (py::array_t<liquid_float_complex> inp) {
        py::array_t<liquid_float_complex> ret(py::len(inp));
        liquid_float_complex *x = array_to_ptr<liquid_float_complex>(inp);
        liquid_float_complex *y = array_to_ptr<liquid_float_complex>(ret);
        nco_crcf_mix_block_up (mNCO,x,y,py::len(inp));
        return ret;
    } 

    py::array_t<liquid_float_complex> mix_down (py::array_t<liquid_float_complex> inp) {
        py::array_t<liquid_float_complex> ret(py::len(inp));
        liquid_float_complex *x = array_to_ptr<liquid_float_complex>(inp);
        liquid_float_complex *y = array_to_ptr<liquid_float_complex>(ret);
        nco_crcf_mix_block_down (mNCO,x,y,py::len(inp));
        return ret;
    } 
};

struct AGC 
{
    AGC (void) {
        mSquelch = false;
        mAGC = agc_crcf_create ();
    }

   ~AGC (void) {
        agc_crcf_destroy (mAGC);
    }

    void set_bandwidth (float bw) {
        agc_crcf_set_bandwidth (mAGC,bw);
    }

    float get_bandwidth (void) {
        return agc_crcf_get_bandwidth (mAGC);
    }

    void squelch_enable (void) { 
        mSquelch = true;
        agc_crcf_squelch_enable (mAGC);
    }

    void squelch_disable (void) {
        mSquelch = false;
        agc_crcf_squelch_disable (mAGC);
    }

    void squelch_set_threshold (double t) {
        agc_crcf_squelch_set_threshold (mAGC,t);
    }

    double squelch_get_threshold (void) {
        return agc_crcf_squelch_get_threshold (mAGC);
    }

    int status (void) {
        return agc_crcf_squelch_get_status (mAGC);
    }

    float signal_level (void) {
        return agc_crcf_get_signal_level (mAGC);
    }

    void print (void) {
        agc_crcf_print (mAGC);
    }

    void reset (void) {
        agc_crcf_reset (mAGC);
    }

    py::array_t<liquid_float_complex> execute (py::array_t<liquid_float_complex> inp) {
        py::array_t<liquid_float_complex> ret(py::len(inp));
        liquid_float_complex *x = array_to_ptr<liquid_float_complex>(inp);
        liquid_float_complex *y = array_to_ptr<liquid_float_complex>(ret);
        for (unsigned int n = 0; n < py::len(inp); n++) { 
            agc_crcf_execute (mAGC,x[n],&y[n]);
            if (mSquelch and agc_crcf_squelch_get_status (mAGC) != 3)
                y[n] *= 0.0;
        }
        return ret;
    }

    bool     mSquelch;
    agc_crcf mAGC;
}; 

class Sampler
{
public:

    Sampler (void) {
        mRate = 0.9f;
        mLen = 13;
        mFc = 0.4f;
        mAs = 60.0f;
        mFilt = 13;
        mRsamp = resamp_crcf_create (mRate, mLen, mFc, mAs, mFilt);
    }

    Sampler (float rate, int len, float Fc, float As, int filter) 
    {
        mRate= rate;
        mLen = len;
        mFc = Fc;
        mAs = As;  
        mFilt = filter;
        mRsamp = resamp_crcf_create (mRate, mLen, mFc, mAs, mFilt);
    }

    static Sampler create (float rate, int len, float Fc, float As, int filter) {
        return Sampler (rate, len, Fc, As, filter);
    }

    Sampler (Sampler &&me) {
        mRsamp = resamp_crcf_create (me.mRate, me.mLen, me.mFc, me.mAs, me.mFilt);
    }

    ~Sampler (void) {
        resamp_crcf_destroy (mRsamp);
    }

    py::array_t<liquid_float_complex> execute (py::array_t<liquid_float_complex> inp) {
        int len_in (py::len(inp));
        liquid_float_complex y[len_in];
        liquid_float_complex *x = array_to_ptr<liquid_float_complex>(inp);
        unsigned int nd(0), nw(0);
        for (auto n = 0; n < len_in; n++ ) {
            resamp_crcf_execute (mRsamp, x[n], &y[nw], &nd);
            nw += nd;
        }
        py::array_t<liquid_float_complex> ret(nw);
        liquid_float_complex *z = array_to_ptr<liquid_float_complex>(ret);
        std::copy (y, y+nw, z);
        return ret;
    }

    void print (void) {
        resamp_crcf_print (mRsamp);
    }

    void set_rate (double rate) {
        mRate = rate;
        resamp_crcf_destroy(mRsamp);
        mRsamp = resamp_crcf_create (mRate, mLen, mFc, mAs, mFilt);
        resamp_crcf_set_rate (mRsamp,mRate);
    }

private:

    float       mRate;
    int         mLen;
    float       mFc;
    float       mAs;
    int         mFilt;

    resamp_crcf mRsamp;
};

PYBIND11_MODULE (liquiddsp, m)
{

    m.def ("bytes_to_iq", bytes_to_iq);

    py::class_<Delay>(m, "Delay")
        .def (py::init<int>(), py::arg("nd")=1)
        .def_property ("delay", &Delay::get_delay, &Delay::set_delay) 
        .def ("__call__", &Delay::call);

    py::class_<IIRfilter>(m, "IIRfilter")
        .def (py::init<std::string,std::string,int,float,float,float,float>(), 
            py::arg("filter_type") = "butter",
            py::arg("band_type") = "lowpass",
            py::arg("order") = 2,
            py::arg("Fc") = 0.2f,
            py::arg("F0") = 0.3f,
            py::arg("Ap") = 0.7f,
            py::arg("As") = 60.0)
        .def_readonly ("filter_type", &IIRfilter::mFt)
        .def_readonly ("band_type", &IIRfilter::mBt)
        .def_readonly ("order", &IIRfilter::mOrder)
        .def_readonly ("Fc", &IIRfilter::mFc)
        .def_readonly ("F0", &IIRfilter::mF0)
        .def_readonly ("Ap", &IIRfilter::mAp)
        .def_readonly ("As", &IIRfilter::mAs)
        .def ("freqresponse", &IIRfilter::response)
        .def ("__call__", &IIRfilter::execute)
        .def ("print", &IIRfilter::print);

    py::class_<PopFilter> (m, "PopFilter")
        .def (py::init())
        .def_readwrite ("enable",&PopFilter::mEnable)
        .def_readwrite ("trigger_level", &PopFilter::mTriggerFraction)
        .def_readwrite ("width", &PopFilter::mBlankWidth)
        .def_property  ("Fc", &PopFilter::get_Fc, &PopFilter::set_Fc)
        .def_property  ("delay", &PopFilter::get_delay, &PopFilter::set_delay)
        .def_property  ("peak_filter", &PopFilter::get_powerFilter, &PopFilter::set_powerFilter)
        .def ("__call__", &PopFilter::call);

    py::class_<HilbertTransform>(m, "HilbertTransform") 
        .def (py::init<int,float>(),py::arg("m")=5,py::arg("As")=60.0f)
        .def ("__call__", &HilbertTransform::call);

    py::class_<DeemphasisFilter>(m,"DeemphasisFilter")
        .def (py::init<float>(), py::arg("sample_rate")=48000) 
        .def ("freqresponse", &DeemphasisFilter::response)
        .def ("__call__", &DeemphasisFilter::execute);

    py::class_<FMDem>(m, "FMDem")
        .def (py::init<float>(), py::arg("modulation_index")=0.33)
        .def ("print", &FMDem::print)
        .def ("reset", &FMDem::reset)
        .def ("__call__", &FMDem::demod);

    py::class_<AmpModem>(m,"AmpModem")
        .def (py::init<float,std::string,bool>(),
            py::arg("modulation")=0.75, 
            py::arg("type")="dsb", 
            py::arg("carrier")=false)
        .def_property ("modulation", &AmpModem::get_modulation, &AmpModem::set_modulation)
        .def_property ("type", &AmpModem::get_type,&AmpModem::set_type)
        .def_property ("carrier", &AmpModem::get_carrier,&AmpModem::set_carrier)
        .def ("print", &AmpModem::print)
        .def ("reset", &AmpModem::reset)
        .def ("__call__", &AmpModem::demod);

    py::class_<LO>(m,"LO")
        .def (py::init<float>(),py::arg("freq") = 0.0)
        .def_readwrite ("phase", &LO::mPhase)
        .def_property ("freq", &LO::get_freq, &LO::set_freq)
        .def ("__call__", &LO::shift);

    py::class_<NCO>(m,"NCO")
        .def (py::init<std::string>(), py::arg("type") = "nco")
        .def ("print", &NCO::print)
        .def_property ("freq", &NCO::frequency, &NCO::set_frequency)
        .def ("adjust_frequency", &NCO::adjust_frequency)
        .def ("adjust_phase", &NCO::adjust_phase)
        .def_property ("phase", &NCO::phase, &NCO::set_phase)
        .def ("set_pll_bandwidth", &NCO::set_pll_bandwidth)
        .def ("pll_step", &NCO::pll_step)
        .def ("__call__", &NCO::mix_up)
        .def ("mix_up", &NCO::mix_up)
        .def ("mix_down", &NCO::mix_down);

    py::class_<Sampler>(m,"Sampler")
        .def (py::init<float,int,float,float,int>(),py::arg("rate")=0.9f,py::arg("len")=13,py::arg("Fc")=0.4f,py::arg("As")=20.0f,py::arg("Filt")=13)
        .def ("__call__", &Sampler::execute)
        .def ("set_rate",&Sampler::set_rate)
        .def ("print",&Sampler::print); 

    py::class_<AGC>(m,"AGC")
        .def (py::init())
        .def ("squelch_enable", &AGC::squelch_enable)
        .def ("squelch_disable", &AGC::squelch_disable)
        .def_property ("threshold", &AGC::squelch_get_threshold, &AGC::squelch_set_threshold)
        .def_property ("bandwidth", &AGC::get_bandwidth, &AGC::set_bandwidth)
        .def ("signal_level", &AGC::signal_level)
        .def ("status", &AGC::status)
        .def ("print", &AGC::print)
        .def ("reset", &AGC::reset)
        .def ("__call__", &AGC::execute);
}
