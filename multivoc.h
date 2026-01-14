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
   file:   MULTIVOC.H

   author: James R. Dose
   date:   December 20, 1993

   Public header for MULTIVOC.C

   (c) Copyright 1993 James R. Dose.  All Rights Reserved.
**********************************************************************/

#ifndef __MULTIVOC_H
#define __MULTIVOC_H

#define MV_MinVoiceHandle 1

extern int MV_ErrorCode;

enum MV_Errors
{
    MV_Warning = -2,
    MV_Error = -1,
    MV_Ok = 0,
    MV_UnsupportedCard,
    MV_NotInstalled,
    MV_NoVoices,
    MV_NoMem,
    MV_VoiceNotFound,
    MV_BlasterError,
    MV_PasError,
    MV_SoundSourceError,
    MV_InvalidVOCFile,
    MV_InvalidWAVFile,
    MV_InvalidMixMode,
    MV_SoundSourceFailure,
    MV_IrqFailure,
    MV_DMAFailure,
    MV_DMA16Failure,
    MV_NullRecordFunction
};

char *MV_ErrorString(int ErrorNumber);
int MV_VoicePlaying(int handle);
int MV_Kill(int handle);
int MV_VoicesPlaying(void);
int MV_SetMixMode(int mode);
void MV_StartPlayback(void);
int MV_StopPlayback(void);
int MV_PlayVOC(char *ptr, int length, int priority);
int MV_Init(int soundcard, int MixRate, int Voices, int MixMode);
int MV_Shutdown(void);

#endif