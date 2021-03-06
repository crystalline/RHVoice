/* ----------------------------------------------------------------- */
/*           The HMM-Based Speech Synthesis Engine "hts_engine API"  */
/*           developed by HTS Working Group                          */
/*           http://hts-engine.sourceforge.net/                      */
/* ----------------------------------------------------------------- */
/*                                                                   */
/*  Copyright (c) 2001-2011  Nagoya Institute of Technology          */
/*                           Department of Computer Science          */
/*                                                                   */
/*                2001-2008  Tokyo Institute of Technology           */
/*                           Interdisciplinary Graduate School of    */
/*                           Science and Engineering                 */
/*                                                                   */
/* All rights reserved.                                              */
/*                                                                   */
/* Redistribution and use in source and binary forms, with or        */
/* without modification, are permitted provided that the following   */
/* conditions are met:                                               */
/*                                                                   */
/* - Redistributions of source code must retain the above copyright  */
/*   notice, this list of conditions and the following disclaimer.   */
/* - Redistributions in binary form must reproduce the above         */
/*   copyright notice, this list of conditions and the following     */
/*   disclaimer in the documentation and/or other materials provided */
/*   with the distribution.                                          */
/* - Neither the name of the HTS working group nor the names of its  */
/*   contributors may be used to endorse or promote products derived */
/*   from this software without specific prior written permission.   */
/*                                                                   */
/* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND            */
/* CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,       */
/* INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF          */
/* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE          */
/* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS */
/* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,          */
/* EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED   */
/* TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,     */
/* DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON */
/* ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,   */
/* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY    */
/* OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE           */
/* POSSIBILITY OF SUCH DAMAGE.                                       */
/* ----------------------------------------------------------------- */

#ifndef HTS106_GSTREAM_C
#define HTS106_GSTREAM_C

#ifdef __cplusplus
#define HTS106_GSTREAM_C_START extern "C" {
#define HTS106_GSTREAM_C_END   }
#else
#define HTS106_GSTREAM_C_START
#define HTS106_GSTREAM_C_END
#endif                          /* __CPLUSPLUS */

HTS106_GSTREAM_C_START;

/* hts_engine libraries */
#include "HTS106_hidden.h"

/* HTS106_GStreamSet_initialize: initialize generated parameter stream set */
void HTS106_GStreamSet_initialize(HTS106_GStreamSet * gss)
{
   gss->nstream = 0;
   gss->total_frame = 0;
   gss->total_nsample = 0;
   gss->gstream = NULL;
   gss->gspeech = NULL;
}

