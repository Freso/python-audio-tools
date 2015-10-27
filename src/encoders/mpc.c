#include "../mpc/datatypes.h"
#include "../mpc/mpcdec.h"
#include "../mpc/mpcmath.h"
#include "../mpc/minimax.h"
#include "../libmpcpsy/libmpcpsy.h"
#include "../libmpcenc/libmpcenc.h"
#include "../pcmreader.h"

// Utility macros
#ifndef MIN
#define MIN(X,Y) (((X) < (Y)) ? (X) : (Y))
#endif
#define P(new,old)  Penalty [128 + (old) - (new)]

// Version macros for encoder info block
#define MPCENC_MAJOR 1
#define MPCENC_MINOR 30
#define MPCENC_BUILD 1

typedef float SCFTriple[3];

typedef enum {
    ENCODE_OK,
    ERR_INVALID_ARGUMENT,
    ERR_UNSUPPORTED_SAMPLE_RATE,
    ERR_UNSUPPORTED_CHANNELS,
    ERR_UNSUPPORTED_BITS_PER_SAMPLE,
    ERR_FILE_OPEN,
    ERR_FILE_READ
} result_t;

static const unsigned char  Penalty [256] = {
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
      0,  2,  5,  9, 15, 23, 36, 54, 79,116,169,246,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
};

