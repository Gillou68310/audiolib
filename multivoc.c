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
   module: MULTIVOC.C

   author: James R. Dose
   date:   December 20, 1993

   Routines to provide multichannel digitized sound playback for
   Sound Blaster compatible sound cards.

   (c) Copyright 1993 James R. Dose.  All Rights Reserved.
**********************************************************************/
#include <stdlib.h>
#include <alloc.h>
#include "interrup.h"
#include "ll_man.h"
#include "sndcards.h"
#include "blaster.h"
#include "sndsrc.h"
#include "pas16.h"
#include "_al_midi.h"
#include "multivoc.h"
#include "_multivc.h"

typedef struct
{
    VoiceNode *start;
    VoiceNode *end;
} VList;

static int MV_Installed = FALSE;
static int MV_SoundCard = SoundBlaster;
static int MV_MaxVoices = 1;
static int word_2FDC2 = 0;
static int MV_BufferSize = MixBufferSize;
static int MV_MixMode = MONO_8BIT;
static int MV_Silence = SILENCE_8BIT;
static char *MV_Buffer = NULL;
static int MV_MixPage = 0;
static int MV_PlayPage = 0;
static int MV_VoiceHandle = MV_MinVoiceHandle;
static int word_2FDD4 = 0;
static int word_2FDD6 = 0;
static int MV_ErrorCode = MV_Ok;

static int MV_MixRate;
static char *MV_MixBuffer[NumberOfBuffers];
static volatile VList VoicePool;
static volatile VList VoiceList;
static int MV_RequestedMixRate;
static VoiceNode MV_Voices[NUM_VOICES];

#define MV_SetErrorCode(status) \
    MV_ErrorCode = (status);

#define ClearBuffer_DW(ptr, data, length) \
    asm push es;                          \
    _CX = length;                         \
    _AX = data;                           \
    _DI = (unsigned long)ptr;             \
    _ES = (unsigned long)ptr >> 16;       \
    asm cld;                              \
    asm rep stosw;                        \
    asm pop es;

/*---------------------------------------------------------------------
   Function: MV_ErrorString

   Returns a pointer to the error message associated with an error
   number.  A -1 returns a pointer the current error.
---------------------------------------------------------------------*/

char *MV_ErrorString(
    int ErrorNumber)

{
    char *ErrorString;

    switch (ErrorNumber)
    {
    case MV_Warning:
    case MV_Error:
        ErrorString = MV_ErrorString(MV_ErrorCode);
        break;

    case MV_Ok:
        ErrorString = "Multivoc ok.";
        break;

    case MV_UnsupportedCard:
        ErrorString = "Selected sound card is not supported by Multivoc.";
        break;

    case MV_NotInstalled:
        ErrorString = "Multivoc not installed.";
        break;

    case MV_NoVoices:
        ErrorString = "No free voices available to Multivoc.";
        break;

    case MV_NoMem:
        ErrorString = "Out of memory in Multivoc.";
        break;

    case MV_VoiceNotFound:
        ErrorString = "No voice with matching handle found.";
        break;

    case MV_BlasterError:
        ErrorString = BLASTER_ErrorString(BLASTER_Error);
        break;

    case MV_PasError:
        ErrorString = PAS_ErrorString(PAS_Error);
        break;

#ifndef SOUNDSOURCE_OFF
    case MV_SoundSourceError:
        ErrorString = SS_ErrorString(SS_Error);
        break;
#endif

    default:
        ErrorString = "Unknown Multivoc error code.";
        break;
    }

    return (ErrorString);
}

/*---------------------------------------------------------------------
   Function: MV_Mix8bitMono

   Mixes the sound into the buffer as an 8 bit mono sample.
---------------------------------------------------------------------*/
static void MV_Mix8bitMono(
    VoiceNode *voice,
    int buffer,
    int arg2)

{
    char *to;
    char *from;
    int len;

    to = MV_MixBuffer[buffer];
    len = (voice->length > MV_BufferSize) ? MV_BufferSize : voice->length;
    from = voice->unk8 + voice->unk10[buffer];
    if (arg2 == 1)
    {
        sub_2A110(to, from, len);
    }
    else
    {
        sub_2A1B1(to, from, len);
    }
    voice->length -= len;
    voice->unkE += len;
    voice->unk18[buffer] = len;
}

/*---------------------------------------------------------------------
   Function: MV_Mix16bitMono

   Mixes the sound into the buffer as an 8 bit mono sample.
---------------------------------------------------------------------*/

