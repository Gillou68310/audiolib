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
   module: PAS16.C

   author: James R. Dose
   date:   March 27, 1994

   Low level routines to support Pro AudioSpectrum and compatible
   sound cards.

   (c) Copyright 1994 James R. Dose.  All Rights Reserved.
**********************************************************************/

#include <dos.h>
#include <conio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "dma.h"
#include "interrup.h"
#include "pas16.h"
#include "_pas16.h"

static const int PAS_Interrupts[PAS_MaxIrq + 1] =
    {
        INVALID, INVALID, 0xa, 0xb,
        INVALID, 0xd, INVALID, 0xf,
        INVALID, INVALID, 0x72, 0x73,
        0x74, INVALID, INVALID, 0x77};

static int PAS_Installed = FALSE;
static int PAS_TranslateCode = DEFAULT_BASE;

static int PAS_OriginalPCMLeftVolume = 75;
static int PAS_OriginalPCMRightVolume = 75;

static int PAS_OriginalFMLeftVolume = 75;
static int PAS_OriginalFMRightVolume = 75;

unsigned int PAS_DMAChannel = 1;
static unsigned int PAS_Irq = 7;
static MVState *PAS_State = NULL;
static MVFunc *PAS_Func = NULL;

static int PAS_TransferLength = 0;
static int PAS_MixMode = PAS_DefaultMixMode;
static unsigned PAS_SampleRate = PAS_DefaultSampleRate;
static int PAS_TimeInterval = 0;

static int PAS_VolumeTable[16] = {0, 28, 36, 42, 49, 56, 62, 68, 74, 80, 85, 89, 92, 95, 98, 100};
static int PAS_ErrorCode = PAS_Ok;

static char *PAS_DMABufferEnd;
static volatile int PAS_SoundPlaying;
static char *PAS_CurrentDMABuffer;
static int PAS_IntController1Mask;
static int PAS_IntController2Mask;
static int PAS_TotalDMABufferSize;
static void(interrupt far *PAS_OldInt)(void);
static void (*PAS_CallBack)(void);
static char *PAS_DMABuffer;

#define PAS_SetErrorCode(status) \
    PAS_ErrorCode = (status);

/*---------------------------------------------------------------------
   Function: PAS_ErrorString

   Returns a pointer to the error message associated with an error
   number.  A -1 returns a pointer the current error.
---------------------------------------------------------------------*/

char *PAS_ErrorString(
    int ErrorNumber)

{
    char *ErrorString;

    switch (ErrorNumber)
    {
    case PAS_Warning:
    case PAS_Error:
        ErrorString = PAS_ErrorString(PAS_ErrorCode);
        break;

    case PAS_Ok:
        ErrorString = "Pro AudioSpectrum ok.";
        break;

    case PAS_DriverNotFound:
        ErrorString = "MVSOUND.SYS not loaded.";
        break;

    case PAS_DmaError:
        ErrorString = DMA_ErrorString(DMA_Error);
        break;

    case PAS_InvalidIrq:
        ErrorString = "Invalid Pro AudioSpectrum Irq.";
        break;

    case PAS_NoSoundPlaying:
        ErrorString = "No sound playing.";
        break;

    case PAS_CardNotFound:
        ErrorString = "Could not find Pro AudioSpectrum.";
        break;

    default:
        ErrorString = "Unknown Pro AudioSpectrum error code.";
        break;
    }

    return (ErrorString);
}

/*---------------------------------------------------------------------
   Function: PAS_CheckForDriver

   Checks to see if MVSOUND.SYS is installed.
---------------------------------------------------------------------*/

int PAS_CheckForDriver(
    void)

{
    union REGS regs;
    unsigned result;

    regs.x.ax = MV_CheckForDriver;
    regs.x.bx = 0x3f3f;

    int86(MV_SoundInt, &regs, &regs);

    if (regs.x.ax != MV_CheckForDriver)
    {
        PAS_SetErrorCode(PAS_DriverNotFound);
        return (PAS_Error);
    }

    result = regs.x.bx ^ regs.x.cx ^ regs.x.dx;
    if (result != MV_Signature)
    {
        PAS_SetErrorCode(PAS_DriverNotFound);
        return (PAS_Error);
    }

    return (PAS_Ok);
}

