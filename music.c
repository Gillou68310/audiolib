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
   module: MUSIC.C

   author: James R. Dose
   date:   March 25, 1994

   Device independant music playback routines.

   (c) Copyright 1994 James R. Dose.  All Rights Reserved.
**********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include "sndcards.h"
#include "music.h"
#include "midi.h"
#include "al_midi.h"
#include "pas16.h"
#include "blaster.h"
#include "mpu401.h"

#define TRUE (1 == 1)
#define FALSE (!TRUE)

int MUSIC_SoundDevice = Adlib;
int MUSIC_ErrorCode = MUSIC_Ok;

static midifuncs MUSIC_MidiFunctions;

int MUSIC_InitFM(int card, midifuncs *Funcs);
int MUSIC_InitMidi(int card, midifuncs *Funcs, int Address);

#define MUSIC_SetErrorCode(status) \
    MUSIC_ErrorCode = (status);

/*---------------------------------------------------------------------
   Function: MUSIC_ErrorString

   Returns a pointer to the error message associated with an error
   number.  A -1 returns a pointer the current error.
---------------------------------------------------------------------*/

char *MUSIC_ErrorString(
    int ErrorNumber)

{
    char *ErrorString;

    switch (ErrorNumber)
    {
    case MUSIC_Warning:
    case MUSIC_Error:
        ErrorString = MUSIC_ErrorString(MUSIC_ErrorCode);
        break;

    case MUSIC_Ok:
        ErrorString = "Music ok.";
        break;

    case MUSIC_SoundCardError:
        switch (MUSIC_SoundDevice)
        {
        case SoundBlaster:
        case ProAudioSpectrum:
        case Adlib:
        case GenMidi:
        case WaveBlaster:
            ErrorString = "Music sound card error.";
            break;

        default:
            ErrorString = "Invalid Music device error.";
            break;
        }
        break;

    case MUSIC_InvalidCard:
        ErrorString = "Invalid Music device.";
        break;

    case MUSIC_MidiError:
        ErrorString = "Error playing MIDI file.";
        break;

    case MUSIC_TaskManError:
        ErrorString = "TaskMan error.";
        break;

    case MUSIC_FMNotDetected:
        ErrorString = "Could not detect FM chip.";
        break;

    default:
        ErrorString = "Unknown Music error code.";
        break;
    }

    return (ErrorString);
}

/*---------------------------------------------------------------------
   Function: MUSIC_Init

   Selects which sound device to use.
---------------------------------------------------------------------*/

int MUSIC_Init(
    int SoundCard,
    int Address)

{
    int i;
    int status;

    status = MUSIC_Ok;
    MUSIC_SoundDevice = SoundCard;

    switch (SoundCard)
    {
    case SoundBlaster:
    case Adlib:
    case ProAudioSpectrum:
        status = MUSIC_InitFM(SoundCard, &MUSIC_MidiFunctions);
        break;

    case GenMidi:
    case WaveBlaster:
        status = MUSIC_InitMidi(SoundCard, &MUSIC_MidiFunctions, Address);
        break;

    default:
        MUSIC_SetErrorCode(MUSIC_InvalidCard);
        status = MUSIC_Error;
    }

    return (status);
}

/*---------------------------------------------------------------------
   Function: MUSIC_Shutdown

   Terminates use of sound device.
---------------------------------------------------------------------*/

int MUSIC_Shutdown(
    void)

{
    int status;

    status = MUSIC_Ok;

    MIDI_StopSong();

    switch (MUSIC_SoundDevice)
    {
    case Adlib:
        AL_Shutdown();
        break;

    case SoundBlaster:
        AL_Shutdown();

    case WaveBlaster:
        BLASTER_RestoreMidiVolume();
        break;

    case ProAudioSpectrum:
        AL_Shutdown();
        PAS_RestoreMusicVolume();
        break;
    }

    return (status);
}

/*---------------------------------------------------------------------
   Function: MUSIC_SetVolume

   Sets the volume of music playback.
---------------------------------------------------------------------*/

void MUSIC_SetVolume(
    int volume)

{
    volume = max(0, volume);
    volume = min(volume, 255);

    MIDI_SetVolume(volume);
}

/*---------------------------------------------------------------------
   Function: MUSIC_GetVolume

   Returns the volume of music playback.
---------------------------------------------------------------------*/

int MUSIC_GetVolume(
    void)

{
    return (MIDI_GetVolume());
}

/*---------------------------------------------------------------------
   Function: MUSIC_SetLoopFlag

   Set whether the music will loop or end when it reaches the end of
   the song.
---------------------------------------------------------------------*/

void MUSIC_SetLoopFlag(
    int loopflag)

{
    MIDI_SetLoopFlag(loopflag);
}

/*---------------------------------------------------------------------
   Function: MUSIC_SongPlaying

   Returns whether there is a song playing.
---------------------------------------------------------------------*/

int MUSIC_SongPlaying(
    void)

{
    return (MIDI_SongPlaying());
}