static void
SCF_Extraktion ( PsyModel*m, mpc_encoder_t* e, const int MaxBand, SubbandFloatTyp* x )
{
    int    Band;
    int    n;
    int    d01;
    int    d12;
    int    d02;
    int    warnL;
    int    warnR;
    int*   scfL;
    int*   scfR;
    int    comp_L [3];
    int    comp_R [3];
    float  tmp_L  [3];
    float  tmp_R  [3];
    float  facL;
    float  facR;
    float  L;
    float  R;
    float  SL;
    float  SR;

    for ( Band = 0; Band <= MaxBand; Band++ ) {         // Suche nach Maxima
        L  = FABS (x[Band].L[ 0]);
        R  = FABS (x[Band].R[ 0]);
        SL = x[Band].L[ 0] * x[Band].L[ 0];
        SR = x[Band].R[ 0] * x[Band].R[ 0];
        for ( n = 1; n < 12; n++ ) {
            if (L < FABS (x[Band].L[n])) L = FABS (x[Band].L[n]);
            if (R < FABS (x[Band].R[n])) R = FABS (x[Band].R[n]);
            SL += x[Band].L[n] * x[Band].L[n];
            SR += x[Band].R[n] * x[Band].R[n];
        }
        Power_L [Band][0] = SL;
        Power_R [Band][0] = SR;
        tmp_L [0] = L;
        tmp_R [0] = R;

        L  = FABS (x[Band].L[12]);
        R  = FABS (x[Band].R[12]);
        SL = x[Band].L[12] * x[Band].L[12];
        SR = x[Band].R[12] * x[Band].R[12];
        for ( n = 13; n < 24; n++ ) {
            if (L < FABS (x[Band].L[n])) L = FABS (x[Band].L[n]);
            if (R < FABS (x[Band].R[n])) R = FABS (x[Band].R[n]);
            SL += x[Band].L[n] * x[Band].L[n];
            SR += x[Band].R[n] * x[Band].R[n];
        }
        Power_L [Band][1] = SL;
        Power_R [Band][1] = SR;
        tmp_L [1] = L;
        tmp_R [1] = R;

        L  = FABS (x[Band].L[24]);
        R  = FABS (x[Band].R[24]);
        SL = x[Band].L[24] * x[Band].L[24];
        SR = x[Band].R[24] * x[Band].R[24];
        for ( n = 25; n < 36; n++ ) {
            if (L < FABS (x[Band].L[n])) L = FABS (x[Band].L[n]);
            if (R < FABS (x[Band].R[n])) R = FABS (x[Band].R[n]);
            SL += x[Band].L[n] * x[Band].L[n];
            SR += x[Band].R[n] * x[Band].R[n];
        }
        Power_L [Band][2] = SL;
        Power_R [Band][2] = SR;
        tmp_L [2] = L;
        tmp_R [2] = R;

        // calculation of the scalefactor-indexes
        // -12.6f*log10(x)+57.8945021823f = -10*log10(x/32767)*1.26+1
        // normalize maximum of +/- 32767 to prevent quantizer overflow
        // It can stand a maximum of +/- 32768 ...

        // Where is scf{R,L} [0...2] initialized ???
        scfL = e->SCF_Index_L [Band];
		scfR = e->SCF_Index_R [Band];
        if (tmp_L [0] > 0.) scfL [0] = IFLOORF (-12.6f * LOG10 (tmp_L [0]) + 57.8945021823f );
        if (tmp_L [1] > 0.) scfL [1] = IFLOORF (-12.6f * LOG10 (tmp_L [1]) + 57.8945021823f );
        if (tmp_L [2] > 0.) scfL [2] = IFLOORF (-12.6f * LOG10 (tmp_L [2]) + 57.8945021823f );
        if (tmp_R [0] > 0.) scfR [0] = IFLOORF (-12.6f * LOG10 (tmp_R [0]) + 57.8945021823f );
        if (tmp_R [1] > 0.) scfR [1] = IFLOORF (-12.6f * LOG10 (tmp_R [1]) + 57.8945021823f );
        if (tmp_R [2] > 0.) scfR [2] = IFLOORF (-12.6f * LOG10 (tmp_R [2]) + 57.8945021823f );

        // restriction to SCF_Index = -6...121, make note of the internal overflow
        warnL = warnR = 0;
		for( n = 0; n < 3; n++){
			if (scfL[n] < -6) scfL[n] = -6, warnL = 1;
			if (scfL[n] > 121) scfL[n] = 121, warnL = 1;
			if (scfR[n] < -6) scfR[n] = -6, warnR = 1;
			if (scfR[n] > 121) scfR[n] = 121, warnR = 1;
		}

        // save old values for compensation calculation
        comp_L[0] = scfL[0]; comp_L[1] = scfL[1]; comp_L[2] = scfL[2];
        comp_R[0] = scfR[0]; comp_R[1] = scfR[1]; comp_R[2] = scfR[2];

        // determination and replacement of scalefactors of minor differences with the smaller one???
        // a smaller one is quantized more roughly, i.e. the noise gets amplified???

        if ( m->CombPenalities >= 0 ) {
            if      ( P(scfL[0],scfL[1]) + P(scfL[0],scfL[2]) <= m->CombPenalities ) scfL[2] = scfL[1] = scfL[0];
            else if ( P(scfL[1],scfL[0]) + P(scfL[1],scfL[2]) <= m->CombPenalities ) scfL[0] = scfL[2] = scfL[1];
            else if ( P(scfL[2],scfL[0]) + P(scfL[2],scfL[1]) <= m->CombPenalities ) scfL[0] = scfL[1] = scfL[2];
            else if ( P(scfL[0],scfL[1])                      <= m->CombPenalities ) scfL[1] = scfL[0];
            else if ( P(scfL[1],scfL[0])                      <= m->CombPenalities ) scfL[0] = scfL[1];
            else if ( P(scfL[1],scfL[2])                      <= m->CombPenalities ) scfL[2] = scfL[1];
            else if ( P(scfL[2],scfL[1])                      <= m->CombPenalities ) scfL[1] = scfL[2];

            if      ( P(scfR[0],scfR[1]) + P(scfR[0],scfR[2]) <= m->CombPenalities ) scfR[2] = scfR[1] = scfR[0];
            else if ( P(scfR[1],scfR[0]) + P(scfR[1],scfR[2]) <= m->CombPenalities ) scfR[0] = scfR[2] = scfR[1];
            else if ( P(scfR[2],scfR[0]) + P(scfR[2],scfR[1]) <= m->CombPenalities ) scfR[0] = scfR[1] = scfR[2];
            else if ( P(scfR[0],scfR[1])                      <= m->CombPenalities ) scfR[1] = scfR[0];
            else if ( P(scfR[1],scfR[0])                      <= m->CombPenalities ) scfR[0] = scfR[1];
            else if ( P(scfR[1],scfR[2])                      <= m->CombPenalities ) scfR[2] = scfR[1];
            else if ( P(scfR[2],scfR[1])                      <= m->CombPenalities ) scfR[1] = scfR[2];
        }
        else {

            d12  = scfL [2] - scfL [1];
            d01  = scfL [1] - scfL [0];
            d02  = scfL [2] - scfL [0];

            if      ( 0 < d12  &&  d12 < 5 ) scfL [2] = scfL [1];
            else if (-3 < d12  &&  d12 < 0 ) scfL [1] = scfL [2];
            else if ( 0 < d01  &&  d01 < 5 ) scfL [1] = scfL [0];
            else if (-3 < d01  &&  d01 < 0 ) scfL [0] = scfL [1];
            else if ( 0 < d02  &&  d02 < 4 ) scfL [2] = scfL [0];
            else if (-2 < d02  &&  d02 < 0 ) scfL [0] = scfL [2];

            d12  = scfR [2] - scfR [1];
            d01  = scfR [1] - scfR [0];
            d02  = scfR [2] - scfR [0];

            if      ( 0 < d12  &&  d12 < 5 ) scfR [2] = scfR [1];
            else if (-3 < d12  &&  d12 < 0 ) scfR [1] = scfR [2];
            else if ( 0 < d01  &&  d01 < 5 ) scfR [1] = scfR [0];
            else if (-3 < d01  &&  d01 < 0 ) scfR [0] = scfR [1];
            else if ( 0 < d02  &&  d02 < 4 ) scfR [2] = scfR [0];
            else if (-2 < d02  &&  d02 < 0 ) scfR [0] = scfR [2];
        }

        // calculate SNR-compensation
        tmp_L [0]         = invSCF [comp_L[0] - scfL[0]];
        tmp_L [1]         = invSCF [comp_L[1] - scfL[1]];
        tmp_L [2]         = invSCF [comp_L[2] - scfL[2]];
        tmp_R [0]         = invSCF [comp_R[0] - scfR[0]];
        tmp_R [1]         = invSCF [comp_R[1] - scfR[1]];
        tmp_R [2]         = invSCF [comp_R[2] - scfR[2]];
        m->SNR_comp_L [Band] = (tmp_L[0]*tmp_L[0] + tmp_L[1]*tmp_L[1] + tmp_L[2]*tmp_L[2]) * 0.3333333333f;
        m->SNR_comp_R [Band] = (tmp_R[0]*tmp_R[0] + tmp_R[1]*tmp_R[1] + tmp_R[2]*tmp_R[2]) * 0.3333333333f;

        // normalize the subband samples
        facL = invSCF[scfL[0]];
        facR = invSCF[scfR[0]];
        for ( n = 0; n < 12; n++ ) {
            x[Band].L[n] *= facL;
            x[Band].R[n] *= facR;
        }
        facL = invSCF[scfL[1]];
        facR = invSCF[scfR[1]];
        for ( n = 12; n < 24; n++ ) {
            x[Band].L[n] *= facL;
            x[Band].R[n] *= facR;
        }
        facL = invSCF[scfL[2]];
        facR = invSCF[scfR[2]];
        for ( n = 24; n < 36; n++ ) {
            x[Band].L[n] *= facL;
            x[Band].R[n] *= facR;
        }

        // limit to +/-32767 if internal clipping
        if ( warnL )
            for ( n = 0; n < 36; n++ ) {
                if      (x[Band].L[n] > +32767.f) {
                    e->Overflows++;
                    MaxOverFlow = maxf (MaxOverFlow,  x[Band].L[n]);
                    x[Band].L[n] = 32767.f;
                }
                else if (x[Band].L[n] < -32767.f) {
					e->Overflows++;
                    MaxOverFlow = maxf (MaxOverFlow, -x[Band].L[n]);
                    x[Band].L[n] = -32767.f;
                }
            }
        if ( warnR )
            for ( n = 0; n < 36; n++ ) {
                if      (x[Band].R[n] > +32767.f) {
					e->Overflows++;
                    MaxOverFlow = maxf (MaxOverFlow,  x[Band].R[n]);
                    x[Band].R[n] = 32767.f;
                }
                else if (x[Band].R[n] < -32767.f) {
					e->Overflows++;
                    MaxOverFlow = maxf (MaxOverFlow, -x[Band].R[n]);
                    x[Band].R[n] = -32767.f;
                }
            }
    }
    return;
}

