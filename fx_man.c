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
   module: FX_MAN.C

   author: James R. Dose
   date:   March 17, 1994

   Device independant sound effect routines.

   (c) Copyright 1994 James R. Dose.  All Rights Reserved.
**********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include "sndcards.h"
#include "multivoc.h"
#include "blaster.h"
#include "_blaster.h"
#include "pas16.h"
#include "sndsrc.h"
#include "fx_man.h"

#define TRUE (1 == 1)
#define FALSE (!TRUE)

int FX_NumVoices = 4;
int FX_SampleBits = 8;
int FX_SoundDevice = 0;
int FX_ErrorCode = FX_Ok;

#define FX_SetErrorCode(status) \
    FX_ErrorCode = (status);

/*---------------------------------------------------------------------
   Function: FX_ErrorString

   Returns a pointer to the error message associated with an error
   number.  A -1 returns a pointer the current error.
---------------------------------------------------------------------*/

char *FX_ErrorString(
    int ErrorNumber)

{
    char *ErrorString;

    switch (ErrorNumber)
    {
    case FX_Error:
        ErrorString = FX_ErrorString(FX_ErrorCode);
        break;

    case FX_Ok:
        ErrorString = "Fx ok.";
        break;

    case FX_SoundCardError:
        switch (FX_SoundDevice)
        {
        case SoundBlaster:
            ErrorString = BLASTER_ErrorString(BLASTER_Error);
            break;

        case ProAudioSpectrum:
            ErrorString = PAS_ErrorString(PAS_Error);
            break;

        case TandySoundSource:
            ErrorString = SS_ErrorString(SS_Error);
            break;
        default:
            ErrorString = FX_ErrorString(FX_InvalidCard);
            break;
        }
        break;

    case FX_InvalidCard:
        ErrorString = "Invalid Sound Fx device.";
        break;

    case FX_MultiVocError:
        ErrorString = MV_ErrorString(MV_Error);
        break;

    case FX_VOCFileError:
        ErrorString = "Invalid VOC file.";
        break;

    default:
        ErrorString = "Unknown Fx error code.";
        break;
    }

    return (ErrorString);
}

/*---------------------------------------------------------------------
   Function: FX_SetupCard

   Sets the configuration of a sound device.
---------------------------------------------------------------------*/

int FX_SetupCard(
    int SoundCard,
    fx_device *device)

{
    int status;
    int DeviceStatus;
    BLASTER_CONFIG Config;

    FX_SoundDevice = SoundCard;

    status = FX_Ok;

    switch (SoundCard)
    {
    case SoundBlaster:
        DeviceStatus = BLASTER_GetEnv(&Config);
        if (DeviceStatus != BLASTER_Ok)
        {
            FX_SetErrorCode(FX_SoundCardError);
            status = FX_Error;
            break;
        }
        BLASTER_SetCardSettings(&Config);
        DeviceStatus = BLASTER_Init();
        if (DeviceStatus != BLASTER_Ok)
        {
            FX_SetErrorCode(FX_SoundCardError);
            status = FX_Error;
            break;
        }

        device->MaxVoices = 8;
        BLASTER_GetCardInfo(&device->MaxSampleBits, &device->MaxChannels);
        break;

    case ProAudioSpectrum:
        DeviceStatus = PAS_Init();
        if (DeviceStatus != PAS_Ok)
        {
            FX_SetErrorCode(FX_SoundCardError);
            status = FX_Error;
            break;
        }

        device->MaxVoices = 8;
        PAS_GetCardInfo(&device->MaxSampleBits, &device->MaxChannels);
        break;

    case TandySoundSource:
        DeviceStatus = SS_SetPort(SS_DefaultPort);
        if (DeviceStatus != SS_Ok)
        {
            FX_SetErrorCode(FX_SoundCardError);
            status = FX_Error;
            break;
        }
        device->MaxVoices = 4;
        device->MaxSampleBits = 8;
        device->MaxChannels = 1;
        break;

    default:
        FX_SetErrorCode(FX_InvalidCard);
        status = FX_Error;
    }

    return (status);
}

/*---------------------------------------------------------------------
   Function: FX_Init

   Selects which sound device to use.
---------------------------------------------------------------------*/
int FX_Init(
    int SoundCard,
    int numvoices,
    int samplebits)

{
    int status;
    int devicestatus;
    int numchannels;
    fx_device device;

    status = FX_SetupCard(SoundCard, &device);
    if (status != FX_Ok)
    {
        return (status);
    }

    FX_SoundDevice = SoundCard;
    switch (SoundCard)
    {
    case SoundBlaster:
    case ProAudioSpectrum:
    case TandySoundSource:
        numchannels = 0;
        FX_SampleBits = min(samplebits, device.MaxSampleBits);
        if (samplebits == 16)
        {
            numchannels |= 2;
        }
        FX_NumVoices = numvoices;
        devicestatus = MV_Init(SoundCard, 10000, numvoices, numchannels);
        if (devicestatus != MV_Ok)
        {
            FX_SetErrorCode(FX_MultiVocError);
            status = FX_Error;
            break;
        }
        break;

    default:
        FX_SetErrorCode(FX_InvalidCard);
        status = FX_Error;
        break;
    }

    return (status);
}

/*---------------------------------------------------------------------
   Function: FX_Shutdown

   Terminates use of sound device.
---------------------------------------------------------------------*/

int FX_Shutdown(
    void)

