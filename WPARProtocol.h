//***************************************************************************
//***                         Project: Wotan                              ***
//***                 PAR - Protokoll Implementierung                     ***
//***************************************************************************
//*                     Written (w) 1996 by RabbiT                          *
//*-------------------------------------------------------------------------*
//*                    Version 1.00.003, 26.12.1996                         *
//***************************************************************************
//*  Version 1.00.001, 08.11.96:                                            *
//*   - Prototyp-Version                                                    *
//*  Version 1.00.002, 19.11.96:                                            *
//*   - bPiggyQuitt umbenannt in bMessage                                   *
//*  Version 1.00.003, 26.12.96:                                            *
//*   - Verbesserungen                                                      *
//***************************************************************************

#include "Allgemein.h"

// Frameaufbau (bei Datensendung):
//   +------------+---------------+------+-------------+
//   | WPARHeader | CRC-16 Header | Text | CRC-32 Text |
//   +------------+---------------+------+-------------+
//
// Frameaufbau (bei Dienstmsg, Quittung only):
//   +------------+---------------+
//   | WPARHeader | CRC-16 Header |
//   +------------+---------------+

// Messages
#define WPAR_MSG_NOTING   0x00
#define WPAR_MSG_ACK      0x01
#define WPAR_MSG_NAK      0x02
#define WPAR_MSG_INIT     0x03

// Version
#define WPAR_VERSION      0x00

struct WPARHeader
{
  BYTE bSeqNum:1;        // Sequenznummer
  BYTE bMessage:3;       // Messages (WPAR_MSG_...)
  BYTE bMessageSeqNum:1; // Sequenznummer f"ur Piggyback (ACK, NAK)
  BYTE bLastFrame:1;     // letztes Frame
  BYTE bVersion:1;       // Version
  BYTE bReserved:1;      // auf Byte Ausrichten
  WORD wFrameNum;        // Framenummer
  WORD wFrameSize;       // Framegroesse
  WORD wCRC16;
};
