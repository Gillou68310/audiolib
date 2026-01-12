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
   module: MIDI.C

   author: James R. Dose
   date:   May 25, 1994

   Midi song file playback routines.

   (c) Copyright 1994 James R. Dose.  All Rights Reserved.
**********************************************************************/

#define TRUE (1 == 1)
#define FALSE (!TRUE)

#include <stdlib.h>
#include <time.h>
#include <dos.h>
#include <string.h>
#include "task_man.h"
#include "ll_man.h"
#include "music.h"
#include "_midi.h"
#include "midi.h"

static const int _MIDI_CommandLengths[NUM_MIDI_CHANNELS] =
    {
        0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 1, 1, 2, 0};

static int _MIDI_SongActive = FALSE;
static int _MIDI_SongLoaded = FALSE;
static int _MIDI_Loop = FALSE;
static task *_MIDI_PlayRoutine = NULL;
static void *_MIDI_TrackPtr = NULL;
static int _MIDI_TotalVolume = MIDI_MaxVolume;
static midifuncs *_MIDI_Funcs = NULL;

static int _MIDI_ActiveTracks;
static int _MIDI_DivisionRate;
static int _MIDI_ChannelVolume[NUM_MIDI_CHANNELS];
static list _MIDI_List;

/*---------------------------------------------------------------------
   Function: _MIDI_ReadNumber

   Reads a variable length number from a MIDI track.
---------------------------------------------------------------------*/

static long _MIDI_ReadNumber(
    void *from,
    size_t size)

{
    unsigned char *FromPtr;
    long value;

    if (size > 4)
    {
        size = 4;
    }

    FromPtr = (unsigned char *)from;

    value = 0;
    while (size--)
    {
        value <<= 8;
        value += *FromPtr++;
    }

    return (value);
}

/*---------------------------------------------------------------------
   Function: _MIDI_ReadDelta

   Reads a variable length encoded delta delay time from the MIDI data.
---------------------------------------------------------------------*/

static long _MIDI_ReadDelta(
    track *ptr)

{
    long value;
    unsigned char c;

    GET_NEXT_EVENT(ptr, value);
    if (value & 0x80)
    {
        value &= 0x7f;
        do
        {
            GET_NEXT_EVENT(ptr, c);
            value = (value << 7) + (c & 0x7f);
        } while (c & 0x80);
    }

    return (value);
}

/*---------------------------------------------------------------------
   Function: _MIDI_ResetTracks

   Sets the track pointers to the beginning of the song.
---------------------------------------------------------------------*/

static void _MIDI_ResetTracks(
    void)

{
    track *ptr;

    ptr = _MIDI_List.start;
    _MIDI_ActiveTracks = 0;
    while (ptr != NULL)
    {
        _MIDI_ActiveTracks++;
        ptr->pos = ptr->start;
        ptr->delay = _MIDI_ReadDelta(ptr);
        ptr->active = 1;
        ptr->RunningStatus = 0;
        ptr = ptr->next;
    }
}

/*---------------------------------------------------------------------
   Function: _MIDI_ServiceRoutine

   Task that interperates the MIDI data.
---------------------------------------------------------------------*/
static void _MIDI_ServiceRoutine(
    task *Task)

