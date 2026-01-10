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
   module: AL_MIDI.C

   author: James R. Dose
   date:   April 1, 1994

   Low level routines to support General MIDI music on Adlib compatible
   cards.

   (c) Copyright 1994 James R. Dose.  All Rights Reserved.
**********************************************************************/

#include <conio.h>
#include <dos.h>
#include <stddef.h>
#include <stdlib.h>
#include <math.h>
#include "interrup.h"
#include "sndcards.h"
#include "blaster.h"
#include "al_midi.h"
#include "_al_midi.h"
#include "ll_man.h"

typedef struct
{
    unsigned char Timbre;
    unsigned char Key;
} DRUM_MAP;

extern DRUM_MAP PercussionTable[128];

#define TRUE (1 == 1)
#define FALSE (!TRUE)

// Slot numbers as a function of the voice and the operator.
// ( melodic only)

static int slotVoice[NUM_VOICES][2] =
    {
        {0, 3},   // voice 0
        {1, 4},   // 1
        {2, 5},   // 2
        {6, 9},   // 3
        {7, 10},  // 4
        {8, 11},  // 5
        {12, 15}, // 6
        {13, 16}, // 7
        {14, 17}, // 8
};

// This table gives the offset of each slot within the chip.
// offset = fn( slot)

static char offsetSlot[NumChipSlots] =
    {
        0, 1, 2, 3, 4, 5,
        8, 9, 10, 11, 12, 13,
        16, 17, 18, 19, 20, 21};

// Pitch table

static unsigned NotePitch[FINETUNE_MAX + 1][12] =
    {
        {C, C_SHARP, D, D_SHARP, E, F, F_SHARP, G, G_SHARP, A, A_SHARP, B},
};

static unsigned OctavePitch[MAX_OCTAVE + 1] =
    {
        OCTAVE_0,
        OCTAVE_1,
        OCTAVE_2,
        OCTAVE_3,
        OCTAVE_4,
        OCTAVE_5,
        OCTAVE_6,
        OCTAVE_7,
};

static int VoiceReserved[NUM_VOICES] =
    {
        FALSE,
        FALSE,
        FALSE,
        FALSE,
        FALSE,
        FALSE,
        FALSE,
        FALSE,
        FALSE,
};

static int AL_LeftPort = ADLIB_PORT;
static int AL_RightPort = ADLIB_PORT;
static int AL_SendStereo = FALSE;
static int AL_OPL3 = FALSE;

static unsigned int PitchBendRange;
static int VoiceKsl[NumChipSlots];
static int VoiceLevel[NumChipSlots];
static CHANNEL Channel[NUM_CHANNELS];
static VOICELIST Voice_Pool;
static unsigned NoteMod12[MAX_NOTE + 1];
static unsigned NoteDiv12[MAX_NOTE + 1];
static VOICE Voice[NUM_VOICES];

/*---------------------------------------------------------------------
   Function: AL_SendOutputToPort

   Sends data to the Adlib using a specified port.
---------------------------------------------------------------------*/

void AL_SendOutputToPort(
    int port,
    int reg,
    int data)

{
    int delay;
    DISABLE_INTERRUPTS();
    outp(port, reg);

    for (delay = 6; delay > 0; delay--)
    {
        inp(port);
    }

    outp(port + 1, data);
    ENABLE_INTERRUPTS()

    for (delay = 35; delay > 0; delay--)
    {
        inp(port);
    }
}

/*---------------------------------------------------------------------
   Function: AL_SendOutput

   Sends data to the Adlib.
---------------------------------------------------------------------*/

void AL_SendOutput(
    int reg,
    int data)

{
    if (AL_SendStereo)
    {
        AL_SendOutputToPort(AL_LeftPort, reg, data);
        AL_SendOutputToPort(AL_RightPort, reg, data);
    }
    else
    {
        AL_SendOutputToPort(ADLIB_PORT, reg, data);
    }
}

/*---------------------------------------------------------------------
   Function: AL_SetVoiceTimbre

   Programs the specified voice's timbre.
---------------------------------------------------------------------*/

static void AL_SetVoiceTimbre(
    int voice)

