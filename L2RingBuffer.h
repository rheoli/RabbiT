//***************************************************************************
//***                 PAR - Protokoll Implementierung                     ***
//***                        Project: WotanPAR                            ***
//***************************************************************************
//*  RingBuffer des Layers 2 (WPAR-Protokoll)                               *
//***************************************************************************
//*                     Written (w) 1996 by RabbiT                          *
//*-------------------------------------------------------------------------*
//*                    Version 1.00.002, 18.12.1996                         *
//***************************************************************************
//*  Version 1.00.001, 15.12.96:                                            *
//*   - Prototyp-Version                                                    *
//*  Version 1.00.002, 18.12.96:                                            *
//*   - RingBuffer verallgemeinert fuer Layer 2                             *
//***************************************************************************

#ifndef _H_L2RINGBUFFER
#define _H_L2RINGBUFFER

#include "Allgemein.h"

#define L2RINGBUFFER_READ        0
#define L2RINGBUFFER_WRITE       1

#define L2RINGBUFFER_CHAR_MAX    100000

void L2RingBuffer_init ( void );
int L2RingBuffer_insertChar ( BYTE _bItem, unsigned int _uiPos, BOOL fWSA );
int L2RingBuffer_takeChar ( BYTE *_pbItem, unsigned int _uiPos, BOOL fWSA );
int L2RingBuffer_prozentFull ( unsigned int _uiPos );
int L2RingBuffer_charInBuffer ( unsigned int _uiPos );

#endif