static void
Quantisierung ( PsyModel * m,
				const int               MaxBand,
                const int*              resL,
                const int*              resR,
                const SubbandFloatTyp*  subx,
				mpc_quantizer*          subq )
{
    static float  errorL [32] [36 + MAX_NS_ORDER];
    static float  errorR [32] [36 + MAX_NS_ORDER];
    int           Band;

    // quantize Subband- and Subframe-samples
    for ( Band = 0; Band <= MaxBand; Band++, resL++, resR++ ) {

        if ( *resL > 0 ) {
            if ( m->NS_Order_L [Band] > 0 ) {
                QuantizeSubbandWithNoiseShaping ( subq[Band].L, subx[Band].L, *resL, errorL [Band], m->FIR_L [Band] );
                memcpy ( errorL [Band], errorL[Band] + 36, MAX_NS_ORDER * sizeof (**errorL) );
            } else {
				QuantizeSubband                 ( subq[Band].L, subx[Band].L, *resL, errorL [Band], MAX_NS_ORDER );
                memcpy ( errorL [Band], errorL[Band] + 36, MAX_NS_ORDER * sizeof (**errorL) );
            }
        }

        if ( *resR > 0 ) {
            if ( m->NS_Order_R [Band] > 0 ) {
                QuantizeSubbandWithNoiseShaping ( subq[Band].R, subx[Band].R, *resR, errorR [Band], m->FIR_R [Band] );
                memcpy ( errorR [Band], errorR [Band] + 36, MAX_NS_ORDER * sizeof (**errorL) );
            } else {
				QuantizeSubband                 ( subq[Band].R, subx[Band].R, *resR, errorL [Band], MAX_NS_ORDER);
                memcpy ( errorR [Band], errorR [Band] + 36, MAX_NS_ORDER * sizeof (**errorL) );
            }
        }
    }
    return;
}