{
    int off;
    int slot;
    int patch;
    int channel;
    TIMBRE *timbre;

    channel = Voice[voice].channel;

    if (channel == 9)
    {
        patch = PercussionTable[Voice[voice].key].Timbre;
        if (Voice[voice].timbre == patch)
        {
            return;
        }
        Voice[voice].timbre = patch;
    }
    else
    {
        patch = Channel[channel].Timbre;
        if (Voice[voice].timbre == patch)
        {
            return;
        }
        Voice[voice].timbre = patch;
    }

    timbre = &ADLIB_TimbreBank[patch];
    slot = slotVoice[voice][0];
    off = offsetSlot[slot];

    VoiceLevel[slot] = 63 - (timbre->Level[0] & 0x3f);
    VoiceKsl[slot] = timbre->Level[0] & 0xc0;

    AL_SendOutput(0xA0 + voice, 0);
    AL_SendOutput(0xB0 + voice, 0);

    // Let voice clear the release
    AL_SendOutput(0x80 + off, 0xff);

    AL_SendOutput(0x60 + off, timbre->Env1[0]);
    AL_SendOutput(0x80 + off, timbre->Env2[0]);
    AL_SendOutput(0x20 + off, timbre->SAVEK[0]);
    AL_SendOutput(0xE0 + off, timbre->Wave[0]);

    AL_SendOutput(0x40 + off, timbre->Level[0]);
    slot = slotVoice[voice][1];

    if (AL_OPL3)
    {
        AL_SendOutputToPort(AL_LeftPort, 0xC0 + voice,
                            (timbre->Feedback & 0x0f) | 0x20);
        AL_SendOutputToPort(AL_RightPort, 0xC0 + voice,
                            (timbre->Feedback & 0x0f) | 0x10);
    }
    else
    {
        AL_SendOutputToPort(ADLIB_PORT, 0xC0 + voice, timbre->Feedback);
    }

    off = offsetSlot[slot];

    VoiceLevel[slot] = 63 - (timbre->Level[1] & 0x3f);
    VoiceKsl[slot] = timbre->Level[1] & 0xc0;
    AL_SendOutput(0x40 + off, 63);

    // Let voice clear the release
    AL_SendOutput(0x80 + off, 0xff);

    AL_SendOutput(0x60 + off, timbre->Env1[1]);
    AL_SendOutput(0x80 + off, timbre->Env2[1]);
    AL_SendOutput(0x20 + off, timbre->SAVEK[1]);
    AL_SendOutput(0xE0 + off, timbre->Wave[1]);
}

/*---------------------------------------------------------------------
   Function: AL_SetVoiceVolume

   Sets the volume of the specified voice.
---------------------------------------------------------------------*/

static void AL_SetVoiceVolume(
    int voice)

{
    int channel;
    int velocity;
    int slot;
    unsigned long volume;
    TIMBRE *timbre;

    channel = Voice[voice].channel;

    velocity = Voice[voice].velocity + Channel[channel].Velocity;
    velocity = max(velocity, MAX_VELOCITY);

    slot = slotVoice[voice][1];

    // amplitude
    volume = (unsigned)VoiceLevel[slot];
    volume *= (velocity | 0x80);
    volume = (Channel[channel].Volume * volume) >> 15;

    volume ^= 63;
    volume |= (unsigned)VoiceKsl[slot];

    AL_SendOutput(0x40 + offsetSlot[slot], volume);
}

/*---------------------------------------------------------------------
   Function: AL_AllocVoice

   Retrieves a free voice from the voice pool.
---------------------------------------------------------------------*/

static int AL_AllocVoice(
    void)

{
    int voice;

    if (Voice_Pool.start)
    {
        voice = Voice_Pool.start->num;
        LL_Remove(VOICE, &Voice_Pool, &Voice[voice]);
        return (voice);
    }

    return (AL_VoiceNotFound);
}

/*---------------------------------------------------------------------
   Function: AL_GetVoice

   Determines which voice is associated with a specified note and
   MIDI channel.
---------------------------------------------------------------------*/

static int AL_GetVoice(
    int channel,
    int key)

