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
   module: TASK_MAN.C

   author: James R. Dose
   date:   July 25, 1994

   Low level timer task scheduler.

   (c) Copyright 1994 James R. Dose.  All Rights Reserved.
**********************************************************************/

#define TRUE (1 == 1)
#define FALSE (!TRUE)

#include <stdlib.h>
#include <dos.h>
#include <conio.h>
#include <string.h>
#include "interrup.h"
#include "task_man.h"
#include "ll_man.h"

typedef struct
{
    task *start;
    task *end;
} tasklist;

/*---------------------------------------------------------------------
   Global variables
---------------------------------------------------------------------*/

static volatile long TaskServiceRate = 0x10000L;
static volatile long TaskServiceCount = 0;
static char byte_2F426 = FALSE;
static char byte_2F427 = FALSE;
static char TS_Installed = FALSE;
static char byte_2F429 = FALSE;
volatile char TS_InInterrupt = FALSE;

static void(interrupt far *OldInt8)(void);
static tasklist TaskList;

/*---------------------------------------------------------------------
   Function prototypes
---------------------------------------------------------------------*/

static void TS_FreeTaskList(tasklist *head);
static void TS_SetClockSpeed(long speed);
static int TS_SetTimer(int TickBase);
static void interrupt far TS_ServiceSchedule(void);
static void TS_AddTask(task *ptr);
static void TS_Startup(void);
static void sub_24E08(void);
static void sub_24EB7(void);
static void sub_24FE8(tasklist *head, task *node);

/*---------------------------------------------------------------------
   Function: TS_FreeTaskList

   Terminates all tasks and releases any memory used for control
   structures.
---------------------------------------------------------------------*/

static void TS_FreeTaskList(
    tasklist *head)

{
    task *node;
    task *next;

    node = head->start;
    while (node != NULL)
    {
        next = node->next;
        farfree(node);
        node = next;
    }

    head->start = NULL;
    head->end = NULL;
}

/*---------------------------------------------------------------------
   Function: TS_SetClockSpeed

   Sets the rate of the 8253 timer.
---------------------------------------------------------------------*/

static void TS_SetClockSpeed(
    long speed)

{
    DISABLE_INTERRUPTS();
    if ((speed > 0) && (speed < 0x10000L))
    {
        TaskServiceRate = speed;
    }
    else
    {
        TaskServiceRate = 0x10000L;
    }

    outp(0x43, 0x36);
    outp(0x40, TaskServiceRate);
    outp(0x40, TaskServiceRate >> 8);
    ENABLE_INTERRUPTS();
}

/*---------------------------------------------------------------------
   Function: TS_SetTimer

   Calculates the rate at which a task will occur and sets the clock
   speed if necessary.
---------------------------------------------------------------------*/

static int TS_SetTimer(
    int TickBase)

{
    int speed;

    speed = 1192030L / TickBase;
    if (speed < TaskServiceRate)
    {
        TS_SetClockSpeed(speed);
    }

    return (speed);
}

/*---------------------------------------------------------------------
   Function: TS_ServiceSchedule

   Interrupt service routine
---------------------------------------------------------------------*/

static void interrupt far TS_ServiceSchedule(
    void)

{
    TS_InInterrupt = TRUE;
    sub_24E08();
    sub_24EB7();

    TaskServiceCount += TaskServiceRate;
    if (TaskServiceCount > 0xffffL)
    {
        TaskServiceCount &= 0xffff;
        OldInt8();
    }
    else
    {
        outp(0x20, 0x20);

        TS_InInterrupt = FALSE;
    }
}

static void sub_24E08()
{
    task *ptr;
    task *next;

    if (byte_2F426 == FALSE)
    {
        return;
    }

    ptr = TaskList.start;
    while (ptr != NULL)
    {
        next = ptr->next;

        if (ptr->priority > 0 || !byte_2F427)
        {
            ptr->count += TaskServiceRate;
            if (ptr->count > ptr->rate - 1)
            {
                ptr->count -= ptr->rate;
                ptr->TaskService(ptr);
                if (!byte_2F426)
                {
                    break;
                }
            }
        }
        ptr = next;
    }
}

static void sub_24EB7()
{
    task *ptr;
    task *next;
    long speed;

    if (byte_2F427 == FALSE)
    {
        return;
    }

    speed = 0x10000L;
    ptr = TaskList.start;
    while (ptr != NULL)
    {
        next = ptr->next;

        if (ptr->priority <= 0)
        {
            if (ptr->next == NULL)
            {
                TaskList.end = ptr->prev;
            }
            else
            {
                next->prev = ptr->prev;
            }

            if (ptr->prev == NULL)
            {
                TaskList.start = next;
            }
            else
            {
                ptr->prev->next = next;
            }
            ptr->priority = 0;
            ptr->next = NULL;
            ptr->prev = NULL;
            farfree(ptr);
        }
        else
        {
            if (ptr->rate < speed)
            {
                speed = ptr->rate;
            }
        }
        ptr = next;
    }
    byte_2F427 = FALSE;
    if (TaskServiceRate != speed)
    {
        TS_SetClockSpeed(speed);
    }
}