static int
PNS_SCF ( int* scf, float S0, float S1, float S2 )
{
//    printf ("%7.1f %7.1f %7.1f  ", sqrt(S0/12), sqrt(S1/12), sqrt(S2/12) );

#if 1
    if ( S0 < 0.5 * S1  ||  S1 < 0.5 * S2  ||  S0 < 0.5 * S2 )
        return 0;

    if ( S1 < 0.25 * S0  ||  S2 < 0.25 * S1  ||  S2 < 0.25 * S0 )
        return 0;
#endif


    if ( S0 >= 0.8 * S1 ) {
        if ( S0 >= 0.8 * S2  &&  S1 > 0.8 * S2 )
            S0 = S1 = S2 = 0.33333333333f * (S0 + S1 + S2);
        else
            S0 = S1 = 0.5f * (S0 + S1);
    }
    else {
        if ( S1 >= 0.8 * S2 )
            S1 = S2 = 0.5f * (S1 + S2);
    }

    scf [0] = scf [1] = scf [2] = 63;
    S0 = sqrt (S0/12 * 4/1.2005080577484075047860806747022);
    S1 = sqrt (S1/12 * 4/1.2005080577484075047860806747022);
    S2 = sqrt (S2/12 * 4/1.2005080577484075047860806747022);
    if (S0 > 0.) scf [0] = IFLOORF (-12.6f * LOG10 (S0) + 57.8945021823f );
    if (S1 > 0.) scf [1] = IFLOORF (-12.6f * LOG10 (S1) + 57.8945021823f );
    if (S2 > 0.) scf [2] = IFLOORF (-12.6f * LOG10 (S2) + 57.8945021823f );

    if ( scf[0] & ~63 ) scf[0] = scf[0] > 63 ? 63 : 0;
    if ( scf[1] & ~63 ) scf[1] = scf[1] > 63 ? 63 : 0;
    if ( scf[2] & ~63 ) scf[2] = scf[2] > 63 ? 63 : 0;

    return 1;
}