/*---------------------------------------------------------------------
   Function: MUSIC_Continue

   Continues playback of a paused song.
---------------------------------------------------------------------*/

void MUSIC_Continue(
    void)

{
    MIDI_ContinueSong();
}

/*---------------------------------------------------------------------
   Function: MUSIC_Pause

   Pauses playback of a song.
---------------------------------------------------------------------*/

void MUSIC_Pause(
    void)

{
    MIDI_PauseSong();
}

/*---------------------------------------------------------------------
   Function: MUSIC_StopSong

   Stops playback of current song.
---------------------------------------------------------------------*/

int MUSIC_StopSong(
    void)

{
    MIDI_StopSong();
    MUSIC_SetErrorCode(MUSIC_Ok);
    return (MUSIC_Ok);
}

/*---------------------------------------------------------------------
   Function: MUSIC_PlaySong

   Begins playback of MIDI song.
---------------------------------------------------------------------*/

int MUSIC_PlaySong(
    unsigned char *song,
    int loopflag)

{
    int status;

    switch (MUSIC_SoundDevice)
    {
    case SoundBlaster:
    case Adlib:
    case ProAudioSpectrum:
    case GenMidi:
    case WaveBlaster:

        MIDI_StopSong();
        status = MIDI_PlaySong(song, loopflag);
        if (status != MIDI_Ok)
        {
            MUSIC_SetErrorCode(MUSIC_MidiError);
            return (MUSIC_Warning);
        }
        break;

    default:
        MUSIC_SetErrorCode(MUSIC_InvalidCard);
        return (MUSIC_Warning);
    }

    return (MUSIC_Ok);
}

int MUSIC_InitFM(
    int card,
    midifuncs *Funcs)

{
    int status;

    status = MIDI_Ok;

    if (!AL_DetectFM())
    {
        MUSIC_SetErrorCode(MUSIC_FMNotDetected);
        return (MUSIC_Error);
    }

    // Init the fm routines
    AL_Init(card);

    Funcs->NoteOff = AL_NoteOff;
    Funcs->NoteOn = AL_NoteOn;
    Funcs->PolyAftertouch = NULL;
    Funcs->ControlChange = AL_ControlChange;
    Funcs->ProgramChange = AL_ProgramChange;
    Funcs->ChannelAftertouch = NULL;
    Funcs->PitchBend = AL_SetPitchBend;
    Funcs->ReleasePatches = NULL;
    Funcs->LoadPatch = NULL;

    switch (card)
    {
    case SoundBlaster:
        if (BLASTER_CardHasMixer())
        {
            BLASTER_SaveMidiVolume();
            Funcs->SetVolume = (void (*)(int volume))BLASTER_SetMidiVolume;
            Funcs->GetVolume = BLASTER_GetMidiVolume;
        }
        else
        {
            Funcs->SetVolume = NULL;
            Funcs->GetVolume = NULL;
        }
        break;

    case Adlib:
        Funcs->SetVolume = NULL;
        Funcs->GetVolume = NULL;
        break;

    case ProAudioSpectrum:
        if (PAS_SaveMusicVolume() == PAS_Ok)
        {
            Funcs->SetVolume = PAS_SetFMVolume;
            Funcs->GetVolume = PAS_GetFMVolume;
        }
        break;
    }

    if (MIDI_SetMidiFuncs(Funcs))
    {
        MUSIC_SetErrorCode(MUSIC_SoundCardError);
        status = MUSIC_Error;
    }

    return (status);
}

int MUSIC_InitMidi(
    int card,
    midifuncs *Funcs,
    int Address)

{
    int status;

    status = MUSIC_Ok;

    if (MPU_Init(Address) != MPU_Ok)
    {
        MUSIC_SetErrorCode(MUSIC_SoundCardError);
        return (MUSIC_Error);
    }

    Funcs->NoteOff = MPU_NoteOff;
    Funcs->NoteOn = MPU_NoteOn;
    Funcs->PolyAftertouch = MPU_PolyAftertouch;
    Funcs->ControlChange = MPU_ControlChange;
    Funcs->ProgramChange = MPU_ProgramChange;
    Funcs->ChannelAftertouch = MPU_ChannelAftertouch;
    Funcs->PitchBend = MPU_PitchBend;
    Funcs->ReleasePatches = NULL;
    Funcs->LoadPatch = NULL;
    Funcs->SetVolume = NULL;
    Funcs->GetVolume = NULL;

    if (card == WaveBlaster)
    {
        if (BLASTER_CardHasMixer())
        {
            BLASTER_SaveMidiVolume();
            Funcs->SetVolume = (void (*)(int volume))BLASTER_SetMidiVolume;
            Funcs->GetVolume = BLASTER_GetMidiVolume;
        }
    }

    if (MIDI_SetMidiFuncs(Funcs))
    {
        MUSIC_SetErrorCode(MUSIC_SoundCardError);
        status = MUSIC_Error;
    }

    return (status);
}