/*---------------------------------------------------------------------
   Function: PAS_GetStateTable

   Returns a pointer to the state table containing hardware state
   information.  The state table is necessary because the Pro Audio-
   Spectrum contains only write-only registers.
---------------------------------------------------------------------*/

MVState *PAS_GetStateTable(
    void)

{
    union REGS regs;
    MVState *ptr;

    regs.x.ax = MV_GetPointerToStateTable;

    int86(MV_SoundInt, &regs, &regs);

    if (regs.x.ax != MV_Signature)
    {
        PAS_SetErrorCode(PAS_DriverNotFound);
        return (NULL);
    }

    ptr = MK_FP(regs.x.dx, regs.x.bx);

    return (ptr);
}

/*---------------------------------------------------------------------
   Function: PAS_GetFunctionTable

   Returns a pointer to the function table containing addresses of
   driver functions.
---------------------------------------------------------------------*/

MVFunc *PAS_GetFunctionTable(
    void)

{
    union REGS regs;
    MVFunc *ptr;

    regs.x.ax = MV_GetPointerToFunctionTable;

    int86(MV_SoundInt, &regs, &regs);

    if (regs.x.ax != MV_Signature)
    {
        PAS_SetErrorCode(PAS_DriverNotFound);
        return (NULL);
    }

    ptr = MK_FP(regs.x.dx, regs.x.bx);

    return (ptr);
}

/*---------------------------------------------------------------------
   Function: PAS_GetCardSettings

   Returns the DMA and the IRQ channels of the sound card.
---------------------------------------------------------------------*/

int PAS_GetCardSettings(
    void)

{
    union REGS regs;
    int status;

    regs.x.ax = MV_GetDmaIrqInt;

    int86(MV_SoundInt, &regs, &regs);

    if (regs.x.ax != MV_Signature)
    {
        PAS_SetErrorCode(PAS_DriverNotFound);
        return (PAS_Error);
    }

    PAS_DMAChannel = regs.x.bx;
    PAS_Irq = regs.x.cx;

    if (PAS_Irq > PAS_MaxIrq)
    {
        PAS_SetErrorCode(PAS_InvalidIrq);
        return (PAS_Error);
    }

    if (PAS_Interrupts[PAS_Irq] == INVALID)
    {
        PAS_SetErrorCode(PAS_InvalidIrq);
        return (PAS_Error);
    }

    status = DMA_VerifyChannel(PAS_DMAChannel);
    if (status == DMA_Error)
    {
        PAS_SetErrorCode(PAS_DmaError);
        return (PAS_Error);
    }

    return (PAS_Ok);
}

/*---------------------------------------------------------------------
   Function: PAS_EnableInterrupt

   Enables the triggering of the sound card interrupt.
---------------------------------------------------------------------*/

void PAS_EnableInterrupt(
    void)

{
    int mask;
    int data;

    DISABLE_INTERRUPTS();

    if (PAS_Irq < 8)
    {
        mask = inp(0x21) & ~(1 << PAS_Irq);
        outp(0x21, mask);
    }
    else
    {
        mask = inp(0xA1) & ~(1 << (PAS_Irq - 8));
        outp(0xA1, mask);

        mask = inp(0x21) & ~(1 << 2);
        outp(0x21, mask);
    }

    // Flush any pending interrupts
    PAS_Write(InterruptStatus, PAS_Read(InterruptStatus) & 0x40);

    // Enable the interrupt on the PAS
    data = PAS_State->intrctlr;
    data |= SampleBufferInterruptFlag;
    PAS_Write(InterruptControl, data);
    PAS_State->intrctlr = data;

    ENABLE_INTERRUPTS();
}

/*---------------------------------------------------------------------
   Function: PAS_DisableInterrupt

   Disables the triggering of the sound card interrupt.
---------------------------------------------------------------------*/

void PAS_DisableInterrupt(
    void)