{
    int status;

    status = FX_Ok;
    switch (FX_SoundDevice)
    {
    case SoundBlaster:
    case ProAudioSpectrum:
    case TandySoundSource:
        status = MV_Shutdown();
        if (status != MV_Ok)
        {
            FX_SetErrorCode(FX_MultiVocError);
            status = FX_Error;
            break;
        }
        break;

    default:
        FX_SetErrorCode(FX_InvalidCard);
        status = FX_Error;
        break;
    }

    return (status);
}

/*---------------------------------------------------------------------
   Function: FX_SetVolume

   Sets the volume of the current sound device.
---------------------------------------------------------------------*/

void FX_SetVolume(
    int volume)

{

    switch (FX_SoundDevice)
    {
    case SoundBlaster:
        if (BLASTER_CardHasMixer())
        {
            BLASTER_SetVoiceVolume(volume);
            break;
        }

        break;

    case ProAudioSpectrum:
        PAS_SetPCMVolume(volume);
        break;
    }
}

/*---------------------------------------------------------------------
   Function: FX_GetVolume

   Returns the volume of the current sound device.
---------------------------------------------------------------------*/

int FX_GetVolume(
    void)

{
    int volume = 0;

    switch (FX_SoundDevice)
    {
    case SoundBlaster:
        if (BLASTER_CardHasMixer())
        {
            volume = BLASTER_GetVoiceVolume();
            break;
        }
        break;

    case ProAudioSpectrum:
        volume = PAS_GetPCMVolume();
        break;
    }
    return (volume);
}

unsigned long sub_2562C(
    char *data,
    unsigned long arg1,
    unsigned int arg2,
    unsigned int arg3)
{
    unsigned long i;
    unsigned long j;
    char *ptr;

    i = ((long)arg2 << 16) / ((long)arg3);
    if (i <= 0x10000)
    {
        return arg1;
    }

    j = 0;
    ptr = data;
    arg1 = arg1 << 16;

    for (; j < arg1; j += i)
    {
        *ptr++ = data[j >> 16];
    }
    return ptr - data;
}

int sub_256BD(char *data)
{
    char huge *ptr;
    unsigned long j;
    unsigned long samplerate;
    unsigned int timeconstant;
    unsigned int i;

    if (*data == 'C')
    {
        ptr = data + *(int *)(data + 20);
        while (*ptr > 1)
        {
            ptr++;
            j = *((unsigned long *)ptr) & 0xFFFFFF;
            ptr += (j + 3);
        }

        if (*ptr == 0)
        {
            FX_SetErrorCode(FX_VOCFileError);
            return FX_Error;
        }

        ptr++;
        j = (*((unsigned long *)ptr) & 0xFFFFFF) - 2;
        timeconstant = ptr[3];
        ptr += 5;
        samplerate = CalcSamplingRate(timeconstant);

        if (FX_SoundDevice == TandySoundSource)
        {
            j = sub_2562C((char *)ptr, j, 10000, 7000);
        }

        *data = 0;
        *((unsigned long *)&data[1]) = (unsigned long)ptr;
        *((unsigned long *)&data[5]) = (unsigned long)j;
        *((unsigned long *)&data[9]) = (unsigned long)samplerate;

        if (FX_SampleBits == 16)
        {
            for (i = 0; i < j; i++)
            {
                ptr[i] += 0x80;
            }
        }
        else
        {
            for (i = 0; i < j; i++)
            {
                ptr[i] += 0x80;
                ptr[i] /= FX_NumVoices;
            }
        }
    }
    FX_SetErrorCode(FX_Ok);
    return 0;
}

/*---------------------------------------------------------------------
   Function: FX_PlayVOC

   Begin playback of sound data with the given volume and priority.
---------------------------------------------------------------------*/

int FX_PlayVOC(
    char *ptr,
    int arg1,
    int arg2,
    int arg3)

{
    int handle;
    int ret;

    (void)arg1;
    (void)arg2;
    if (*ptr == 'C')
    {
        ret = sub_256BD(ptr);
        if (ret)
            return ret;
    }

    switch (FX_SoundDevice)
    {
    case SoundBlaster:
    case ProAudioSpectrum:
    case TandySoundSource:
        handle = MV_PlayVOC(*((int *)&ptr[1]), *((int *)&ptr[3]), *((int *)&ptr[5]), arg3);
        if (handle != MV_Error)
            break;
        FX_SetErrorCode(FX_MultiVocError);
        handle = FX_Error;
        break;
    default:
        FX_SetErrorCode(FX_InvalidCard);
        handle = FX_Error;
        break;
    }

    return (handle);
}

/*---------------------------------------------------------------------
   Function: FX_SoundActive

   Tests if the specified sound is currently playing.
---------------------------------------------------------------------*/

int FX_SoundActive(
    int handle)

{
    return (MV_VoicePlaying(handle));
}

/*---------------------------------------------------------------------
   Function: FX_StopSound

   Halts playback of a specific voice
---------------------------------------------------------------------*/

int FX_StopSound(
    int handle)

{
    int status;

    status = MV_Kill(handle);
    if (status != MV_Ok)
    {
        FX_SetErrorCode(FX_MultiVocError);
        return (FX_Warning);
    }

    return (FX_Ok);
}

/*---------------------------------------------------------------------
   Function: FX_StopAllSounds

   Halts playback of all sounds.
---------------------------------------------------------------------*/

int FX_StopAllSounds(
    void)

{
    int status;

    status = MV_StopPlayback();
    if (status != MV_Ok)
    {
        FX_SetErrorCode(FX_MultiVocError);
        return (FX_Warning);
    }

    return (FX_Ok);
}

/*---------------------------------------------------------------------
   Function: FX_SoundsPlaying

   Reports the number of voices playing.
---------------------------------------------------------------------*/

int FX_SoundsPlaying(
    void)

{
    return (MV_VoicesPlaying());
}