/* HTS106_GStreamSet_create: generate speech */
/* (stream[0] == spectrum && stream[1] == lf0) */
HTS106_Boolean HTS106_GStreamSet_create(HTS106_GStreamSet * gss, HTS106_PStreamSet * pss, int stage, HTS106_Boolean use_log_gain, int sampling_rate, int fperiod, double alpha, double beta, HTS106_Boolean * stop, double volume, HTS106_Audio * audio)
{
   int i, j, k;
   int msd_frame;
   HTS106_Vocoder v;
   int nlpf = 0;
   double *lpf = NULL;

   /* check */
   if (gss->gstream || gss->gspeech) {
      HTS106_error(1, "HTS106_GStreamSet_create: HTS106_GStreamSet is not initialized.\n");
      return FALSE;
   }

   /* initialize */
   gss->nstream = HTS106_PStreamSet_get_nstream(pss);
   gss->total_frame = HTS106_PStreamSet_get_total_frame(pss);
   gss->total_nsample = fperiod * gss->total_frame;
   gss->gstream = (HTS106_GStream *) HTS106_calloc(gss->nstream, sizeof(HTS106_GStream));
   for (i = 0; i < gss->nstream; i++) {
      gss->gstream[i].static_length = HTS106_PStreamSet_get_static_length(pss, i);
      gss->gstream[i].par = (double **) HTS106_calloc(gss->total_frame, sizeof(double *));
      for (j = 0; j < gss->total_frame; j++)
         gss->gstream[i].par[j] = (double *) HTS106_calloc(gss->gstream[i].static_length, sizeof(double));
   }
   gss->gspeech = (short *) HTS106_calloc(gss->total_nsample, sizeof(short));

   /* copy generated parameter */
   for (i = 0; i < gss->nstream; i++) {
      if (HTS106_PStreamSet_is_msd(pss, i)) {      /* for MSD */
         for (j = 0, msd_frame = 0; j < gss->total_frame; j++)
            if (HTS106_PStreamSet_get_msd_flag(pss, i, j)) {
               for (k = 0; k < gss->gstream[i].static_length; k++)
                  gss->gstream[i].par[j][k] = HTS106_PStreamSet_get_parameter(pss, i, msd_frame, k);
               msd_frame++;
            } else
               for (k = 0; k < gss->gstream[i].static_length; k++)
                  gss->gstream[i].par[j][k] = LZERO;
      } else {                  /* for non MSD */
         for (j = 0; j < gss->total_frame; j++)
            for (k = 0; k < gss->gstream[i].static_length; k++)
               gss->gstream[i].par[j][k] = HTS106_PStreamSet_get_parameter(pss, i, j, k);
      }
   }

   /* check */
   if (gss->nstream != 2 && gss->nstream != 3) {
      HTS106_error(1, "HTS106_GStreamSet_create: The number of streams should be 2 or 3.\n");
      HTS106_GStreamSet_clear(gss);
      return FALSE;
   }
   if (HTS106_PStreamSet_get_static_length(pss, 1) != 1) {
      HTS106_error(1, "HTS106_GStreamSet_create: The size of lf0 static vector should be 1.\n");
      HTS106_GStreamSet_clear(gss);
      return FALSE;
   }
   if (gss->nstream >= 3 && gss->gstream[2].static_length % 2 == 0) {
      HTS106_error(1, "HTS106_GStreamSet_create: The number of low-pass filter coefficient should be odd numbers.");
      HTS106_GStreamSet_clear(gss);
      return FALSE;
   }

   /* synthesize speech waveform */
   HTS106_Vocoder_initialize(&v, gss->gstream[0].static_length - 1, stage, use_log_gain, sampling_rate, fperiod);
   if (gss->nstream >= 3)
      nlpf = (gss->gstream[2].static_length - 1) / 2;
   for (i = 0; i < gss->total_frame && (*stop) == FALSE; i++) {
      if (gss->nstream >= 3)
         lpf = &gss->gstream[2].par[i][0];
      HTS106_Vocoder_synthesize(&v, gss->gstream[0].static_length - 1, gss->gstream[1].par[i][0], &gss->gstream[0].par[i][0], nlpf, lpf, alpha, beta, volume, &gss->gspeech[i * fperiod], audio);
   }
   HTS106_Vocoder_clear(&v);
   if (audio)
      HTS106_Audio_flush(audio);

   return TRUE;
}

/* HTS106_GStreamSet_get_total_nsample: get total number of sample */
int HTS106_GStreamSet_get_total_nsample(HTS106_GStreamSet * gss)
{
   return gss->total_nsample;
}

/* HTS106_GStreamSet_get_total_frame: get total number of frame */
int HTS106_GStreamSet_get_total_frame(HTS106_GStreamSet * gss)
{
   return gss->total_frame;
}

/* HTS106_GStreamSet_get_static_length: get static features length */
int HTS106_GStreamSet_get_static_length(HTS106_GStreamSet * gss, int stream_index)
{
   return gss->gstream[stream_index].static_length;
}

/* HTS106_GStreamSet_get_speech: get synthesized speech parameter */
short HTS106_GStreamSet_get_speech(HTS106_GStreamSet * gss, int sample_index)
{
   return gss->gspeech[sample_index];
}

/* HTS106_GStreamSet_get_parameter: get generated parameter */
double HTS106_GStreamSet_get_parameter(HTS106_GStreamSet * gss, int stream_index, int frame_index, int vector_index)
{
   return gss->gstream[stream_index].par[frame_index][vector_index];
}

/* HTS106_GStreamSet_clear: free generated parameter stream set */
void HTS106_GStreamSet_clear(HTS106_GStreamSet * gss)
{
   int i, j;

   if (gss->gstream) {
      for (i = 0; i < gss->nstream; i++) {
         for (j = 0; j < gss->total_frame; j++)
            HTS106_free(gss->gstream[i].par[j]);
         HTS106_free(gss->gstream[i].par);
      }
      HTS106_free(gss->gstream);
   }
   if (gss->gspeech)
      HTS106_free(gss->gspeech);
   HTS106_GStreamSet_initialize(gss);
}

HTS106_GSTREAM_C_END;

#endif                          /* !HTS106_GSTREAM_C */