{
    int mask;
    int data;

    DISABLE_INTERRUPTS();

    // Disable the interrupt on the PAS
    data = PAS_State->intrctlr;
    data &= ~(SampleRateInterruptFlag | SampleBufferInterruptFlag);
    PAS_Write(InterruptControl, data);
    PAS_State->intrctlr = data;

    // Restore interrupt mask
    if (PAS_Irq < 8)
    {
        mask = inp(0x21) & ~(1 << PAS_Irq);
        mask |= PAS_IntController1Mask & (1 << PAS_Irq);
        outp(0x21, mask);
    }
    else
    {
        mask = inp(0x21) & ~(1 << 2);
        mask |= PAS_IntController1Mask & (1 << 2);
        outp(0x21, mask);

        mask = inp(0xA1) & ~(1 << (PAS_Irq - 8));
        mask |= PAS_IntController2Mask & (1 << (PAS_Irq - 8));
        outp(0xA1, mask);
    }

    ENABLE_INTERRUPTS();
}

/*---------------------------------------------------------------------
   Function: PAS_ServiceInterrupt

   Handles interrupt generated by sound card at the end of a voice
   transfer.  Calls the user supplied callback function.
---------------------------------------------------------------------*/

void interrupt far PAS_ServiceInterrupt(
    void)

{
    int irqstatus;

    irqstatus = PAS_Read(InterruptStatus);
    if ((irqstatus & SampleBufferInterruptFlag) == 0)
    {
        PAS_OldInt();
        return;
    }

    // Clear the interrupt
    irqstatus &= ~SampleBufferInterruptFlag;
    PAS_Write(InterruptStatus, irqstatus);

    // send EOI to Interrupt Controller
    if (PAS_Irq > 7)
    {
        outp(0xA0, 0x20);
    }
    outp(0x20, 0x20);

    // Keep track of current buffer
    PAS_CurrentDMABuffer += PAS_TransferLength;
    if (PAS_CurrentDMABuffer >= PAS_DMABufferEnd)
    {
        PAS_CurrentDMABuffer = PAS_DMABuffer;
    }

    // Call the caller's callback function
    if (PAS_CallBack != NULL)
    {
        PAS_CallBack();
    }
}

/*---------------------------------------------------------------------
   Function: PAS_Write

   Writes a byte of data to the sound card.
---------------------------------------------------------------------*/

void PAS_Write(
    int Register,
    int Data)

{
    int port;

    port = Register ^ PAS_TranslateCode;
    outp(port, Data);
}

/*---------------------------------------------------------------------
   Function: PAS_Read

   Reads a byte of data from the sound card.
---------------------------------------------------------------------*/

int PAS_Read(
    int Register)

{
    int port;
    int data;

    port = Register ^ PAS_TranslateCode;
    data = inp(port);
    return (data);
}

/*---------------------------------------------------------------------
   Function: PAS_SetSampleRateTimer

   Programs the Sample Rate Timer.
---------------------------------------------------------------------*/

void PAS_SetSampleRateTimer(
    void)

{
    int LoByte;
    int HiByte;
    int data;

    DISABLE_INTERRUPTS();

    // Disable the Sample Rate Timer
    data = PAS_State->audiofilt;
    data &= ~SampleRateTimerGateFlag;
    PAS_Write(AudioFilterControl, data);
    PAS_State->audiofilt = data;

    // Select the Sample Rate Timer
    data = SelectSampleRateTimer;
    PAS_Write(LocalTimerControl, data);
    PAS_State->tmrctlr = data;

    LoByte = lobyte(PAS_TimeInterval);
    HiByte = hibyte(PAS_TimeInterval);

    // Program the Sample Rate Timer
    PAS_Write(SampleRateTimer, LoByte);
    PAS_Write(SampleRateTimer, HiByte);
    PAS_State->samplerate = PAS_TimeInterval;

    ENABLE_INTERRUPTS();
}

/*---------------------------------------------------------------------
   Function: PAS_SetSampleBufferCount

   Programs the Sample Buffer Count.
---------------------------------------------------------------------*/

void PAS_SetSampleBufferCount(
    void)