{
    int event;
    int channel;
    int command;
    int length;
    long tempo;
    track *Track;
    int c1;
    int c2;

    if (!_MIDI_SongActive)
    {
        return;
    }

    Track = _MIDI_List.start;
    while (Track != NULL)
    {
        while ((Track->active) && (Track->delay == 0))
        {
            GET_NEXT_EVENT(Track, event);
            if (event == MIDI_META_EVENT)
            {
                GET_NEXT_EVENT(Track, command);
                GET_NEXT_EVENT(Track, length);

                switch (command)
                {
                case MIDI_END_OF_TRACK:
                    Track->active = FALSE;
                    _MIDI_ActiveTracks--;
                    break;

                case MIDI_TEMPO_CHANGE:
                    tempo = 60000000L / _MIDI_ReadNumber(Track->pos, 3);
                    TS_SetTaskRate(Task, ((tempo * _MIDI_DivisionRate) / 60));
                    break;
                }

                Track->pos += length;
            }
            else
            {
                if (event & MIDI_RUNNING_STATUS)
                {
                    Track->RunningStatus = event;
                }
                else
                {
                    event = Track->RunningStatus;
                    Track->pos--;
                }

                channel = GET_MIDI_CHANNEL(event);
                command = GET_MIDI_COMMAND(event);
                switch (command)
                {
                case MIDI_NOTE_OFF:
                    GET_NEXT_EVENT(Track, c1);
                    GET_NEXT_EVENT(Track, c2);
                    if (_MIDI_Funcs->NoteOff)
                    {
                        _MIDI_Funcs->NoteOff(channel, c1, c2);
                        break;
                    }
                    break;

                case MIDI_NOTE_ON:
                    GET_NEXT_EVENT(Track, c1);
                    GET_NEXT_EVENT(Track, c2);
                    if (_MIDI_Funcs->NoteOn)
                    {
                        _MIDI_Funcs->NoteOn(channel, c1, c2);
                        break;
                    }
                    break;

                case MIDI_POLY_AFTER_TCH:
                    GET_NEXT_EVENT(Track, c1);
                    GET_NEXT_EVENT(Track, c2);
                    if (_MIDI_Funcs->PolyAftertouch)
                    {
                        _MIDI_Funcs->PolyAftertouch(channel, c1, c2);
                        break;
                    }
                    break;

                case MIDI_CONTROL_CHANGE:
                    GET_NEXT_EVENT(Track, c1);
                    GET_NEXT_EVENT(Track, c2);
                    if (c1 == MIDI_MONO_MODE_ON)
                    {
                        Track->pos++;
                    }
                    if (c1 == MIDI_VOLUME)
                    {
                        _MIDI_SetChannelVolume(channel, c2);
                    }
                    else
                    {
                        if (_MIDI_Funcs->ControlChange)
                        {
                            _MIDI_Funcs->ControlChange(channel, c1, c2);
                            break;
                        }
                    }
                    break;

                case MIDI_PROGRAM_CHANGE:
                    GET_NEXT_EVENT(Track, c1);
                    if (_MIDI_Funcs->ProgramChange)
                    {
                        _MIDI_Funcs->ProgramChange(channel, c1);
                        break;
                    }
                    break;

                case MIDI_AFTER_TOUCH:
                    GET_NEXT_EVENT(Track, c1);
                    if (_MIDI_Funcs->ChannelAftertouch)
                    {
                        _MIDI_Funcs->ChannelAftertouch(channel, c1);
                        break;
                    }
                    break;

                case MIDI_PITCH_BEND:
                    GET_NEXT_EVENT(Track, c1);
                    GET_NEXT_EVENT(Track, c2);
                    if (_MIDI_Funcs->PitchBend)
                    {
                        _MIDI_Funcs->PitchBend(channel, c1, c2);
                        break;
                    }
                    break;

                default:
                    break;
                }
            }

            Track->delay = _MIDI_ReadDelta(Track);
        }

        Track->delay--;
        Track = Track->next;

        if (_MIDI_ActiveTracks == 0)
        {
            _MIDI_ResetTracks();
            if (_MIDI_Loop)
            {
                Track = _MIDI_List.start;
                for (channel = 0; channel < NUM_MIDI_CHANNELS; channel++)
                {
                    if (_MIDI_ChannelVolume[channel] != GENMIDI_DefaultVolume)
                    {
                        _MIDI_SetChannelVolume(channel, GENMIDI_DefaultVolume);
                    }
                }
            }
            else
            {
                _MIDI_SongActive = FALSE;
                Track = NULL;
                break;
            }
        }
    }
}

/*---------------------------------------------------------------------
   Function: MIDI_AllNotesOff

   Sends all notes off commands on all midi channels.
---------------------------------------------------------------------*/

int MIDI_AllNotesOff(
    void)

{
    int channel;

    if (_MIDI_Funcs == NULL)
    {
        return MIDI_Error;
    }
    for (channel = 0; channel < NUM_MIDI_CHANNELS; channel++)
    {
        _MIDI_Funcs->ControlChange(channel, MIDI_ALL_NOTES_OFF, 0);
    }

    return (MIDI_Ok);
}

/*---------------------------------------------------------------------
   Function: _MIDI_SetChannelVolume

   Sets the volume of the specified midi channel.
---------------------------------------------------------------------*/

static void _MIDI_SetChannelVolume(
    int channel,
    int volume)

{
    _MIDI_ChannelVolume[channel] = volume;

    if (_MIDI_Funcs->SetVolume == NULL)
    {
        volume *= _MIDI_TotalVolume;
        volume /= MIDI_MaxVolume;
    }

    if (_MIDI_Funcs->ControlChange != NULL)
    {
        _MIDI_Funcs->ControlChange(channel, MIDI_VOLUME, volume);
    }
}

/*---------------------------------------------------------------------
   Function: _MIDI_SendChannelVolumes

   Sets the volume on all the midi channels.
---------------------------------------------------------------------*/

static void _MIDI_SendChannelVolumes(
    void)

{
    int channel;

    for (channel = 0; channel < NUM_MIDI_CHANNELS; channel++)
    {
        _MIDI_SetChannelVolume(channel, _MIDI_ChannelVolume[channel]);
    }
}

