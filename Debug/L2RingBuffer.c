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
  bBuffer[_uiPos][iNextInsert[_uiPos]] = _bItem;
  iNextInsert[_uiPos] = (iNextInsert[_uiPos]+1)%L2RINGBUFFER_INTERN_CHAR_MAX;
  iCharInBuffer[_uiPos]++;
  return ( 0 );
}

int L2RingBuffer_takeChar ( BYTE *_pbItem, unsigned int _uiPos, BOOL fWSA )
{
  if ( iCharInBuffer[_uiPos] == 0 )
    return ( -1 );
  *_pbItem = bBuffer[_uiPos][iNextTake[_uiPos]];
  iNextTake[_uiPos] = (iNextTake[_uiPos]+1)%L2RINGBUFFER_INTERN_CHAR_MAX;
  iCharInBuffer[_uiPos]--;
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