{
    int LoByte;
    int HiByte;
    int count;
    int data;

    DISABLE_INTERRUPTS();

    // Disable the Sample Buffer Count
    data = PAS_State->audiofilt;
    data &= ~SampleBufferCountGateFlag;
    PAS_Write(AudioFilterControl, data);
    PAS_State->audiofilt = data;

    // Select the Sample Buffer Count
    data = SelectSampleBufferCount;
    PAS_Write(LocalTimerControl, data);
    PAS_State->tmrctlr = data;

    count = PAS_TransferLength;

    // Check if we're using a 16-bit DMA channel
    if (PAS_DMAChannel > 3)
    {
        count >>= 1;
    }

    LoByte = lobyte(count);
    HiByte = hibyte(count);

    // Program the Sample Buffer Count
    PAS_Write(SampleBufferCount, LoByte);
    PAS_Write(SampleBufferCount, HiByte);
    PAS_State->samplecnt = count;

    ENABLE_INTERRUPTS();
}

/*---------------------------------------------------------------------
   Function: PAS_SetPlaybackRate

   Sets the rate at which the digitized sound will be played in
   hertz.
---------------------------------------------------------------------*/

void PAS_SetPlaybackRate(
    unsigned rate)

{
    long samplerate;
    if (rate < PAS_MinSamplingRate)
    {
        rate = PAS_MinSamplingRate;
    }

    if (rate > PAS_MaxSamplingRate)
    {
        rate = PAS_MaxSamplingRate;
    }

    PAS_TimeInterval = (unsigned)CalcTimeInterval(rate);
    if (PAS_MixMode & STEREO)
    {
        PAS_TimeInterval /= 2;
    }

    // Keep track of what the actual rate is
    samplerate = CalcSamplingRate(PAS_TimeInterval);
    if (PAS_MixMode & STEREO)
    {
        samplerate /= 2;
    }
    PAS_SampleRate = samplerate;
}

/*---------------------------------------------------------------------
   Function: PAS_GetPlaybackRate

   Returns the rate at which the digitized sound will be played in
   hertz.
---------------------------------------------------------------------*/

unsigned PAS_GetPlaybackRate(
    void)

{
    return (PAS_SampleRate);
}

/*---------------------------------------------------------------------
   Function: PAS_SetMixMode

   Sets the sound card to play samples in mono or stereo.
---------------------------------------------------------------------*/

int PAS_SetMixMode(
    int mode)

{
    mode &= PAS_MaxMixMode;

    // Check board revision.  Revision # 0 can't play 16-bit data.
    if ((PAS_State->intrctlr & 0xe0) == 0)
    {
        // Force the mode to 8-bit data.
        mode &= ~SIXTEEN_BIT;
    }

    PAS_MixMode = mode;

    PAS_SetPlaybackRate(PAS_SampleRate);

    return (mode);
}

/*---------------------------------------------------------------------
   Function: PAS_StopPlayback

   Ends the DMA transfer of digitized sound to the sound card.
---------------------------------------------------------------------*/

void PAS_StopPlayback(
    void)

{
    int data;

    // Don't allow anymore interrupts
    PAS_DisableInterrupt();

    // Stop the transfer of digital data
    data = PAS_State->crosschannel;
    data &= PAS_PCMStopMask;
    PAS_Write(CrossChannelControl, data);
    PAS_State->crosschannel = data;

    PAS_SoundPlaying = FALSE;

    PAS_DMABuffer = NULL;
}

/*---------------------------------------------------------------------
   Function: PAS_SetupDMABuffer

   Programs the DMAC for sound transfer.
---------------------------------------------------------------------*/

int PAS_SetupDMABuffer(
    char *BufferPtr,
    int BufferSize)

{
    int DmaStatus;
    int data;

    // Enable PAS Dma
    data = PAS_State->crosschannel;
    data |= PAS_DMAEnable;
    PAS_Write(CrossChannelControl, data);
    PAS_State->crosschannel = data;

    DmaStatus = DMA_SetupTransfer(PAS_DMAChannel, BufferPtr, BufferSize);
    if (DmaStatus == DMA_Error)
    {
        PAS_SetErrorCode(PAS_DmaError);
        return (PAS_Error);
    }

    PAS_DMABuffer = BufferPtr;
    PAS_CurrentDMABuffer = BufferPtr;
    PAS_TotalDMABufferSize = BufferSize;
    PAS_DMABufferEnd = BufferPtr + BufferSize;

    return (PAS_Ok);
}