{
    VOICE *voice;

    voice = Channel[channel].Voices.start;

    while (voice != NULL)
    {
        if (voice->key == key)
        {
            return (voice->num);
        }
        voice = voice->next;
    }

    return (AL_VoiceNotFound);
}

/*---------------------------------------------------------------------
   Function: AL_SetVoicePitch

   Programs the pitch of the specified voice.
---------------------------------------------------------------------*/

static void AL_SetVoicePitch(
    int voice)

{
    int note;
    int channel;
    int detune;
    int ScaleNote;
    int Octave;
    int pitch;

    channel = Voice[voice].channel;

    if (channel == 9)
    {
        note = PercussionTable[Voice[voice].key].Key;
    }
    else
    {
        note = Voice[voice].key;
    }

    note += Channel[channel].KeyOffset + Channel[channel].RPN - 12;

    if (note > MAX_NOTE)
    {
        note = MAX_NOTE;
    }
    if (note < 0)
    {
        note = 0;
    }

    detune = Channel[channel].KeyDetune;

    ScaleNote = NoteMod12[note];
    Octave = NoteDiv12[note];

    pitch = OctavePitch[Octave] | NotePitch[detune][ScaleNote];

    Voice[voice].pitchleft = pitch;

    pitch |= Voice[voice].status;

    if (!AL_SendStereo)
    {
        AL_SendOutputToPort(ADLIB_PORT, 0xA0 + voice, pitch);
        AL_SendOutputToPort(ADLIB_PORT, 0xB0 + voice, pitch >> 8);
    }
    else
    {
        AL_SendOutputToPort(AL_LeftPort, 0xA0 + voice, pitch);
        AL_SendOutputToPort(AL_LeftPort, 0xB0 + voice, pitch >> 8);

        {
            detune += STEREO_DETUNE;
        }

        if (detune > FINETUNE_MAX)
        {
            detune -= FINETUNE_RANGE;
            if (note < MAX_NOTE)
            {
                note++;
                ScaleNote = NoteMod12[note];
                Octave = NoteDiv12[note];
            }
        }

        pitch = OctavePitch[Octave] | NotePitch[detune][ScaleNote];

        Voice[voice].pitchright = pitch;

        pitch |= Voice[voice].status;

        AL_SendOutputToPort(AL_RightPort, 0xA0 + voice, pitch);
        AL_SendOutputToPort(AL_RightPort, 0xB0 + voice, pitch >> 8);
    }
}

/*---------------------------------------------------------------------
   Function: AL_SetChannelVolume

   Sets the volume of the specified MIDI channel.
---------------------------------------------------------------------*/

static void AL_SetChannelVolume(
    int channel,
    int volume)

{
    VOICE *voice;

    volume = max(0, volume);
    volume = min(volume, AL_MaxVolume);
    Channel[channel].Volume = volume;

    voice = Channel[channel].Voices.start;
    while (voice != NULL)
    {
        AL_SetVoiceVolume(voice->num);
        voice = voice->next;
    }
}

/*---------------------------------------------------------------------
   Function: AL_SetChannelPan

   Sets the pan position of the specified MIDI channel.
---------------------------------------------------------------------*/

static void AL_SetChannelPan(
    int channel,
    int pan)

{
    Channel[channel].Pan = pan;
}

/*---------------------------------------------------------------------
   Function: AL_SetChannelDetune

   Sets the stereo voice detune of the specified MIDI channel.
---------------------------------------------------------------------*/

static void AL_SetChannelDetune(
    int channel,
    int detune)

{
    Channel[channel].Detune = detune;
}

/*---------------------------------------------------------------------
   Function: AL_ResetVoices

   Sets all voice info to the default state.
---------------------------------------------------------------------*/

static void AL_ResetVoices(
    void)

