//***************************************************************************
//***                 PAR - Protokoll Implementierung                     ***
//***                        Project: WotanPAR                            ***
//***************************************************************************
//*  RingBuffer des Layers 1 (serielle Schnittstelle)                       *
//***************************************************************************
//*                     Written (w) 1996 by RabbiT                          *
//*-------------------------------------------------------------------------*
//*                    Version 1.00.002, 18.12.1996                         *
//***************************************************************************
//*  Version 1.00.001, 15.12.96:                                            *
//*   - Prototyp-Version                                                    *
//*  Version 1.00.002, 18.12.96:                                            *
//*   - RingBuffer verallgemeinert fuer Layer 1                             *
//***************************************************************************

#ifndef _H_L1RINGBUFFER
#define _H_L1RINGBUFFER

#include "Allgemein.h"

#define L1RINGBUFFER_READ        0
#define L1RINGBUFFER_WRITE       1

#define L1RINGBUFFER_CHAR_MAX    15000

void L1RingBuffer_init ( void );
int L1RingBuffer_insertChar ( BYTE _bItem, unsigned int _uiPos, BOOL fWSA );
int L1RingBuffer_takeChar ( BYTE *_pbItem, unsigned int _uiPos, BOOL fWSA );
int L1RingBuffer_prozentFull ( unsigned int _uiPos );
int L1RingBuffer_charInBuffer ( unsigned int _uiPos );

#endif