//***************************************************************************
//***                 PAR - Protokoll Implementierung                     ***
//***                         Project: WotanPAR                           ***
//***************************************************************************
//*                     Written (w) 1996 by RabbiT                          *
//*-------------------------------------------------------------------------*
//*                    Version 1.00.001, 21.12.1996                         *
//***************************************************************************
//*  Version 1.00.001, 21.12.96:                                            *
//*   - Prototyp-Version                                                    *
//***************************************************************************

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/sched.h>
#include <linux/serial_reg.h>
#include <linux/config.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/sockios.h>
#include <linux/if_ether.h>
#include <linux/if.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <asm/bitops.h>
#include <asm/byteorder.h>

#include "L1RingBuffer.h"
#include "L2RingBuffer.h"
#include "RABDef.h"
#include "WPARProtocol.h"
#include "SerialPort.h"

#define SERIAL_WAIT_TIME  80   // 80ms

static char          szRabbiT[] = "\xFFRABBIT?\xFF";
static int           iReceivePos[2];
static int           iReceiveMsg[2];

static BOOL          fStationSearchInit[2];
static BOOL          fStationSearch[2];
static int           iStationFound[2];
static unsigned long ulTime[2];

static void SerialPort_readTimer1 ( void )
{ 
  BYTE bChar;

  if ( sRabbiTBusy[0].iProtocol == RABBIT_PROTOCOL_CHAT )
  {
    if ( fStationSearch[0] )
    {
      if ( jiffies > ulTime[0] )
      {
        iStationFound[0] = 1;   // Not found
        fStationSearch[0] = FALSE;
      }
      else
      {
        if ( L1RingBuffer_takeChar(&bChar, L2RINGBUFFER_READ, TRUE) == 0 )
        {
          if ( bChar == 0xFF )          
          {
            if ( iReceivePos[0] == strlen(szRabbit) )
            {  // Signet wieder gefunden
              if ( iReceiveMsg[0] == 2 )
              {  // Message: Empf"anger auch auf CHAT eingestellt
                fStationSearch[0] = FALSE;
                iStationFound[0] = 2;
              }
              else
              {  // Message: Empf"anger nicht auf CHAT eingestellt
                fStationSearch[0] = FALSE;
                iStationFound[0] = 4;
              }
            }
            else
            {  // erstes 0xFF gefunden, weiter gehts
              iReveicePos[0] = 1;
            }
          }
          else
          {
            if ( bChar == szRabbiT[iReceivePos[0]] )
            {  // ein weiteres richtiges Zeichen empfangen
              iReceivePos[0]++;
            }
            else
            {  // wieder von vorne beginnen
              iReceivePos[0] = 0;
            }
          }
        }
      }
    }
    else
    {
      while ( L2RingBuffer_charInBuffer(L2RINGBUFFER_READ) < L1RINGBUFFER_CHAR_MAX-1 )
      {
        if ( L1RingBuffer_takeChar(&bChar, L2RINGBUFFER_READ, TRUE) != 0 )
          break;
        L2RingBuffer_insertChar(bChar, L1RINGBUFFER_READ, TRUE);
      }
    }
  }
  
  if ( sRabbiTBusy[0].iProtocol == RABBIT_PROTOCOL_WPAR )
  {  
    printk ( "WPAR-Protocol not Implemented.\n" );
  }

  // Timer wieder aktivieren f"ur n"achsten Durchlauf
  timer_table[RABBIT_TIMER_READ1].expires = jiffies + SERIAL_WAIT_TIME;
  timer_active |= 1 << RABBIT_TIMER_READ1;
} 

static void SerialPort_writeTimer1 ( void )
{ 
  BYTE bChar;
  BOOL fWriteChar = FALSE;
//  printk ( "Serial-WriteTimer1 aufgerufen: %ld\n", jiffies );

  if ( sRabbiTBusy[0].iProtocol == RABBIT_PROTOCOL_CHAT )
  {
    if ( fStationSearchInit[0] )
    {
      int i;
      fStationSearch[0]     = TRUE;
      fStationSearchInit[0] = FALSE;
      fWriteChar            = TRUE;
      ulTime[0]             = jiffies+400;   // 4 sec bis Timeout
      // Sendestruktur: "0xFF R A B B I T 1 0xFF"
      //                                  ^--- CHAT-Moduls request
      for ( i = 0; i < strlen(szRabbiT); i++ )      
      {
        if ( szRabbiT[i] == '?' )
          L1RingBuffer_insertChar ( 1, L1RINGBUFFER_WRITE, TRUE );
        else
          L1RingBuffer_insertChar ( szRabbiT[i], L1RINGBUFFER_WRITE, TRUE );
      }
    }
    else
    { 
      while ( L1RingBuffer_charInBuffer(L2RINGBUFFER_WRITE) < L1RINGBUFFER_CHAR_MAX-1 )
      {
        if ( L2RingBuffer_takeChar(&bChar, L2RINGBUFFER_WRITE, TRUE) != 0 )
          break;
        fWriteChar = TRUE;
        L1RingBuffer_insertChar(bChar, L1RINGBUFFER_WRITE, TRUE);
      }
    }
    if ( fWriteChar )
      SerialPort_writeIntOn ( 0 );    
  }
  
  if ( sRabbiTBusy[0].iProtocol == RABBIT_PROTOCOL_WPAR )
  {  
    printk ( "WPAR-Protocol not Implemented.\n" );
  }
  
  // Timer wieder aktivieren f"ur n"achsten Durchlauf
  timer_table[RABBIT_TIMER_WRITE1].expires = jiffies + SERIAL_WAIT_TIME;
  timer_active |= 1 << RABBIT_TIMER_WRITE1;
} 