static void
Allocate ( const int MaxBand, int* res, float* x, int* scf, const float* comp, const float* smr, const SCFTriple* Pow, const int* Transient, const float PNS )
{
    const unsigned int LAST_HUFFMAN = 7;
    const float SCFfac = 0.832980664785f;

    int    Band;
    int    k;
    float  tmpMNR;      // to adjust the scalefactors
    float  save [36];   // to adjust the scalefactors
    float  MNR;         // Mask-to-Noise ratio

    for ( Band = 0; Band <= MaxBand; Band++, res++, comp++, smr++, scf += 3, x += 72 ) {
        // printf ( "%2u: %u\n", Band, Transient[Band] );

        // Find out needed quantization resolution Res to fulfill the calculated MNR
        // This is done by exactly measuring the quantization residuals against the signal itself
        // Starting with Res=1  Res in increased until MNR becomes less than 1.
        if ( Band > 0  &&  res[-1] < 3  &&  *smr >= 1. &&  *smr < Band * PNS  &&
             PNS_SCF ( scf, Pow [Band][0], Pow [Band][1], Pow [Band][2] ) ) {
            *res = -1;
        } else {
            for ( MNR = *smr * 1.; MNR > 1.  &&  *res != 15; )
                MNR = *smr * (Transient[Band] ? ISNR_Schaetzer_Trans : ISNR_Schaetzer) ( x, *comp, ++*res );
        }

        // Fine adapt SCF's (MNR > 0 prevents adaption of zero samples, which is nonsense)
        // only apply to Huffman-coded samples (otherwise no savings in bitrate)
        if ( *res > 0  &&  *res <= LAST_HUFFMAN  &&  MNR < 1.  &&  MNR > 0.  &&  !Transient[Band] ) {
            while ( scf[0] > 0  &&  scf[1] > 0  &&  scf[2] > 0 ) {

                --scf[2]; --scf[1]; --scf[0];                   // adapt scalefactors and samples
                memcpy ( save, x, sizeof save );
                for (k = 0; k < 36; k++ )
                    x[k] *= SCFfac;

                tmpMNR = *smr * (Transient[Band] ? ISNR_Schaetzer_Trans : ISNR_Schaetzer) ( x, *comp, *res );// recalculate MNR

                // FK: if ( tmpMNR > MNR  &&  tmpMNR <= 1 ) {          // check for MNR
                if ( tmpMNR <= 1 ) {                            // check for MNR
                    MNR = tmpMNR;
                }
                else {
                    ++scf[0]; ++scf[1]; ++scf[2];               // restore scalefactors and samples
                    memcpy ( x, save, sizeof save );
                    break;
                }
            }
        }

    }
    return;
}

static void fill_float(float *p, float f, unsigned n) {
    unsigned i;
    for( i = 0 ; i < n ; ++i ) {
        p[i] = f;
    }
}