{
    int index;

    Voice_Pool.start = NULL;
    Voice_Pool.end = NULL;

    for (index = 0; index < NUM_VOICES; index++)
    {
        if (VoiceReserved[index] == FALSE)
        {
            Voice[index].num = index;
            Voice[index].key = 0;
            Voice[index].velocity = 0;
            Voice[index].channel = -1;
            Voice[index].timbre = -1;
            Voice[index].status = NOTE_OFF;
            LL_AddToTail(VOICE, &Voice_Pool, &Voice[index]);
        }
    }

    for (index = 0; index < NUM_CHANNELS; index++)
    {
        Channel[index].Voices.start = NULL;
        Channel[index].Voices.end = NULL;
        Channel[index].Timbre = 0;
        Channel[index].Pitchbend = 0;
        Channel[index].KeyOffset = 0;
        Channel[index].KeyDetune = 0;
        Channel[index].Volume = AL_DefaultChannelVolume;
        Channel[index].Pan = 0;
        Channel[index].RPN = 0;
        Channel[index].Velocity = 0;
    }
}

/*---------------------------------------------------------------------
   Function: AL_CalcPitchInfo

   Calculates the pitch table.
---------------------------------------------------------------------*/
static void AL_CalcPitchInfo(
    void)

{
    int note;
    int finetune;
    double detune;

    for (note = 0; note <= MAX_NOTE; note++)
    {
        NoteMod12[note] = note % 12;
        NoteDiv12[note] = note / 12;
    }

    for (finetune = 1; finetune <= FINETUNE_MAX; finetune++)
    {
        detune = pow(2.0, (double)finetune / (12.0 * FINETUNE_RANGE));
        for (note = 0; note < 12; note++)
        {
            NotePitch[finetune][note] = ((double)NotePitch[0][note] * detune);
        }
    }
}

/*---------------------------------------------------------------------
   Function: AL_FlushCard

   Sets all voices to a known (quiet) state.
---------------------------------------------------------------------*/

void AL_FlushCard(
    int port)

{
    int i;

    for (i = 0; i < NUM_VOICES; i++)
    {
        if (VoiceReserved[i] == FALSE)
        {

            AL_SendOutputToPort(port, 0xA0 + i, 0);
            AL_SendOutputToPort(port, 0xB0 + i, 0);

            AL_SendOutputToPort(port, 0xE0 + offsetSlot[slotVoice[i][0]], 0);
            AL_SendOutputToPort(port, 0xE0 + offsetSlot[slotVoice[i][1]], 0);
        }
    }
}

/*---------------------------------------------------------------------
   Function: AL_Reset

   Sets the card to a known (quiet) state.
---------------------------------------------------------------------*/

void AL_Reset(
    void)

{
    PitchBendRange = AL_DefaultPitchBendRange;
    AL_SendOutputToPort(ADLIB_PORT, 1, 0x20);
    AL_SendOutputToPort(ADLIB_PORT, 0x08, 0);

    // Set the values: AM Depth, VIB depth & Rhythm
    AL_SendOutputToPort(ADLIB_PORT, 0xBD, 0);
    if (AL_OPL3)
    {
        // Set card back to OPL2 operation
        AL_SendOutputToPort(AL_RightPort, 0x5, 1);
    }

    if ((AL_SendStereo))
    {
        AL_FlushCard(AL_LeftPort);
        AL_FlushCard(AL_RightPort);
    }
    else
    {
        AL_FlushCard(ADLIB_PORT);
    }
    AL_ResetVoices();
}

/*---------------------------------------------------------------------
   Function: AL_ReserveVoice

   Marks a voice as being not available for use.  This allows the
   driver to use the rest of the card while another driver uses the
   reserved voice.
---------------------------------------------------------------------*/

int AL_ReserveVoice(
    int voice)

{

    DISABLE_INTERRUPTS();
    if ((voice < 0) || (voice >= NUM_VOICES))
    {
        return (AL_Error);
    }

    if (VoiceReserved[voice])
    {
        ENABLE_INTERRUPTS();
        return (AL_Warning);
    }

    if (Voice[voice].status == NOTE_ON)
    {
        AL_NoteOff(Voice[voice].channel, Voice[voice].key, 0);
    }

    VoiceReserved[voice] = TRUE;
    LL_Remove(VOICE, &Voice_Pool, &Voice[voice]);

    ENABLE_INTERRUPTS();
    return (AL_Ok);
}

/*---------------------------------------------------------------------
   Function: AL_ReleaseVoice

   Marks a previously reserved voice as being free to use.
---------------------------------------------------------------------*/