static void MV_Mix16bitMono(
    VoiceNode *voice,
    int buffer,
    int arg2)

{
    char *to;
    char *from;
    int len;

    to = MV_MixBuffer[buffer];
    len = (voice->length > MV_BufferSize) ? MV_BufferSize : voice->length;
    from = voice->unk8 + voice->unk10[buffer];
    if (arg2 == 1)
    {
        sub_2A252(to, from, len, word_2FDC2);
    }
    else
    {
        sub_2A333(to, from, len, word_2FDC2);
    }
    voice->unkE += len;
    voice->length -= len;
    voice->unk18[buffer] = len;
}

static void sub_29658(
    VoiceNode *voice,
    int buffer)

{
    if (voice->length == 0)
    {
        voice->Active[buffer] = FALSE;
        return;
    }
    voice->Active[buffer] = TRUE;
    voice->unk10[buffer] = voice->unkE;
    switch (MV_MixMode)
    {
    case MONO_8BIT:
        MV_Mix8bitMono(voice, buffer, 1);
        break;

    case MONO_16BIT:
        MV_Mix16bitMono(voice, buffer, 1);
        break;
    }
}

static void sub_296C7(
    VoiceNode *voice,
    int buffer)

{
    voice->unkE = voice->unk10[buffer];
    voice->length = voice->unk18[buffer];
    switch (MV_MixMode)
    {
    case MONO_8BIT:
        MV_Mix8bitMono(voice, buffer, 0);
        break;

    case MONO_16BIT:
        MV_Mix16bitMono(voice, buffer, 0);
        break;
    }
}

/*---------------------------------------------------------------------
   Function: MV_PrepareBuffer

   Initializes the current buffer and mixes the currently active
   voices.
---------------------------------------------------------------------*/

void MV_PrepareBuffer(
    int page)

{
    VoiceNode *voice;

    // Initialize buffer
    ClearBuffer_DW(MV_MixBuffer[page], MV_Silence, MixBufferSize / 2);
    voice = VoiceList.start;
    while (voice != NULL)
    {
        sub_29658(voice, page);
        voice = voice->next;
    }
}

/*---------------------------------------------------------------------
   Function: MV_DeleteDeadVoices

   Removes any voices that have finished playing.
---------------------------------------------------------------------*/

void MV_DeleteDeadVoices(
    int page)

{
    VoiceNode *voice;
    VoiceNode *next;

    DISABLE_INTERRUPTS();

    voice = VoiceList.start;
    while (voice != NULL)
    {
        next = voice->next;

        // Is this voice done?
        if (!voice->Active[page])
        {
            // Yes, move it from the play list into the free list
            LL_Remove(VoiceNode, &VoiceList, voice);
            LL_AddToTail(VoiceNode, &VoicePool, voice);
            word_2FDD6--;
        }

        voice = next;
    }

    ENABLE_INTERRUPTS();
}

/*---------------------------------------------------------------------
   Function: MV_ServiceVoc

   Starts playback of the waiting buffer and mixes the next one.
---------------------------------------------------------------------*/

void MV_ServiceVoc(
    void)

{
    // Set which buffer is currently being played.
    MV_PlayPage = MV_MixPage;

    // Toggle which buffer we'll mix next
    MV_MixPage++;
    if (MV_MixPage >= NumberOfBuffers)
    {
        MV_MixPage = 0;
    }

    // Play any waiting voices
    MV_PrepareBuffer(MV_MixPage);
    // Delete any voices that are done playing
    MV_DeleteDeadVoices(MV_MixPage);
}

/*---------------------------------------------------------------------
   Function: MV_GetVoice

   Locates the voice with the specified handle.
---------------------------------------------------------------------*/

VoiceNode *MV_GetVoice(
    int handle)

{
    VoiceNode *voice;

    DISABLE_INTERRUPTS();

    voice = VoiceList.start;

    while (voice != NULL)
    {
        if (handle == voice->handle)
        {
            break;
        }

        voice = voice->next;
    }

    ENABLE_INTERRUPTS();

    if (voice == NULL)
    {
        MV_SetErrorCode(MV_VoiceNotFound);
    }

    return (voice);
}

/*---------------------------------------------------------------------
   Function: MV_VoicePlaying

   Checks if the voice associated with the specified handle is
   playing.
---------------------------------------------------------------------*/

int MV_VoicePlaying(
    int handle)