/*---------------------------------------------------------------------
   Function: PAS_GetCurrentPos

   Returns the offset within the current sound being played.
---------------------------------------------------------------------*/

int PAS_GetCurrentPos(
    void)

{
    char *CurrentAddr;
    int offset;

    if (!PAS_SoundPlaying)
    {
        PAS_SetErrorCode(PAS_NoSoundPlaying);
        return (PAS_Error);
    }

    CurrentAddr = DMA_GetCurrentPos(PAS_DMAChannel);
    if (CurrentAddr == NULL)
    {
        PAS_SetErrorCode(PAS_DmaError);
        return (PAS_Error);
    }

    offset = (int)(((char huge *)CurrentAddr) -
                   ((char huge *)PAS_CurrentDMABuffer));

    if (PAS_MixMode & SIXTEEN_BIT)
    {
        offset >>= 1;
    }

    if (PAS_MixMode & STEREO)
    {
        offset >>= 1;
    }

    return (offset);
}

/*---------------------------------------------------------------------
   Function: PAS_GetFilterSetting

   Returns the bit settings for the appropriate filter level.
---------------------------------------------------------------------*/

int PAS_GetFilterSetting(
    int rate)

{
    /* CD Quality 17897hz */
    if ((unsigned long)rate > (unsigned long)17897L * 2)
    {
        /* 00001b 20hz to 17.8khz */
        return (0x01);
    }

    /* Cassette Quality 15090hz */
    if ((unsigned long)rate > (unsigned long)15909L * 2)
    {
        /* 00010b 20hz to 15.9khz */
        return (0x02);
    }

    /* FM Radio Quality 11931hz */
    if ((unsigned long)rate > (unsigned long)11931L * 2)
    {
        /* 01001b 20hz to 11.9khz */
        return (0x09);
    }

    /* AM Radio Quality  8948hz */
    if ((unsigned long)rate > (unsigned long)8948L * 2)
    {
        /* 10001b 20hz to 8.9khz */
        return (0x11);
    }

    /* Telphone Quality  5965hz */
    if ((unsigned long)rate > (unsigned long)5965L * 2)
    {
        /* 00100b 20hz to 5.9khz */
        return (0x19);
    }

    /* Male voice quality 2982hz */
    /* 111001b 20hz to 2.9khz */
    return (0x04);
}

/*---------------------------------------------------------------------
   Function: PAS_BeginTransfer

   Starts playback of digitized sound on the sound card.
---------------------------------------------------------------------*/

void PAS_BeginTransfer(
    void)

{
    int data;

    PAS_SetSampleRateTimer();

    PAS_SetSampleBufferCount();

    PAS_EnableInterrupt();

    if (PAS_State->intrctlr & 0xe0)
    {
        // Get sample size configuration
        data = PAS_Read(SampleSizeConfiguration);

        // Check board revision.  Revision # 0 can't play 16-bit data.
        data &= PAS_SampleSizeMask;

        // set sample size bit
        if (PAS_MixMode & SIXTEEN_BIT)
        {
            data |= PAS_16BitSampleFlag;
        }

        // Set sample size configuration
        PAS_Write(SampleSizeConfiguration, data);
    }

    // Get Cross channel setting
    data = PAS_State->crosschannel;
    data &= PAS_ChannelConnectMask;

    data |= PAS_PCMStartDAC;
    data |= 3; // TODO

    // set stereo mode bit
    if (!(PAS_MixMode & STEREO))
    {
        data |= PAS_StereoFlag;
    }

    PAS_Write(CrossChannelControl, data);
    PAS_State->crosschannel = data;

    // Get the filter appropriate filter setting
    data = PAS_GetFilterSetting(PAS_SampleRate);

    data |= PAS_PCMStartADC;
    data |= PAS_AudioMuteFlag;

    PAS_Write(AudioFilterControl, data);
    PAS_State->audiofilt = data;
}

