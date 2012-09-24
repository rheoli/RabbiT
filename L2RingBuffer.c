//***************************************************************************
//***                 PAR - Protokoll Implementierung                     ***
//***                         Project: Wotan                              ***
//***************************************************************************
//*                     Written (w) 1996 by RabbiT                          *
//*-------------------------------------------------------------------------*
//*                    Version 1.00.001, 15.12.1996                         *
//***************************************************************************
//*  Version 1.00.001, 15.12.96:                                            *
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


#include "Allgemein.h"

#define L2RINGBUFFER_INTERN_CHAR_MAX    100000
#define L2RINGBUFFER_BUFFER_MAX              4

static BYTE bBuffer[L2RINGBUFFER_BUFFER_MAX][L2RINGBUFFER_INTERN_CHAR_MAX];
static int  iCharInBuffer[L2RINGBUFFER_BUFFER_MAX];
static int  iNextInsert[L2RINGBUFFER_BUFFER_MAX];
static int  iNextTake[L2RINGBUFFER_BUFFER_MAX];

void L2RingBuffer_init ( void )
{
  int i;
  for ( i = 0; i < L2RINGBUFFER_BUFFER_MAX; i++ )
  {
    iCharInBuffer[i] = 0;
    iNextTake[i]     = 0;
    iNextInsert[i]   = 0;
  }
}

int L2RingBuffer_insertChar ( BYTE _bItem, unsigned int _uiPos, BOOL fWSA )
{
  if ( iCharInBuffer[_uiPos] > (L2RINGBUFFER_INTERN_CHAR_MAX-1) )
    return ( -1 );
  if ( fWSA )
    cli ();
  bBuffer[_uiPos][iNextInsert[_uiPos]] = _bItem;
  iNextInsert[_uiPos] = (iNextInsert[_uiPos]+1)%L2RINGBUFFER_INTERN_CHAR_MAX;
  iCharInBuffer[_uiPos]++;
  if ( fWSA )
    sti ();
  return ( 0 );
}

int L2RingBuffer_takeChar ( BYTE *_pbItem, unsigned int _uiPos, BOOL fWSA )
{
  if ( iCharInBuffer[_uiPos] == 0 )
    return ( -1 );
  if ( fWSA )
    cli ();
  *_pbItem = bBuffer[_uiPos][iNextTake[_uiPos]];
  iNextTake[_uiPos] = (iNextTake[_uiPos]+1)%L2RINGBUFFER_INTERN_CHAR_MAX;
  iCharInBuffer[_uiPos]--;
  if ( fWSA )
    sti ();
  return ( 0 );
}

int L2RingBuffer_prozentRFull ( unsigned int _uiPos )
{
  return ( (iCharInBuffer[_uiPos]*100)/L2RINGBUFFER_INTERN_CHAR_MAX );
}

int L2RingBuffer_charInBuffer ( unsigned int _uiPos )
{
  return ( iCharInBuffer[_uiPos] );
}  