{
    VoiceNode *voice;

    if (!MV_Installed)
    {
        MV_SetErrorCode(MV_NotInstalled);
        return (FALSE);
    }

    voice = MV_GetVoice(handle);

    if (voice == NULL)
    {
        return (FALSE);
    }

    return (TRUE);
}

/*---------------------------------------------------------------------
   Function: MV_StopPlayback

   Stops the sound playback engine.
---------------------------------------------------------------------*/

int MV_StopPlayback(
    void)

{
    VoiceNode *voice;

    if (!MV_Installed)
    {
        MV_SetErrorCode(MV_NotInstalled);
        return (MV_Error);
    }
    // Stop sound playback
    switch (MV_SoundCard)
    {
    case SoundBlaster:
        BLASTER_StopPlayback();
        break;

    case ProAudioSpectrum:
        PAS_StopPlayback();
        break;

#ifndef SOUNDSOURCE_OFF
    case TandySoundSource:
        SS_StopPlayback();
        break;
#endif
    }
    word_2FDD4 = 0;
    while (VoiceList.start != NULL)
    {
        voice = VoiceList.start;
        LL_Remove(VoiceNode, &VoiceList, voice);
        LL_AddToTail(VoiceNode, &VoicePool, voice);
        word_2FDD6--;
    }
    return (MV_Ok);
}

/*---------------------------------------------------------------------
   Function: MV_Kill

   Stops output of the voice associated with the specified handle.
---------------------------------------------------------------------*/

int MV_Kill(
    int handle)

{
    VoiceNode *voice;
    int page;

    if (!MV_Installed)
    {
        MV_SetErrorCode(MV_NotInstalled);
        return (MV_Error);
    }

    voice = MV_GetVoice(handle);
    if (voice == NULL)
    {
        MV_SetErrorCode(MV_VoiceNotFound);
        return (MV_Error);
    }

    DISABLE_INTERRUPTS();
    // move the voice from the play list to the free list
    LL_Remove(VoiceNode, &VoiceList, voice);
    word_2FDD6--;
    page = MV_PlayPage;
    sub_296C7(voice, page);
    page++;
    if (page >= NumberOfBuffers)
        page = 0;
    sub_296C7(voice, page);

    LL_AddToTail(VoiceNode, &VoicePool, voice);
    ENABLE_INTERRUPTS();

    MV_SetErrorCode(MV_Ok);
    return (MV_Ok);
}

/*---------------------------------------------------------------------
   Function: MV_VoicesPlaying

   Determines the number of currently active voices.
---------------------------------------------------------------------*/

int MV_VoicesPlaying(
    void)

{
    VoiceNode *voice;
    int NumVoices = 0;
    int page;

    if (!MV_Installed)
    {
        MV_SetErrorCode(MV_NotInstalled);
        return (0);
    }

    DISABLE_INTERRUPTS();
    page = MV_PlayPage;

    voice = VoiceList.start;
    while (voice != NULL)
    {
        if (voice->Active[page])
            NumVoices++;
        voice = voice->next;
    }

    ENABLE_INTERRUPTS();

    return (NumVoices);
}

/*---------------------------------------------------------------------
   Function: MV_AllocVoice

   Retrieve an inactive or lower priority voice for output.
---------------------------------------------------------------------*/

VoiceNode *MV_AllocVoice(
    int priority)

{
    VoiceNode *voice;

    if (word_2FDD6 >= MV_MaxVoices)
    {
        DISABLE_INTERRUPTS();

        // check if we have a higher priority than a voice that is playing.
        voice = VoiceList.start;
        while (voice != NULL)
        {
            if (priority >= voice->priority)
            {
                MV_Kill(voice->handle);
                break;
            }
            voice = voice->next;
        }
        ENABLE_INTERRUPTS();
        // Check if any voices are in the voice pool
        if (voice == NULL)
        {
            // No free voices
            return (NULL);
        }
    }

    DISABLE_INTERRUPTS();
    if (VoicePool.start != NULL)
    {
        voice = VoicePool.start;
        LL_Remove(VoiceNode, &VoicePool, voice);
    }
    ENABLE_INTERRUPTS();

    if (voice != NULL)
    {
        // Find a free voice handle
        do
        {
            MV_VoiceHandle++;
            if (MV_VoiceHandle < MV_MinVoiceHandle)
            {
                MV_VoiceHandle = MV_MinVoiceHandle;
            }
        } while (MV_VoicePlaying(MV_VoiceHandle));

        voice->handle = MV_VoiceHandle;
    }

    return (voice);
}