/*---------------------------------------------------------------------
   Function: PAS_BeginBufferedPlayback

   Begins multibuffered playback of digitized sound on the sound card.
---------------------------------------------------------------------*/

int PAS_BeginBufferedPlayback(
    char *BufferStart,
    int BufferSize,
    int NumDivisions,
    unsigned SampleRate,
    int MixMode,
    void (*CallBackFunc)(void))

{
    int DmaStatus;

    PAS_StopPlayback();

    PAS_SetMixMode(MixMode);
    PAS_SetPlaybackRate(SampleRate);

    PAS_TransferLength = BufferSize / NumDivisions;
    PAS_SetCallBack(CallBackFunc);

    DmaStatus = PAS_SetupDMABuffer(BufferStart, BufferSize);
    if (DmaStatus == PAS_Error)
    {
        return (PAS_Error);
    }

    PAS_BeginTransfer();

    return (PAS_Ok);
}

/*---------------------------------------------------------------------
   Function: PAS_CallMVFunction

   Performs a call to a real mode function.
---------------------------------------------------------------------*/

int PAS_CallMVFunction(
    unsigned long function,
    int bx,
    int cx,
    int dx)

{
    _BX = bx;
    _CX = cx;
    _DX = dx;
    ((void (*)(void))function)();
    return _BX;
}

/*---------------------------------------------------------------------
   Function: PAS_SetPCMVolume

   Sets the volume of digitized sound playback.
---------------------------------------------------------------------*/

void PAS_SetPCMVolume(
    int volume)

{
    int status;
    volume = max(0, volume);
    volume = min(volume, 255);

    volume = PAS_VolumeTable[volume >> 4];

    PAS_CallMVFunction(PAS_Func->SetMixer, volume,
                       OUTPUTMIXER, L_PCM);

    PAS_CallMVFunction(PAS_Func->SetMixer, volume,
                       OUTPUTMIXER, R_PCM);
}

/*---------------------------------------------------------------------
   Function: PAS_GetPCMVolume

   Returns the current volume of digitized sound playback.
---------------------------------------------------------------------*/

int PAS_GetPCMVolume(
    void)

{
    int leftvolume;
    int rightvolume;
    int totalvolume;

    if (PAS_Func == NULL)
    {
        return (PAS_Error & 0xff);
    }

    leftvolume = PAS_CallMVFunction(PAS_Func->GetMixer, 0,
                                    OUTPUTMIXER, L_PCM);
    rightvolume = PAS_CallMVFunction(PAS_Func->GetMixer, 0,
                                     OUTPUTMIXER, R_PCM);

    totalvolume = (rightvolume + leftvolume) / 2;
    totalvolume *= 255;
    totalvolume /= 100;
    return (totalvolume);
}

/*---------------------------------------------------------------------
   Function: PAS_SetFMVolume

   Sets the volume of FM sound playback.
---------------------------------------------------------------------*/

void PAS_SetFMVolume(
    int volume)

{
    volume = max(0, volume);
    volume = min(volume, 255);

    volume = PAS_VolumeTable[volume >> 4];
    if (PAS_Func)
    {
        PAS_CallMVFunction(PAS_Func->SetMixer, volume, OUTPUTMIXER, L_FM);
        PAS_CallMVFunction(PAS_Func->SetMixer, volume, OUTPUTMIXER, R_FM);
    }
}

/*---------------------------------------------------------------------
   Function: PAS_GetFMVolume

   Returns the current volume of FM sound playback.
---------------------------------------------------------------------*/

int PAS_GetFMVolume(
    void)

{
    int leftvolume;
    int rightvolume;
    int totalvolume;

    if (PAS_Func == NULL)
    {
        return (255);
    }

    leftvolume = PAS_CallMVFunction(PAS_Func->GetMixer, 0,
                                    OUTPUTMIXER, L_FM);
    rightvolume = PAS_CallMVFunction(PAS_Func->GetMixer, 0,
                                     OUTPUTMIXER, R_FM);

    totalvolume = (rightvolume + leftvolume) / 2;
    totalvolume *= 255;
    totalvolume /= 100;
    totalvolume = min(255, totalvolume);

    return (totalvolume);
}