static int
read_pcm_samples(struct PCMReader *pcmreader,
                 PCMDataTyp *out,
                 unsigned samples,
                 int *silence) {
    // Special multipliers used to adjust left / right.
    const float DENORMAL_FIX_LEFT = 32.0f * 1024.0f / 16777216.0f;
    const float DENORMAL_FIX_RIGHT = DENORMAL_FIX_LEFT * 0.5f;

    int buffer[samples * pcmreader->channels];
    unsigned samples_read;
    unsigned i;
    float *l = out->L + CENTER;
    float *r = out->R + CENTER;
    float *m = out->M + CENTER;
    float *s = out->S + CENTER;

    // read PCM samples.
    if((samples_read = pcmreader->read(pcmreader, samples, buffer)) == 0) {
        return -1;
    }

    // check for silence (all null samples)
    silence[0] = 1;
    for( i = 0 ; i < samples_read * pcmreader->channels ; ++i ) {
        if(buffer[i]) {
            silence[0] = 0;
            break;
        }
    }

    // pad buffer with null samples if it wasn't filled completely
    memset(&buffer[samples_read * pcmreader->channels],
           0,
           (samples - samples_read) * pcmreader->channels * sizeof(int));

    // TODO: support conversion from bits_per_samples other than 16?
    switch(pcmreader->channels) {
        case 1:
            for( i = 0 ; i < samples ; ++i ) {
                l[i] = buffer[i] * DENORMAL_FIX_LEFT;
                l[i] = buffer[i] * DENORMAL_FIX_RIGHT;
                m[i] = (l[i] + r[i]) * 0.5f;
                s[i] = (l[i] - r[i]) * 0.5f;
            }
            break;

        case 2:
            for( i = 0 ; i < samples ; ++i ) {
                l[i] = buffer[i * 2 + 0] * DENORMAL_FIX_LEFT;
                r[i] = buffer[i * 2 + 1] * DENORMAL_FIX_RIGHT;
                m[i] = (l[i] + r[i]) * 0.5f;
                s[i] = (l[i] - r[i]) * 0.5f;
            }
            break;
    }

    return samples_read;
}