/*---------------------------------------------------------------------
   Function: MV_SetMixMode

   Prepares Multivoc to play stereo of mono digitized sounds.
---------------------------------------------------------------------*/
int MV_SetMixMode(
    int mode)

{
    if (!MV_Installed)
    {
        MV_SetErrorCode(MV_NotInstalled);
        return (MV_Error);
    }

    MV_StopPlayback();

    switch (MV_SoundCard)
    {
    case SoundBlaster:
        MV_MixMode = BLASTER_SetMixMode(mode);
        break;

    case ProAudioSpectrum:
        MV_MixMode = PAS_SetMixMode(mode);
        break;

#ifndef SOUNDSOURCE_OFF
    // case SoundSource :
    case TandySoundSource:
        MV_MixMode = SS_SetMixMode(mode);
        break;
#endif
    }

    switch (MV_MixMode)
    {
    case MONO_8BIT:
        MV_BufferSize = MixBufferSize;
        MV_Silence = SILENCE_8BIT;
        break;

    case MONO_16BIT:
        MV_BufferSize = MixBufferSize / 2;
        MV_Silence = SILENCE_16BIT;
        break;
    }

    return (MV_Ok);
}

/*---------------------------------------------------------------------
   Function: MV_StartPlayback

   Starts the sound playback engine.
---------------------------------------------------------------------*/

void MV_StartPlayback(
    void)

{
    // Set the mix buffer variables
    MV_MixPage = 0;
    MV_PrepareBuffer(MV_MixPage);
    MV_PlayPage = MV_MixPage;

    // Start playback
    switch (MV_SoundCard)
    {
    case SoundBlaster:
        BLASTER_BeginBufferedPlayback(MV_MixBuffer[0],
                                      TotalBufferSize, NumberOfBuffers,
                                      MV_RequestedMixRate, MV_MixMode, MV_ServiceVoc);

        MV_MixRate = BLASTER_GetPlaybackRate();
        break;

    case ProAudioSpectrum:
        PAS_BeginBufferedPlayback(MV_MixBuffer[0],
                                  TotalBufferSize, NumberOfBuffers,
                                  MV_RequestedMixRate, MV_MixMode, MV_ServiceVoc);

        MV_MixRate = PAS_GetPlaybackRate();
        break;

#ifndef SOUNDSOURCE_OFF
    // case SoundSource :
    case TandySoundSource:
        SS_BeginBufferedPlayback(MV_MixBuffer[0],
                                 TotalBufferSize, NumberOfBuffers,
                                 MV_ServiceVoc);
        MV_MixRate = SS_SampleRate;
        break;
#endif
    }
    MV_MixPage++;
    if (MV_MixPage >= NumberOfBuffers)
    {
        MV_MixPage = 0;
    }
    MV_PrepareBuffer(MV_MixPage);
    word_2FDD4 = 1;
}

void sub_29C4E(
    VoiceNode *voice)
{
    int page;

    DISABLE_INTERRUPTS();
    page = MV_PlayPage + 1;
    if (page >= NumberOfBuffers)
    {
        page = 0;
    }
    sub_29658(voice, page);
    LL_AddToTail(VoiceNode, &VoiceList, voice);
    word_2FDD6++;
    ENABLE_INTERRUPTS();
}

/*---------------------------------------------------------------------
   Function: MV_Play

   Begin playback of sound data with the given sound levels and
   priority.
---------------------------------------------------------------------*/

int MV_PlayVOC(
    char *ptr,
    int length,
    int priority)

{
    VoiceNode *voice;
    int buffer;

    if (!MV_Installed)
    {
        MV_SetErrorCode(MV_NotInstalled);
        return (MV_Error);
    }

    // Request a voice from the voice pool
    voice = MV_AllocVoice(priority);
    if (voice == NULL)
    {
        MV_SetErrorCode(MV_NoVoices);
        return (MV_Error);
    }

    if (word_2FDD4 == 0)
    {
        MV_StartPlayback();
    }
    voice->unk8 = ptr;
    voice->length = length;
    voice->next = NULL;
    voice->prev = NULL;
    voice->unkE = 0;

    for (buffer = 0; buffer < NumberOfBuffers; buffer++)
    {
        voice->unk10[buffer] = 0;
        voice->unk18[buffer] = 0;
        voice->Active[buffer] = 0;
    }

    voice->priority = priority;
    sub_29C4E(voice);

    MV_SetErrorCode(MV_Ok);
    return (voice->handle);
}