/*---------------------------------------------------------------------
   Function: PAS_GetCardInfo

   Returns the maximum number of bits that can represent a sample
   (8 or 16) and the number of channels (1 for mono, 2 for stereo).
---------------------------------------------------------------------*/

int PAS_GetCardInfo(
    int *MaxSampleBits,
    int *MaxChannels)

{
    int status;

    if (PAS_State == NULL)
    {
        status = PAS_CheckForDriver();
        if (status != PAS_Ok)
        {
            return (status);
        }

        PAS_State = PAS_GetStateTable();
        if (PAS_State == NULL)
        {
            return (PAS_Error);
        }
    }

    *MaxChannels = 2;

    // Check board revision.  Revision # 0 can't play 16-bit data.
    if ((PAS_State->intrctlr & 0xe0) == 0)
    {
        *MaxSampleBits = 8;
    }
    else
    {
        *MaxSampleBits = 16;
    }

    return (PAS_Ok);
}

/*---------------------------------------------------------------------
   Function: PAS_SetCallBack

   Specifies the user function to call at the end of a sound transfer.
---------------------------------------------------------------------*/

void PAS_SetCallBack(
    void (*func)(void))

{
    PAS_CallBack = func;
}

int PAS_TestAddress(int address)
{
    asm mov ax, [address];
    asm mov dx, 0b8bh;
    asm xor dx, ax;
    asm in al, dx;
    asm cmp al, 0ffh;
    asm je TestExit;
    asm mov ah, al;
    asm xor al, 0e0h;
    asm out dx, al;
    asm jmp TestDelay1;
TestDelay1:
    asm jmp TestDelay2;
TestDelay2:
    asm in al, dx;
    asm xchg al, ah;
    asm out dx, al;
    asm sub al, ah;
TestExit:
    // asm and ax, 0ffh;
    return _AX & 0xff;
}

/*---------------------------------------------------------------------
   Function: PAS_FindCard

   Auto-detects the port the Pro AudioSpectrum is set for.
---------------------------------------------------------------------*/

int PAS_FindCard(
    void)

{
    int status;

    status = PAS_TestAddress(DEFAULT_BASE);
    if (status == 0)
    {
        PAS_TranslateCode = DEFAULT_BASE;
        return (PAS_Ok);
    }

    status = PAS_TestAddress(ALT_BASE_1);
    if (status == 0)
    {
        PAS_TranslateCode = ALT_BASE_1;
        return (PAS_Ok);
    }

    status = PAS_TestAddress(ALT_BASE_2);
    if (status == 0)
    {
        PAS_TranslateCode = ALT_BASE_2;
        return (PAS_Ok);
    }

    status = PAS_TestAddress(ALT_BASE_3);
    if (status == 0)
    {
        PAS_TranslateCode = ALT_BASE_3;
        return (PAS_Ok);
    }

    PAS_SetErrorCode(PAS_CardNotFound);
    return (PAS_Error);
}

/*---------------------------------------------------------------------
   Function: PAS_SaveMusicVolume

   Saves the user's FM mixer settings.
---------------------------------------------------------------------*/

int PAS_SaveMusicVolume(
    void)

{
    int status;
    int data;

    if (!PAS_Installed)
    {
        status = PAS_CheckForDriver();
        if (status != PAS_Ok)
        {
            return (status);
        }

        PAS_State = PAS_GetStateTable();
        if (PAS_State == NULL)
        {
            return (PAS_Error);
        }

        PAS_Func = PAS_GetFunctionTable();
        if (PAS_Func == NULL)
        {
            return (PAS_Error);
        }

        status = PAS_GetCardSettings();
        if (status != PAS_Ok)
        {
            return (status);
        }

        status = PAS_FindCard();
        if (status != PAS_Ok)
        {
            return (status);
        }

        // Enable PAS Sound
        data = PAS_State->audiofilt;
        data |= PAS_AudioMuteFlag;

        PAS_Write(AudioFilterControl, data);
        PAS_State->audiofilt = data;
    }

    PAS_OriginalFMLeftVolume = PAS_CallMVFunction(PAS_Func->GetMixer,
                                                  0, OUTPUTMIXER, L_FM);

    PAS_OriginalFMRightVolume = PAS_CallMVFunction(PAS_Func->GetMixer,
                                                   0, OUTPUTMIXER, R_FM);

    return (PAS_Ok);
}

