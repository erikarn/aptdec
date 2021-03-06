/*
 *  Atpdec 
 *  Copyright (c) 2004 by Thierry Leconte (F4DWV)
 *
 *      $Id$
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846	/* for OS that don't know it */
#endif				/*  */
#include "filter.h"
#include "filtercoeff.h"

extern int getsample(float *inbuff, int nb);

#define Fc 2400.0
#define DFc 50.0
#define PixelLine 2080
#define Fp (2*PixelLine)
#define RSMULT 15
#define Fi (Fp*RSMULT)

static double Fe;

static double offset = 0.0;
static double FreqLine = 1.0;

static double FreqOsc;
static double K1,K2;


int init_dsp(double F)
{
if(F > Fi) return (1);
if(F < Fp) return (-1);
Fe=F;

K1=DFc/Fe;
K2=K1*K1/2.0;
FreqOsc=Fc/Fe;

return(0);
}

/* fast phase estimator */
static inline double Phase(double I,double Q)
{
   double angle,r;
   int s;

   if(I==0.0 && Q==0.0)
	return(0.0);

   if (Q < 0)  {
	s=-1;
	Q=-Q;
   } else {
	s=1;
   }
	
   if (I>=0) {
      r = (I - Q) / (I + Q);
      angle = 0.25 - 0.25 * r;
   } else {
      r = (I + Q) / (Q - I);
      angle = 0.75 - 0.25 * r;
   }

  if(s>0)
  	return(angle);
  else
  	return(-angle);
}

static double pll(double I, double Q)
{

/* pll coeff */
    static double PhaseOsc = 0.0;
    double Io, Qo;
    double Ip, Qp;
    double DPhi;

/* quadrature oscillator */
    Io = cos(PhaseOsc);
    Qo = sin(PhaseOsc);

/* phase detector */
    Ip = I*Io+Q*Qo;
    Qp = Q*Io-I*Qo;
    DPhi = Phase(Ip,Qp);

/*  loop filter  */

    PhaseOsc += 2.0 * M_PI * (K1 * DPhi + FreqOsc);
    if (PhaseOsc > M_PI)
	PhaseOsc -= 2.0 * M_PI;
    if (PhaseOsc <= -M_PI)
	PhaseOsc += 2.0 * M_PI;

    FreqOsc += K2 * DPhi;
    if (FreqOsc > ((Fc + DFc) / Fe))
	FreqOsc = (Fc + DFc) / Fe;
    if (FreqOsc < ((Fc - DFc) / Fe))
	FreqOsc = (Fc - DFc) / Fe;

    return (Ip);
}

static int getamp(double *ambuff, int nb)
{

#define BLKIN 1024
    static float inbuff[BLKIN];
    static int idxin=0;
    static int nin=0;

    int n;

    for (n = 0; n < nb; n++) {
    	double I,Q;

	if (nin < IQFilterLen*2+2) {
	    int res;
	    memmove(inbuff, &(inbuff[idxin]), nin * sizeof(float));
	    idxin = 0;
    	    res = getsample(&(inbuff[nin]), BLKIN - nin);
	    nin += res;
	    if (nin < IQFilterLen*2+2)
		return (n);
	}

    	iqfir(&inbuff[idxin],iqfilter,IQFilterLen,&I,&Q);
	ambuff[n] = pll(I,Q);

	idxin += 1;
	nin -= 1;
    }
    return (n);
}

int getpixelv(float *pvbuff, int nb)
{

#define BLKAMP 1024
    static double ambuff[BLKAMP];
    static int nam = 0;
    static int idxam = 0;

    int n,m;
    double mult;

    mult = (double) Fi/Fe*FreqLine;

    m=RSFilterLen/mult+1;

    for (n = 0; n < nb; n++) {
	int shift;

	if (nam < m) {
	    int res;
	    memmove(ambuff, &(ambuff[idxam]), nam * sizeof(double));
	    idxam = 0;
	    res = getamp(&(ambuff[nam]), BLKAMP - nam);
	    nam += res;
	    if (nam < m)
		return (n);
	}

	pvbuff[n] = rsfir(&(ambuff[idxam]), rsfilter, RSFilterLen, offset, mult) * mult * 256.0;

//printf("%g\n",pvbuff[n]);

	shift = ((int) floor((RSMULT - offset) / mult))+1;
	offset = shift*mult+offset-RSMULT ;

	idxam += shift;
	nam -= shift;
    }
    return (nb);
}

int getpixelrow(float *pixelv)
{
    static float pixels[PixelLine + SyncFilterLen];
    static int npv = 0;
    static int synced = 0;
    static double max = 0.0;

    double corr, ecorr, lcorr;
    int res;

    if (npv > 0)
	memmove(pixelv, pixels, npv * sizeof(float));
    if (npv < SyncFilterLen + 2) {
	res = getpixelv(&(pixelv[npv]), SyncFilterLen + 2 - npv);
	npv += res;
	if (npv < SyncFilterLen + 2)
	    return (0);
    }

    /* test sync */
    ecorr = fir(pixelv, Sync, SyncFilterLen);
    corr = fir(&(pixelv[1]), Sync, SyncFilterLen);
    lcorr = fir(&(pixelv[2]), Sync, SyncFilterLen);
    FreqLine = 1.0+((ecorr-lcorr) / corr / PixelLine / 4.0);
    if (corr < 0.75 * max) {
	synced = 0;
	FreqLine = 1.0;
    }
    max = corr;
    if (synced < 8) {
	int shift, mshift;

	if (npv < PixelLine + SyncFilterLen) {
	    res =
		getpixelv(&(pixelv[npv]), PixelLine + SyncFilterLen - npv);
	    npv += res;
	    if (npv < PixelLine + SyncFilterLen)
		return (0);
	}

	/* lookup sync start */
	mshift = 0;
	for (shift = 1; shift < PixelLine; shift++) {
	    double corr;

	    corr = fir(&(pixelv[shift + 1]), Sync, SyncFilterLen);
	    if (corr > max) {
		mshift = shift;
		max = corr;
	    }
	}
	if (mshift != 0) {
	    memmove(pixelv, &(pixelv[mshift]),
		    (npv - mshift) * sizeof(float));
	    npv -= mshift;
	    synced = 0;
	    FreqLine = 1.0;
	} else
	    synced += 1;
    }
    if (npv < PixelLine) {
	res = getpixelv(&(pixelv[npv]), PixelLine - npv);
	npv += res;
	if (npv < PixelLine)
	    return (0);
    }
    if (npv == PixelLine) {
	npv = 0;
    } else {
	memmove(pixels, &(pixelv[PixelLine]),
		(npv - PixelLine) * sizeof(float));
	npv -= PixelLine;
    }

    return (1);
}