static void sub_24FE8(tasklist *head, task *node)
{
    task *ptr;

    if (head->start == NULL)
    {
        LL_AddToHead(task, head, node);
        return;
    }

    if (node->priority >= head->end->priority)
    {
        LL_AddToTail(task, head, node);
        return;
    }

    for (ptr = head->start; node->priority >= ptr->priority; ptr = ptr->next)
        ;

    if (ptr->prev == NULL)
    {
        LL_AddToHead(task, head, node);
        return;
    }

    node->prev = ptr->prev;
    node->next = ptr;
    ptr->prev = node;
    node->prev->next = node;
}

/*---------------------------------------------------------------------
   Function: TS_Startup

   Sets up the task service routine.
---------------------------------------------------------------------*/

static void TS_Startup(
    void)

{

    TaskList.start = NULL;
    TaskList.end = NULL;
    byte_2F426 = FALSE;
    byte_2F427 = FALSE;

    TaskServiceRate = 0x10000L;
    TaskServiceCount = 0;

    OldInt8 = getvect(0x08);
    setvect(0x08, TS_ServiceSchedule);

    TS_Installed = TRUE;

    if (byte_2F429 == FALSE)
    {
        atexit(TS_Shutdown);
        byte_2F429 = TRUE;
    }
}

/*---------------------------------------------------------------------
   Function: TS_Shutdown

   Ends processing of all tasks.
---------------------------------------------------------------------*/

void TS_Shutdown(
    void)

{
    if (TS_Installed)
    {
        TS_SetClockSpeed(0);
        setvect(0x08, OldInt8);
        TS_FreeTaskList(&TaskList);
        byte_2F426 = FALSE;
        TS_Installed = FALSE;
    }
}

/*---------------------------------------------------------------------
   Function: TS_ScheduleTask

   Schedules a new task for processing.
---------------------------------------------------------------------*/

task *TS_ScheduleTask(
    void (*Function)(task *),
    int rate,
    int priority,
    void *data)

{
    task *ptr = NULL;
    if (priority > 0)
    {
        ptr = malloc(sizeof(task));
        if (ptr != NULL)
        {
            if (!TS_Installed)
            {
                TS_Startup();
            }

            ptr->TaskService = Function;
            ptr->data = data;
            ptr->rate = TS_SetTimer(rate);
            ptr->count = 0;
            ptr->priority = priority;

            TS_AddTask(ptr);
        }
    }
    return (ptr);
}

/*---------------------------------------------------------------------
   Function: TS_AddTask

   Adds a new task to our list of tasks.
---------------------------------------------------------------------*/

static void TS_AddTask(
    task *node)

{
    sub_24FE8(&TaskList, node);
}

/*---------------------------------------------------------------------
   Function: TS_Terminate

   Ends processing of a specific task.
---------------------------------------------------------------------*/

int TS_Terminate(
    task *NodeToRemove)

{
    if (TS_InInterrupt)
    {
        NodeToRemove->priority = 0;
        byte_2F427 = TRUE;
        return TRUE;
    }

    DISABLE_INTERRUPTS();
    NodeToRemove->priority = -1;
    byte_2F427 = TRUE;
    sub_24EB7();
    ENABLE_INTERRUPTS();

    if (NodeToRemove->priority != -1)
        return TRUE;
    return FALSE;
}

/*---------------------------------------------------------------------
   Function: TS_Dispatch

   Begins processing of all inactive tasks.
---------------------------------------------------------------------*/

void TS_Dispatch(
    void)

{
    byte_2F426 = TRUE;
}

void TS_Stop(
    void)

{
    byte_2F426 = FALSE;
}

/*---------------------------------------------------------------------
   Function: TS_SetTaskRate

   Sets the rate at which the specified task is serviced.
---------------------------------------------------------------------*/

void TS_SetTaskRate(
    task *Task,
    int rate)

{
    task *ptr;
    long MaxServiceRate;

    Task->rate = TS_SetTimer(rate);
    MaxServiceRate = 0x10000L;

    ptr = TaskList.start;
    while (ptr != 0)
    {
        if (ptr->rate < MaxServiceRate)
        {
            MaxServiceRate = ptr->rate;
        }

        ptr = ptr->next;
    }

    if (TaskServiceRate != MaxServiceRate)
    {
        TS_SetClockSpeed(MaxServiceRate);
    }
}
