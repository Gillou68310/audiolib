/*
Copyright (C) 1994-1995 Apogee Software, Ltd.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
/**********************************************************************
   file:   _MULTIVC.H

   author: James R. Dose
   date:   December 20, 1993

   Private header for MULTIVOC.C

   (c) Copyright 1993 James R. Dose.  All Rights Reserved.
**********************************************************************/

#ifndef ___MULTIVC_H
#define ___MULTIVC_H

#define TRUE (1 == 1)
#define FALSE (!TRUE)

#define VOC_8BIT 0x0
#define VOC_CT4_ADPCM 0x1
#define VOC_CT3_ADPCM 0x2
#define VOC_CT2_ADPCM 0x3
#define VOC_16BIT 0x4
#define VOC_ALAW 0x6
#define VOC_MULAW 0x7
#define VOC_CREATIVE_ADPCM 0x200

#define T_SIXTEENBIT_STEREO 0
#define T_8BITS 1
#define T_MONO 2
#define T_16BITSOURCE 4
#define T_LEFTQUIET 8
#define T_RIGHTQUIET 16
#define T_DEFAULT T_SIXTEENBIT_STEREO

#define SILENCE_16BIT 0
#define SILENCE_8BIT 0x80808080

#define MixBufferSize 128
#define NumberOfBuffers 4
#define TotalBufferSize (MixBufferSize * NumberOfBuffers)

typedef struct VoiceNode
{
    struct VoiceNode *next;
    struct VoiceNode *prev;

    char *unk8;
    unsigned int length;
    int unkE;
    int unk10[NumberOfBuffers];
    int unk18[NumberOfBuffers];
    int Active[NumberOfBuffers];
    int handle;
    int priority;
} VoiceNode;

static void MV_ServiceVoc(void);
static VoiceNode *MV_GetVoice(int handle);
static VoiceNode *MV_AllocVoice(int priority);
void sub_2A110(char *to, char *from, int len);
void sub_2A1B1(char *to, char *from, int len);
void sub_2A252(char *to, char *from, int len, int shift);
void sub_2A333(char *to, char *from, int len, int shift);

#endif