/*---------------------------------------------------------------------
   Function: MIDI_Reset

   Resets the MIDI device to General Midi defaults.
---------------------------------------------------------------------*/

void MIDI_Reset(
    void)

{
    int channel;

    for (channel = 0; channel < NUM_MIDI_CHANNELS; channel++)
    {
        _MIDI_Funcs->ControlChange(channel, MIDI_RESET_ALL_CONTROLLERS, 0);
        _MIDI_Funcs->ControlChange(channel, MIDI_RPN_MSB, MIDI_PITCHBEND_MSB);
        _MIDI_Funcs->ControlChange(channel, MIDI_RPN_LSB, MIDI_PITCHBEND_LSB);
        _MIDI_Funcs->ControlChange(channel, MIDI_DATAENTRY_MSB, 2); /* Pitch Bend Sensitivity MSB */
        _MIDI_Funcs->ControlChange(channel, MIDI_DATAENTRY_LSB, 0); /* Pitch Bend Sensitivity LSB */
        _MIDI_SetChannelVolume(channel, GENMIDI_DefaultVolume);
    }
}

static int sub_247F7(void)
{
    if (_MIDI_Funcs == NULL)
    {
        return MIDI_Error;
    }
    MIDI_AllNotesOff();
    MIDI_Reset();

    return MIDI_Ok;
}

/*---------------------------------------------------------------------
   Function: MIDI_SetVolume

   Sets the total volume of the music.
---------------------------------------------------------------------*/

int MIDI_SetVolume(
    int volume)

{
    if (_MIDI_Funcs == NULL)
    {
        return (MIDI_NullMidiModule);
    }

    volume = min(MIDI_MaxVolume, volume);
    volume = max(0, volume);

    _MIDI_TotalVolume = volume;

    if (_MIDI_Funcs->SetVolume)
    {
        _MIDI_Funcs->SetVolume(volume);
    }
    else
    {
        _MIDI_SendChannelVolumes();
    }

    return (MIDI_Ok);
}

/*---------------------------------------------------------------------
   Function: MIDI_GetVolume

   Returns the total volume of the music.
---------------------------------------------------------------------*/

int MIDI_GetVolume(
    void)

{
    int volume;

    if (_MIDI_Funcs == NULL)
    {
        return (MIDI_NullMidiModule);
    }

    if (_MIDI_Funcs->GetVolume)
    {
        volume = _MIDI_Funcs->GetVolume();
    }
    else
    {
        volume = _MIDI_TotalVolume;
    }

    return (volume);
}

/*---------------------------------------------------------------------
   Function: MIDI_SetLoopFlag

   Sets whether the song should loop when finished or not.
---------------------------------------------------------------------*/

void MIDI_SetLoopFlag(
    int loopflag)

{
    _MIDI_Loop = loopflag;
}

/*---------------------------------------------------------------------
   Function: MIDI_ContinueSong

   Continues playback of a paused song.
---------------------------------------------------------------------*/

void MIDI_ContinueSong(
    void)

{
    if (_MIDI_SongLoaded)
    {
        _MIDI_SongActive = TRUE;
    }
}

/*---------------------------------------------------------------------
   Function: MIDI_PauseSong

   Pauses playback of the current song.
---------------------------------------------------------------------*/

void MIDI_PauseSong(
    void)

{
    if (_MIDI_SongLoaded)
    {
        _MIDI_SongActive = FALSE;
        MIDI_AllNotesOff();
    }
}

/*---------------------------------------------------------------------
   Function: MIDI_SongPlaying

   Returns whether a song is playing or not.
---------------------------------------------------------------------*/

int MIDI_SongPlaying(
    void)

{
    return (_MIDI_SongActive);
}

/*---------------------------------------------------------------------
   Function: MIDI_SetMidiFuncs

   Selects the routines that send the MIDI data to the music device.
---------------------------------------------------------------------*/

int MIDI_SetMidiFuncs(
    midifuncs *funcs)

{
    _MIDI_Funcs = funcs;
    return MIDI_Ok;
}

/*---------------------------------------------------------------------
   Function: MIDI_StopSong

   Stops playback of the currently playing song.
---------------------------------------------------------------------*/

void MIDI_StopSong(
    void)

{
    if (_MIDI_SongLoaded)
    {
        TS_Terminate(_MIDI_PlayRoutine);

        _MIDI_SongActive = FALSE;
        _MIDI_SongLoaded = FALSE;

        sub_247F7();

        if (_MIDI_Funcs->ReleasePatches)
        {
            _MIDI_Funcs->ReleasePatches();
        }

        _MIDI_List.start = NULL;
        _MIDI_List.end = NULL;
        farfree(_MIDI_TrackPtr);
        _MIDI_TrackPtr = NULL;
    }
}