/*---------------------------------------------------------------------
   Function: PAS_RestoreMusicVolume

   Restores the user's FM mixer settings.
---------------------------------------------------------------------*/

void PAS_RestoreMusicVolume(
    void)

{
    if (PAS_Func)
    {
        PAS_CallMVFunction(PAS_Func->SetMixer, PAS_OriginalFMLeftVolume,
                           OUTPUTMIXER, L_FM);
        PAS_CallMVFunction(PAS_Func->SetMixer, PAS_OriginalFMRightVolume,
                           OUTPUTMIXER, R_FM);
    }
}

/*---------------------------------------------------------------------
   Function: PAS_Init

   Initializes the sound card and prepares the module to play
   digitized sounds.
---------------------------------------------------------------------*/

int PAS_Init(
    void)

{
    int Interrupt;
    int status;
    int data;

    if (PAS_Installed)
    {
        PAS_Shutdown();
    }

    PAS_IntController1Mask = inp(0x21);
    PAS_IntController2Mask = inp(0xA1);

    status = PAS_CheckForDriver();
    if (status != PAS_Ok)
    {
        return (status);
    }

    PAS_State = PAS_GetStateTable();
    if (PAS_State == NULL)
    {
        return (PAS_Error);
    }

    PAS_Func = PAS_GetFunctionTable();
    if (PAS_Func == NULL)
    {
        return (PAS_Error);
    }

    status = PAS_GetCardSettings();
    if (status != PAS_Ok)
    {
        return (status);
    }

    status = PAS_FindCard();
    if (status != PAS_Ok)
    {
        return (status);
    }

    PAS_OriginalPCMLeftVolume = PAS_CallMVFunction(PAS_Func->GetMixer, 0,
                                                   OUTPUTMIXER, L_PCM);
    PAS_OriginalPCMRightVolume = PAS_CallMVFunction(PAS_Func->GetMixer, 0,
                                                    OUTPUTMIXER, R_PCM);

    PAS_SoundPlaying = FALSE;

    PAS_SetCallBack(NULL);

    PAS_DMABuffer = NULL;

    // Enable PAS Sound
    data = PAS_State->audiofilt;
    data |= PAS_AudioMuteFlag;

    PAS_Write(AudioFilterControl, data);
    PAS_State->audiofilt = data;

    PAS_SetPlaybackRate(PAS_DefaultSampleRate);
    PAS_SetMixMode(PAS_DefaultMixMode);

    // Install our interrupt handler
    Interrupt = PAS_Interrupts[PAS_Irq];
    PAS_OldInt = getvect(Interrupt);
    setvect(Interrupt, PAS_ServiceInterrupt);

    PAS_Installed = TRUE;

    PAS_SetErrorCode(PAS_Ok);
    return (PAS_Ok);
}

/*---------------------------------------------------------------------
   Function: PAS_Shutdown

   Ends transfer of sound data to the sound card and restores the
   system resources used by the card.
---------------------------------------------------------------------*/

void PAS_Shutdown(
    void)

{
    int Interrupt;

    if (PAS_Installed)
    {
        // Halt the DMA transfer
        PAS_StopPlayback();

        // Restore the original interrupt
        Interrupt = PAS_Interrupts[PAS_Irq];
        setvect(Interrupt, PAS_OldInt);

        PAS_SoundPlaying = FALSE;

        PAS_DMABuffer = NULL;

        PAS_SetCallBack(NULL);

        PAS_CallMVFunction(PAS_Func->SetMixer, PAS_OriginalPCMLeftVolume,
                           OUTPUTMIXER, L_PCM);
        PAS_CallMVFunction(PAS_Func->SetMixer, PAS_OriginalPCMRightVolume,
                           OUTPUTMIXER, R_PCM);

        PAS_Installed = FALSE;
    }
}