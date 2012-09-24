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

#ifndef _H_SERIALPORT
#define _H_SERIALPORT

struct SERIALINFO
{
  int iPortType;
  int iChipType;
  int iPortAddr;
  int iIRQ;
  int iFlags;
  int iFIFOTrigger;
  int iFIFOSize;
  int iDivisor;
};

void SerialPort_start ( int _iPort );
void SerialPort_init ( void );
void SerialPort_release ( int _iPort );
void SerialPort_writeIntOn ( int _iPort );
#endif