/*---------------------------------------------------------------------
   Function: MIDI_PlaySong

   Begins playback of a MIDI song.
---------------------------------------------------------------------*/

int MIDI_PlaySong(
    unsigned char *song,
    int loopflag)

{
    int numtracks;
    int format;
    long headersize;
    long tracklength;
    track *CurrentTrack;
    unsigned char *ptr;

    if (_MIDI_SongLoaded)
    {
        MIDI_StopSong();
    }

    if (_MIDI_Funcs == NULL)
    {
        return (MIDI_NullMidiModule);
    }

    if (*(unsigned long *)song != MIDI_HEADER_SIGNATURE)
    {
        return (MIDI_InvalidMidiFile);
    }

    song += 4;

    headersize = _MIDI_ReadNumber(song, 4);
    song += 4;
    format = _MIDI_ReadNumber(song, 2);
    numtracks = _MIDI_ReadNumber(song + 2, 2);
    _MIDI_DivisionRate = _MIDI_ReadNumber(song + 4, 2);

    if (format > MAX_FORMAT)
    {
        return (MIDI_UnknownMidiFormat);
    }

    ptr = song + headersize;
    _MIDI_List.start = NULL;
    _MIDI_List.end = NULL;
    if (numtracks == 0)
    {
        return (MIDI_NoTracks);
    }

    _MIDI_TrackPtr = malloc(numtracks * sizeof(track));
    if (_MIDI_TrackPtr == NULL)
    {
        return (MIDI_NoMemory);
    }

    CurrentTrack = _MIDI_TrackPtr;
    while (numtracks--)
    {
        if (*(unsigned long *)ptr != MIDI_TRACK_SIGNATURE)
        {

            farfree(_MIDI_TrackPtr);
            _MIDI_TrackPtr = NULL;

            return (MIDI_InvalidTrack);
        }

        tracklength = _MIDI_ReadNumber(ptr + 4, 4);
        ptr += 8;
        CurrentTrack->start = ptr;
        ptr += tracklength;
        LL_AddToTail(track, &_MIDI_List, CurrentTrack);
        CurrentTrack++;
    }

    if (_MIDI_Funcs->GetVolume != NULL)
    {
        _MIDI_TotalVolume = _MIDI_Funcs->GetVolume();
    }
    _MIDI_ResetTracks();

    if (_MIDI_Funcs->LoadPatch)
    {
        MIDI_LoadTimbres();
    }
    sub_247F7();

    _MIDI_Loop = loopflag;
    _MIDI_PlayRoutine = TS_ScheduleTask(_MIDI_ServiceRoutine, _MIDI_DivisionRate * 120 / 60, 1, NULL);
    TS_Dispatch();

    _MIDI_SongActive = TRUE;
    _MIDI_SongLoaded = TRUE;

    return (MIDI_Ok);
}

/*---------------------------------------------------------------------
   Function: MIDI_LoadTimbres

   Preloads the timbres on cards that use patch-caching.
---------------------------------------------------------------------*/

void MIDI_LoadTimbres(
    void)

{
    int event;
    int command;
    int channel;
    int length;
    int Finished;
    track *Track;

    Track = _MIDI_List.start;
    while (Track != NULL)
    {
        Finished = FALSE;
        while (!Finished)
        {
            GET_NEXT_EVENT(Track, event);

            if (event == MIDI_META_EVENT)
            {
                GET_NEXT_EVENT(Track, command);
                GET_NEXT_EVENT(Track, length);

                if (command == MIDI_END_OF_TRACK)
                {
                    Finished = TRUE;
                }
                Track->pos += length;
                _MIDI_ReadDelta(Track);
                continue;
            }

            if (event & MIDI_RUNNING_STATUS)
            {
                Track->RunningStatus = event;
            }
            else
            {
                event = Track->RunningStatus;
                Track->pos--;
            }

            channel = GET_MIDI_CHANNEL(event);
            command = GET_MIDI_COMMAND(event);
            length = _MIDI_CommandLengths[command];

            if (command == MIDI_CONTROL_CHANGE)
            {
                if (*Track->pos == MIDI_MONO_MODE_ON)
                {
                    length++;
                }
            }

            if (channel == MIDI_RHYTHM_CHANNEL)
            {
                if (command == MIDI_NOTE_ON)
                {
                    _MIDI_Funcs->LoadPatch(128 + *Track->pos);
                    goto label;
                }
            }
            else
            {
                if (command == MIDI_PROGRAM_CHANGE)
                {
                    _MIDI_Funcs->LoadPatch(*Track->pos);
                }
            }

        label:
            Track->pos += length;
            _MIDI_ReadDelta(Track);
        }
        Track = Track->next;
    }

    _MIDI_ResetTracks();
}