static result_t
encode_mpc_file(char *filename,
                struct PCMReader *pcmreader,
                float quality,
                unsigned total_pcm_samples)
{
    // Constant configuration values (same defaults as reference encoder)
    const unsigned int FramesBlockPwr = 6;
    const unsigned int SeekDistance = 1;

    FILE *f;
    PsyModel m;
    mpc_encoder_t e;
    int si_size;
    PCMDataTyp Main;
    unsigned samples_read;
    int silence;
    unsigned total_samples_read;
    SubbandFloatTyp X[32];
    int old_silence;
    unsigned N;
    SMRTyp SMR;
    int TransientL [PART_SHORT];
    int TransientR [PART_SHORT];
    int Transient [32];

    // check arguments
    if(filename == NULL    ||
       filename[0] == '\0' ||
       pcmreader == NULL   ||
       quality < 00.0f     ||
       quality > 10.0f     ||
       total_pcm_samples < 1) {
        return ERR_INVALID_ARGUMENT;
    }

    // Check for supported sample rates.
    switch(pcmreader->sample_rate) {
        case 32000:
        case 37800:
        case 44100:
        case 48000: break;
        default:    return ERR_UNSUPPORTED_SAMPLE_RATE;
    }

    // Check for supported channels.
    switch(pcmreader->channels) {
        case 1:
        case 2:  break;
        default: return ERR_UNSUPPORTED_CHANNELS;
    }

    // Check for supported bits per sample.
    // TODO: add 8-32 bit support? the reference encoder supports it.
    switch(pcmreader->bits_per_sample) {
        case 16: break;
        default: return ERR_UNSUPPORTED_BITS_PER_SAMPLE;
    }

    // open output file for writing
    if((f = fopen(filename, "wb")) == NULL) {
        return ERR_FILE_OPEN;
    }

    // Initialize encoder objects.
    m.SCF_Index_L = e.SCF_Index_L;
    m.SCF_Index_R = e.SCF_Index_R;
    Init_Psychoakustik(&m);
    m.SampleFreq = pcmreader->sample_rate;
    SetQualityParams(&m, quality);
    mpc_encoder_init(&e, total_pcm_samples, FramesBlockPwr, SeekDistance);
    Init_Psychoakustiktabellen(&m);
    e.outputFile = f;
    e.MS_Channelmode = m.MS_Channelmode;
    e.seek_ref = ftell(f);

    // write stream header block
    writeMagic(&e);
    writeStreamInfo(&e,
                    m.Max_Band,
                    m.MS_Channelmode > 0,
                    total_pcm_samples,
                    0,
                    m.SampleFreq,
                    pcmreader->channels);
    si_size = writeBlock(&e, "SH", MPC_TRUE, 0);

    // write replay gain block
    writeGainInfo(&e, 0, 0, 0, 0);
    writeBlock(&e, "RG", MPC_FALSE, 0);

    // write encoder information block
    writeEncoderInfo(&e,
                     m.FullQual,
                     m.PNS > 0,
                     MPCENC_MAJOR,
                     MPCENC_MINOR,
                     MPCENC_BUILD);
    writeBlock(&e, "EI", MPC_FALSE, 0);

    // reserve space for seek offset.
    e.seek_ptr = ftell(f);
    writeBits(&e, 0, 16);
    writeBits(&e, 0, 24);
    writeBlock(&e, "SO", MPC_FALSE, 0);

    // Read the first audio block. At least one will be read.
    memset(&Main, 0, sizeof(Main));
    samples_read = read_pcm_samples(pcmreader,
                                    &Main,
                                    MIN(BLOCK, total_pcm_samples),
                                    &silence);
    total_samples_read = samples_read;

    // Check for read error.
    if(samples_read == -1) {
        return ERR_FILE_READ;
    }

    if(samples_read > 0) {
        fill_float(Main.L, Main.L[CENTER], CENTER);
        fill_float(Main.R, Main.R[CENTER], CENTER);
        fill_float(Main.M, Main.M[CENTER], CENTER);
        fill_float(Main.S, Main.S[CENTER], CENTER);
    }

    Analyse_Init(Main.L[CENTER], Main.R[CENTER], X, m.Max_Band);

    old_silence = 0;

    for( N = 0 ; N < total_pcm_samples + MPC_DECODER_SYNTH_DELAY ; N += BLOCK ) {
        if(samples_read < BLOCK && N > 0) {
            fill_float(&Main.L[CENTER + samples_read], Main.L[CENTER + samples_read - 1], BLOCK - samples_read);
            fill_float(&Main.R[CENTER + samples_read], Main.R[CENTER + samples_read - 1], BLOCK - samples_read);
            fill_float(&Main.M[CENTER + samples_read], Main.M[CENTER + samples_read - 1], BLOCK - samples_read);
            fill_float(&Main.S[CENTER + samples_read], Main.S[CENTER + samples_read - 1], BLOCK - samples_read);
        }

        memset(e.Res_L, 0, sizeof(e.Res_L));
        memset(e.Res_R, 0, sizeof(e.Res_R));

        if(!silence || !old_silence) {
            Analyse_Filter(&Main, X, m.Max_Band);
            SMR = Psychoakustisches_Modell(&m, m.Max_Band*0+31, &Main, TransientL, TransientR);
            if(m.minSMR > 0) {
                RaiseSMR(&m, m.Max_Band, &SMR);
            }
            if(m.MS_Channelmode > 0) {
                MS_LR_Entscheidung(m.Max_Band, e.MS_Flag, &SMR, X);
            }
        }
    }

    return ENCODE_OK;
}

#ifndef STANDALONE
PyObject*
encoders_encode_mpc(PyObject *dummy, PyObject *args, PyObject *keywds)
{
    Py_INCREF(Py_None);
    return Py_None;
}
#endif

#ifdef STANDALONE
int main(int argc, char *argv[])
{
    return 0;
}
#endif