static void SerialPort_readTimer2 ( void )
{ 
  BYTE bChar;

  if ( sRabbiTBusy[1].iProtocol == RABBIT_PROTOCOL_CHAT )
  {
    while ( L2RingBuffer_charInBuffer(2+L2RINGBUFFER_READ) < L1RINGBUFFER_CHAR_MAX-1 )
    {
      if ( L1RingBuffer_takeChar(&bChar, 2+L2RINGBUFFER_READ, TRUE) != 0 )
        break;
      L2RingBuffer_insertChar(bChar, 2+L1RINGBUFFER_READ, TRUE);
    }
  }

  // Timer wieder aktivieren f"ur n"achsten Durchlauf
  timer_table[RABBIT_TIMER_READ2].expires = jiffies + SERIAL_WAIT_TIME;
  timer_active |= 1 << RABBIT_TIMER_READ2;
} 

static void SerialPort_writeTimer2 ( void )
{ 
  BYTE bChar;
  BOOL fWriteChar = FALSE;

  if ( sRabbiTBusy[1].iProtocol == RABBIT_PROTOCOL_CHAT )
  {
    while ( L1RingBuffer_charInBuffer(2+L2RINGBUFFER_WRITE) < L1RINGBUFFER_CHAR_MAX-1 )
    {
      if ( L2RingBuffer_takeChar(&bChar, 2+L2RINGBUFFER_WRITE, TRUE) != 0 )
        break;
      fWriteChar = TRUE;
      L1RingBuffer_insertChar(bChar, 2+L1RINGBUFFER_WRITE, TRUE);
    }
    if ( fWriteChar )
      SerialPort_writeIntOn ( 1 );
  }
  
  if ( sRabbiTBusy[1].iProtocol == RABBIT_PROTOCOL_WPAR )
  {  
    printk ( "WPAR-Protocol not Implemented.\n" );
  }

  // Timer wieder aktivieren f"ur n"achsten Durchlauf
  timer_table[RABBIT_TIMER_WRITE2].expires = jiffies + SERIAL_WAIT_TIME;
  timer_active |= 1 << RABBIT_TIMER_WRITE2;
} 

void SerialPort_searchStation ( int _iPort )
{
  fStationSearchInit[_iPort] = TRUE;
}

int SerialPort_isStationFound ( int _iPort )
{
  return ( iStationFound[_iPort] );
}

void SerialPort_timerInit ( void )
{
  fStationSearchInit[0] = FALSE;
  fStationSearch[0]     = FALSE;
  iStationFound[0]      = 0;
  fStationSearchInit[1] = FALSE;
  fStationSearch[1]     = FALSE;  
  iStationFound[1]      = 0;
  iReceivePos[0]        = 0;
  iReceiveMsg[0]        = 0;
  iReceivePos[1]        = 0;
  iReceiveMsg[1]        = 0;
  timer_table[RABBIT_TIMER_READ1].fn      = SerialPort_readTimer1;
  timer_table[RABBIT_TIMER_READ1].expires = jiffies + SERIAL_WAIT_TIME;
  timer_active |= 1 << RABBIT_TIMER_READ1;
  timer_table[RABBIT_TIMER_READ2].fn      = SerialPort_readTimer2;
  timer_table[RABBIT_TIMER_READ2].expires = jiffies + SERIAL_WAIT_TIME;
  timer_active |= 1 << RABBIT_TIMER_READ2;
  timer_table[RABBIT_TIMER_WRITE1].fn      = SerialPort_writeTimer1;
  timer_table[RABBIT_TIMER_WRITE1].expires = jiffies + SERIAL_WAIT_TIME;
  timer_active |= 1 << RABBIT_TIMER_WRITE1;
  timer_table[RABBIT_TIMER_WRITE2].fn      = SerialPort_writeTimer2;
  timer_table[RABBIT_TIMER_WRITE2].expires = jiffies + SERIAL_WAIT_TIME;
  timer_active |= 1 << RABBIT_TIMER_WRITE2;
}