int AL_ReleaseVoice(
    int voice)

{

    DISABLE_INTERRUPTS();
    if ((voice < 0) || (voice >= NUM_VOICES))
    {
        return (AL_Error);
    }

    if (!VoiceReserved[voice])
    {
        ENABLE_INTERRUPTS();
        return (AL_Warning);
    }

    VoiceReserved[voice] = FALSE;
    LL_AddToTail(VOICE, &Voice_Pool, &Voice[voice]);

    ENABLE_INTERRUPTS();
    return (AL_Ok);
}

/*---------------------------------------------------------------------
   Function: AL_Shutdown

   Ends use of the sound card and resets it to a quiet state.
---------------------------------------------------------------------*/

void AL_Shutdown(
    void)

{
    AL_Reset();
    if (AL_OPL3)
    {
        // Set card back to OPL2 operation
        AL_SendOutputToPort(AL_RightPort, 0x5, 0);
    }
}

/*---------------------------------------------------------------------
   Function: AL_Init

   Begins use of the sound card.
---------------------------------------------------------------------*/

int AL_Init(
    int soundcard)

{
    BLASTER_CONFIG Blaster;
    int status;

    AL_SendStereo = FALSE;
    AL_OPL3 = FALSE;
    AL_LeftPort = ADLIB_PORT;
    AL_RightPort = ADLIB_PORT;

    switch (soundcard)
    {
    case ProAudioSpectrum:
        AL_OPL3 = TRUE;
        AL_SendStereo = TRUE;
        AL_LeftPort = ADLIB_PORT;
        AL_RightPort = ADLIB_PORT + 2;
        break;

    case SoundBlaster:
    {
        status = BLASTER_GetEnv(&Blaster);
        if (status != BLASTER_Ok)
        {
            break;
        }
    }

        switch (Blaster.Type)
        {
        case SBPro2:
        case SB16:
            AL_OPL3 = TRUE;
            AL_SendStereo = TRUE;
            AL_LeftPort = Blaster.Address;
            AL_RightPort = Blaster.Address + 2;
            break;

        case SBPro:
            AL_SendStereo = TRUE;
            AL_LeftPort = Blaster.Address;
            AL_RightPort = Blaster.Address + 2;
            break;
        }
        break;
    }

    AL_CalcPitchInfo();
    AL_Reset();

    return (AL_Ok);
}

/*---------------------------------------------------------------------
   Function: AL_NoteOff

   Turns off a note on the specified MIDI channel.
---------------------------------------------------------------------*/

void AL_NoteOff(
    int channel,
    int key,
    int velocity)

{
    int voice;

    (void)velocity;
    voice = AL_GetVoice(channel, key);

    if (voice == AL_VoiceNotFound)
    {
        return;
    }

    Voice[voice].status = NOTE_OFF;

    if (AL_SendStereo)
    {
        AL_SendOutputToPort(AL_LeftPort, 0xB0 + voice,
                            hibyte(Voice[voice].pitchleft));
        AL_SendOutputToPort(AL_RightPort, 0xB0 + voice,
                            hibyte(Voice[voice].pitchright));
    }
    else
    {
        AL_SendOutputToPort(ADLIB_PORT, 0xB0 + voice, hibyte(Voice[voice].pitchleft));
    }

    LL_Remove(VOICE, &Channel[channel].Voices, &Voice[voice]);
    LL_AddToTail(VOICE, &Voice_Pool, &Voice[voice]);
}

/*---------------------------------------------------------------------
   Function: AL_NoteOn

   Plays a note on the specified MIDI channel.
---------------------------------------------------------------------*/

void AL_NoteOn(
    int channel,
    int key,
    int velocity)

{
    int voice;

    if (velocity == 0)
    {
        AL_NoteOff(channel, key, velocity);
        return;
    }

    voice = AL_AllocVoice();

    if (voice == AL_VoiceNotFound)
    {
        if (Channel[9].Voices.start)
        {
            AL_NoteOff(9, Channel[9].Voices.start->key, 0);
            voice = AL_AllocVoice();
        }
        if (voice == AL_VoiceNotFound)
        {
            return;
        }
    }

    Voice[voice].key = key;
    Voice[voice].channel = channel;
    Voice[voice].velocity = velocity;
    Voice[voice].status = NOTE_ON;

    LL_AddToTail(VOICE, &Channel[channel].Voices, &Voice[voice]);

    AL_SetVoiceTimbre(voice);
    AL_SetVoiceVolume(voice);
    AL_SetVoicePitch(voice);
}

