//***************************************************************************
//***                 PAR - Protokoll Implementierung                     ***
//***************************************************************************
//*                     Written (w) 1996 by RabbiT                          *
//*-------------------------------------------------------------------------*
//*                    Version 1.00.001, 09.11.1996                         *
//***************************************************************************
//*  Version 1.00.001, 09.11.96:                                            *
//*   - Allgemeine Definitionen                                             *
//***************************************************************************

#ifndef _H_ALLGEMEIN
#define _H_ALLGEMEIN

typedef int                BOOL;
#define TRUE                 0x01
#define FALSE                0x00

typedef unsigned char      BYTE;
typedef unsigned short     WORD;
typedef unsigned long      DWORD;
typedef unsigned int       UINT;
typedef unsigned long      ULONG;
typedef unsigned long int  UINT32;

#define MAKEWORD(low,high) (((WORD)((BYTE)(low)))|(((WORD)((BYTE)(high)))*0x100))
#define LOBYTE(w)          ((BYTE)(w))
#define HIBYTE(w)          ((BYTE)(((WORD)(w))/0x100)&0xFF)
#define MAKEULONG(low,high) (((ULONG)((WORD)(low)))|(((ULONG)((WORD)(high)))*0x10000))
#define LOWORD(ul)          ((WORD)(ul))
#define HIWORD(ul)          ((WORD)(((ULONG)(ul))/0x10000)&0xFFFF)

#endif