/*---------------------------------------------------------------------
   Function: MV_Init

   Perform the initialization of variables and memory used by
   Multivoc.
---------------------------------------------------------------------*/

int MV_Init(
    int soundcard,
    int MixRate,
    int Voices,
    int MixMode)

{
    char huge *ptr;
    int status;
    int buffer;
    int index;

    if (MV_Installed)
    {
        MV_Shutdown();
    }

    MV_SetErrorCode(MV_Ok);

    if (soundcard == TandySoundSource)
        ptr = farmalloc(TotalBufferSize);
    else
        ptr = farmalloc(TotalBufferSize * 2);

    if (ptr == NULL)
    {
        MV_SetErrorCode(MV_NoMem);
        return MV_Error;
    }

    // Initialize the sound card
    switch (soundcard)
    {
    case SoundBlaster:
        status = BLASTER_Init();
        if (status != BLASTER_Ok)
        {
            MV_SetErrorCode(MV_BlasterError);
        }
        break;

    case ProAudioSpectrum:
        status = PAS_Init();
        if (status != PAS_Ok)
        {
            MV_SetErrorCode(MV_PasError);
        }
        break;

#ifndef SOUNDSOURCE_OFF
    // case SoundSource :
    case TandySoundSource:
        status = SS_Init();
        if (status != SS_Ok)
        {
            MV_SetErrorCode(MV_SoundSourceError);
        }
        break;
#endif

    default:
        MV_SetErrorCode(MV_UnsupportedCard);
        break;
    }

    if (MV_ErrorCode != MV_Ok)
    {
        farfree(MV_Buffer);
        MV_Buffer = NULL;
        return (MV_Error);
    }

    MV_Buffer = (char *)ptr;
    MV_SoundCard = soundcard;

    if (soundcard != TandySoundSource)
    {
        // Make sure we don't cross a physical page
        if ((long)((long)((unsigned int)ptr) + TotalBufferSize) > 0x10000)
        {
            ptr += TotalBufferSize;
        }
    }

    for (buffer = 0; buffer < NumberOfBuffers; buffer++)
    {
        MV_MixBuffer[buffer] = (char *)ptr;
        ptr += MixBufferSize;
    }

    MV_Installed = TRUE;

    // Set the sampling rate
    MV_RequestedMixRate = MixRate;

    // Set Mixer to play stereo digitized sound
    MV_SetMixMode(MixMode);

    MV_MaxVoices = Voices;
    switch (Voices)
    {
    case 1:
        word_2FDC2 = 8;
        break;
    case 2:
        word_2FDC2 = 7;
        break;
    case 3:
    case 4:
        word_2FDC2 = 6;
        break;
    case 5:
    case 6:
    case 7:
    case 8:
        word_2FDC2 = 5;
        break;
    default:
        word_2FDC2 = 0;
        break;
    }
    VoiceList.start = NULL;
    VoiceList.end = NULL;
    VoicePool.start = NULL;
    VoicePool.end = NULL;

    for (index = 0; index < NUM_VOICES; index++)
    {
        LL_AddToTail(VoiceNode, &VoicePool, &MV_Voices[index]);
    }

    return (MV_Ok);
}

/*---------------------------------------------------------------------
   Function: MV_Shutdown

   Restore any resources allocated by Multivoc back to the system.
---------------------------------------------------------------------*/

int MV_Shutdown(
    void)

{
    int buffer;

    if (!MV_Installed)
    {
        MV_SetErrorCode(MV_NotInstalled);
        return (MV_Error);
    }

    // Shutdown the sound card
    switch (MV_SoundCard)
    {
    case SoundBlaster:
        BLASTER_Shutdown();
        break;

    case ProAudioSpectrum:
        PAS_Shutdown();
        break;

#ifndef SOUNDSOURCE_OFF
    // case SoundSource :
    case TandySoundSource:
        SS_Shutdown();
        break;
#endif
    }

    VoiceList.start = NULL;
    VoiceList.end = NULL;
    VoicePool.start = NULL;
    VoicePool.end = NULL;
    word_2FDD4 = 0;
    MV_MaxVoices = 0;

    // Release our mix buffer
    farfree(MV_Buffer);
    MV_Buffer = NULL;

    for (buffer = 0; buffer < NumberOfBuffers; buffer++)
    {
        MV_MixBuffer[buffer] = NULL;
    }
    MV_Installed = FALSE;
    return (MV_Ok);
}