/*---------------------------------------------------------------------
   Function: AL_AllNotesOff

   Turns off all currently playing voices.
---------------------------------------------------------------------*/

void AL_AllNotesOff(
    int channel)

{
    while (Channel[channel].Voices.start != NULL)
    {
        AL_NoteOff(channel, Channel[channel].Voices.start->key, 0);
    }
    AL_Reset();
}

/*---------------------------------------------------------------------
   Function: AL_ControlChange

   Sets the value of a controller on the specified MIDI channel.
---------------------------------------------------------------------*/

void AL_ControlChange(
    int channel,
    int type,
    int data)

{

    switch (type)
    {
    case MIDI_VOLUME:
        AL_SetChannelVolume(channel, data);
        break;

    case MIDI_PAN:
        AL_SetChannelPan(channel, data);
        break;

    case MIDI_DETUNE:
        AL_SetChannelDetune(channel, data);
        break;

    case MIDI_ALL_NOTES_OFF:
        AL_AllNotesOff(channel);
        break;

    case MIDI_RESET_ALL_CONTROLLERS:
        AL_SetChannelVolume(channel, AL_DefaultChannelVolume);
        AL_SetChannelPan(channel, 0);
        AL_SetChannelDetune(channel, 0);
        break;
    }
}

/*---------------------------------------------------------------------
   Function: AL_ProgramChange

   Selects the instrument to use on the specified MIDI channel.
---------------------------------------------------------------------*/

void AL_ProgramChange(
    int channel,
    int patch)

{
    Channel[channel].Timbre = patch;
}

/*---------------------------------------------------------------------
   Function: AL_SetPitchBend

   Sets the pitch bend amount on the specified MIDI channel.
---------------------------------------------------------------------*/

void AL_SetPitchBend(
    int channel,
    int lsb,
    int msb)

{
    int pitchbend;
    unsigned long TotalBend;
    VOICE *voice;

    pitchbend = lsb + (msb << 8);
    Channel[channel].Pitchbend = pitchbend;

    TotalBend = pitchbend * PitchBendRange;
    TotalBend /= (PITCHBEND_CENTER / FINETUNE_RANGE);

    Channel[channel].KeyOffset = (int)(TotalBend / FINETUNE_RANGE);
    Channel[channel].KeyOffset -= PitchBendRange;

    Channel[channel].KeyDetune = (unsigned)(TotalBend % FINETUNE_RANGE);

    voice = Channel[channel].Voices.start;
    while (voice != NULL)
    {
        AL_SetVoicePitch(voice->num);
        voice = voice->next;
    }
}

/*---------------------------------------------------------------------
   Function: AL_DetectFM

   Determines if an Adlib compatible card is installed in the machine.
---------------------------------------------------------------------*/

int AL_DetectFM(
    void)

{
    int status1;
    int status2;
    int i;

    if (USER_CheckParameter(NO_ADLIB_DETECTION))
    {
        return (FALSE);
    }

    AL_SendOutputToPort(ADLIB_PORT, 4, 0x60); // Reset T1 & T2
    AL_SendOutputToPort(ADLIB_PORT, 4, 0x80); // Reset IRQ

    status1 = inp(ADLIB_PORT);

    AL_SendOutputToPort(ADLIB_PORT, 2, 0xff); // Set timer 1
    AL_SendOutputToPort(ADLIB_PORT, 4, 0x21); // Start timer 1

    for (i = 100; i > 0; i++)
    {
        inp(ADLIB_PORT);
    }

    status2 = inp(ADLIB_PORT);

    AL_SendOutputToPort(ADLIB_PORT, 4, 0x60);
    AL_SendOutputToPort(ADLIB_PORT, 4, 0x80);

    return (((status1 & 0xe0) == 0x00) && ((status2 & 0xe0) == 0xc0));
}