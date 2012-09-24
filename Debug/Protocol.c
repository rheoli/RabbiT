//***************************************************************************
//***                 Project: Wotan  (DEBUG-Version)                     ***
//***                 PAR - Protokoll Implementierung                     ***
//***************************************************************************
//*         Written (w) 1996-97 by Stephan Toggweiler (RabbiT)              *
//*-------------------------------------------------------------------------*
//*                    Version 1.0d.001, 03.01.1997                         *
//***************************************************************************
//*  Version 1.00.001, 21.12.96:                                            *
//*   - Prototyp-Version, "ubernommen von RabbiT.c                          *
//*  Version 1.00.002, 26.12.96:                                            *
//*   - Im CHAT Betrieb kann die Verbindung anhand von abgesendeten Token   *
//*     getestet werden, ob die Verbindung noch OK ist                      *
//*   - Neues Protokoll WotanPAR begonnen zu erstellen                      *
//*  Version 1.00.003, 02.01.97:                                            *
//*   - Weiterf"uhren des Protokolls WotanPAR                               *
//*  Version 1.00.004, 03.01.97:                                            *
//*   - Weiterf"uhren des Protokolls WotanPAR                               *
//*  Version 1.0d.001, 03.01.97:                                            *
//*   - Alle Aenderungen werden mit //Neu! bezeichnet !!!!                  *
//***************************************************************************

#include <stdio.h>
#include "Allgemein.h"
#include "CRC16.h"
#include "CRC32.h"
#include "L1RingBuffer.h"
#include "L2RingBuffer.h"
#include "RABDef.h"
#include "WPARProtocol.h"

#define WPAR_BUFFER_SIZE     10000
#define WPAR_START_OF_HEADER  0x01

#define WPAR_STATE_SEARCH_SOH    0x01
#define WPAR_STATE_READ_HEADER   0x02
#define WPAR_STATE_READ_TEXT     0x03

//NEU! (Begin)
struct WPARStruct
{
  // Read-Abschnitt f"ur WPAR-Protokoll
  BYTE               bWPARReadBuffer[WPAR_BUFFER_SIZE];
  BYTE               *pbWPARReadPointer;
  BYTE               bReadHeaderCount;
  WORD               wReadTextCount;
  WORD               wReadFrameSize;
  struct WPARHeader  *pwparReadHeader;
  BOOL               fSonderZeichen;
  BYTE               bReadState;
  BYTE               bReadSeqNum;

  // Write-Abschnitt f"ur WPAR-Protokoll
  BYTE               bWPARWriteBuffer[WPAR_BUFFER_SIZE];
  BYTE               *pbWPARWritePointer;
  BYTE               bWriteHeaderCount;
  struct WPARHeader  *pwparWriteHeader;
  BYTE               bWriteSeqNum;
};
//NEU! (Ende)


static int           iStationSearch[2];
static int           iStationFound[2];
static unsigned long ulTime[2];

// Read-Abschnitt f"ur WPAR-Protokoll
static BYTE               bWPARReadBuffer[WPAR_BUFFER_SIZE];
static BYTE               *pbWPARReadPointer;
static BYTE               bReadHeaderCount;
static WORD               wReadTextCount;
//NEU! (Begin)
static WORD               wReadFrameSize;
//NEU! (Ende)
static struct WPARHeader  *pwparReadHeader;
static BOOL               fSonderZeichen;
static BYTE               bReadState = 0;
static BYTE               bReadSeqNum = 0;

// Write-Abschnitt f"ur WPAR-Protokoll
static BYTE               bWPARWriteBuffer[WPAR_BUFFER_SIZE];
static BYTE               *pbWPARWritePointer;
static BYTE               bWriteHeaderCount;
static struct WPARHeader  *pwparWriteHeader;
static BYTE               bWriteSeqNum = 0;

static BYTE          bPiggyBack[2][2];
#define WPAR_PIGGYBACK_NONE    0x00
#define WPAR_PIGGYBACK_ACK     0x01
#define WPAR_PIGGYBACK_NAK     0x02

static void setPiggyBack ( BYTE _bMsg, BYTE _bSeq )
{
  if ( bPiggyBack[0][0] != WPAR_PIGGYBACK_NONE )
  {
    bPiggyBack[1][0] = _bMsg;
    bPiggyBack[1][1] = _bSeq;
  }
  else
  {
    bPiggyBack[0][0] = _bMsg;
    bPiggyBack[0][1] = _bSeq;
  }
}

static int isPiggyBack ( void )
{
  return ( bPiggyBack[0][0] );
}

static void getPiggyBack ( BYTE *_pbMsg, BYTE *_pbSeq )
{
  *_pbMsg = bPiggyBack[0][0];
  *_pbSeq = bPiggyBack[0][1];
  if ( bPiggyBack[1][0] != WPAR_PIGGYBACK_NONE )
  {
    bPiggyBack[0][0] = bPiggyBack[1][0];
    bPiggyBack[0][1] = bPiggyBack[1][1];
    bPiggyBack[1][0] = WPAR_PIGGYBACK_NONE;
  }
}


static void Protocol_readFrame ( void )
{
  BYTE bChar;
  
      while ( L1RingBuffer_charInBuffer(L1RINGBUFFER_READ) > 0 )
      {      
        switch ( bReadState )
        {
          case WPAR_STATE_SEARCH_SOH:
            while ( L1RingBuffer_takeChar(&bChar, L2RINGBUFFER_READ, TRUE) == 0 )
            {           
              printf ( "/dev/rab0-WPAR_SOH: Suche nach SOH (%d)\n", bChar );
              if ( fSonderZeichen )
              {
                fSonderZeichen = FALSE;
                continue;
              }
              if ( bChar == 0xFF )
              {
                fSonderZeichen = TRUE;
                continue;
              }
              if ( (bChar==WPAR_START_OF_HEADER) && (!fSonderZeichen) )
              {
                printf ( "/dev/rab0-WPAR_SOH: SOH gefunden.\n" );
                pbWPARReadPointer = &bWPARReadBuffer[0];
                bReadHeaderCount  = 0;
                bReadState        = WPAR_STATE_READ_HEADER;
                break;
              }
            }
            break;          
          case WPAR_STATE_READ_HEADER:
            while ( L1RingBuffer_takeChar(&bChar, L2RINGBUFFER_READ, TRUE) == 0 )
            {
              printf ( "/dev/rab0-WPAR_HEADER: lese Header (%x).\n", bChar );
              if ( (bChar==0xFF) && (!fSonderZeichen) )
              {
                fSonderZeichen = TRUE;
                continue;
              }
              if ( (bChar==WPAR_START_OF_HEADER) && (!fSonderZeichen) )
              {  // ein Frame wurde verst"ummelt
                printf ( "/dev/rab0-WPAR_HEADER: Header wurde verstuemmelt; habe ein SOH gefunden.\n" );
                setPiggyBack ( WPAR_PIGGYBACK_NAK, bReadSeqNum );
                pbWPARReadPointer = &bWPARReadBuffer[0];
                bReadHeaderCount  = 0;
                break;
              }
              if ( fSonderZeichen )
                fSonderZeichen = FALSE;
              *pbWPARReadPointer = bChar;
              pbWPARReadPointer++;
              bReadHeaderCount++;
//NEU! (Begin) -> '>=' eingesetzt, anstatt '>'
              if ( bReadHeaderCount >= sizeof(struct WPARHeader) )
//NEU! (Ende)
              {
                pwparReadHeader = (struct WPARHeader *)&bWPARReadBuffer[0];
                // CRC Ueberpr"ufen
                if ( MAKEWORD(pwparReadHeader->bCRC16Lo,pwparReadHeader->bCRC16Hi)
                                 != makeCRC16(&bWPARReadBuffer[0], sizeof(struct WPARHeader)-2) )
                {  // Header CRC ist falsch
                  setPiggyBack ( WPAR_PIGGYBACK_NAK, bReadSeqNum );
                  printf ( "/dev/rab0-WPAR_HEADER: verstuemmelten Header empfangen (CRC falsch).\n" );
                  bReadState = WPAR_STATE_SEARCH_SOH;
                  break;
                }
                printf ( "/dev/rab0-WPAR_HEADER: Header eines Frames korrekt angekommen.\n" );
                wReadTextCount  = 0;
                wReadFrameSize  = MAKEWORD(pwparReadHeader->bFrameSizeLo,pwparReadHeader->bFrameSizeHi);
                bReadState      = WPAR_STATE_READ_TEXT;                              
                break;
              }
            }
            break;
          case WPAR_STATE_READ_TEXT:
            while ( L1RingBuffer_takeChar(&bChar, L2RINGBUFFER_READ, TRUE) == 0 )
            {
              printf ( "/dev/rab0-WPAR_TEXT: Text lesen (%x)\n", bChar );
              printf ( "wReadTextCount: %d\n", wReadTextCount );
              if ( (bChar==0xFF) && (!fSonderZeichen) )
              {
                printf ( "Sonderzeichen gefunden\n" );
                fSonderZeichen = TRUE;
                continue;
              }
              if ( (bChar==WPAR_START_OF_HEADER) && (!fSonderZeichen) )
              {  // ein Frame wurde verst"ummelt
                setPiggyBack ( WPAR_PIGGYBACK_NAK, bReadSeqNum );
                printf ( "/dev/rab0-WPAR_TEXT: verstuemmeltes Frame empfangen.\n" );
                pbWPARReadPointer = &bWPARReadBuffer[0];
                bReadHeaderCount  = 0;
                bReadState        = WPAR_STATE_READ_HEADER;
                break;
              }
              if ( fSonderZeichen )
                fSonderZeichen = FALSE;
              *pbWPARReadPointer = bChar;
              pbWPARReadPointer++;
              wReadTextCount++;
//NEU! (Begin) -> '>=' eingesetzt, anstatt '>'
              if ( wReadTextCount >= (wReadFrameSize+4) )
//NEU! (Ende)
              {                
                int   i;
                WORD  wCRC32Lo = MAKEWORD(*(pbWPARReadPointer-4),*(pbWPARReadPointer-3));
                WORD  wCRC32Hi = MAKEWORD(*(pbWPARReadPointer-2),*(pbWPARReadPointer-1));
                ULONG ulCRC32  = MAKEULONG(wCRC32Lo,wCRC32Hi);                
                printf ( "CRC32-Code (%lx)\n", ulCRC32 );
                printf ( "----------\n" );
                printf ( "LoLo: %d\n", *(pbWPARReadPointer-4) );
                printf ( "HiLo: %d\n", *(pbWPARReadPointer-3) );
                printf ( "LoHi: %d\n", *(pbWPARReadPointer-2) );
                printf ( "HiHi: %d\n", *(pbWPARReadPointer-1) );
                printf ( "Frame pr\"ufen\n" );
                printf ( "Berechneter CRC32: %lx\n", makeCRC32((&bWPARReadBuffer[0]+sizeof(struct WPARHeader)), wReadFrameSize) );
                printf ( "wReadFrameSize: %d\n", wReadFrameSize );
                // Frame CRC pr"ufen
                if ( ulCRC32 != makeCRC32((&bWPARReadBuffer[0]+sizeof(struct WPARHeader)), wReadFrameSize) )
                {  // Frame CRC ist falsch
                  printf ( "/dev/rab0-WPAR_TEXT: Frame mit falschem CRC32 angekommen.\n" );
                  setPiggyBack ( WPAR_PIGGYBACK_NAK, bReadSeqNum );
                  bReadState = WPAR_STATE_SEARCH_SOH;
                  break;
                }
                // Frame verarbeiten
                printf ( "/dev/rab0-WPAR_TEXT: Frame ist korrekt angekommen\n" );
//NEU! (Begin) -> Text weitergeben
                for ( i = 0; i < wReadFrameSize; i++ )
                  L2RingBuffer_insertChar ( bWPARReadBuffer[sizeof(struct WPARHeader)+i], L2RINGBUFFER_READ, TRUE );
//NEU! (Ende)
                setPiggyBack ( WPAR_PIGGYBACK_ACK, bReadSeqNum );
                bReadState = WPAR_STATE_SEARCH_SOH;
                break;
              }
            }
            break;
        }
      }
}



static void Protocol_writeFrame ( void )
{ 
  BYTE bChar;

      printf ( "/dev/rab0(WPAR): Anfang\n" );
      if ( L2RingBuffer_charInBuffer(L2RINGBUFFER_WRITE) > 0 )
      {
        WORD  wCount;
        int   i;
        ULONG ulCRC32;
        WORD  wCRC16;
        printf ( "/dev/rab0(WPAR) Judihui es haet Zeichae fuer mich\n" );
        pwparWriteHeader = (struct WPARHeader *)&bWPARWriteBuffer[0];
        pbWPARWritePointer = &bWPARWriteBuffer[0] + sizeof(struct WPARHeader);
        
        pwparWriteHeader->bSeqNum = bWriteSeqNum;
        if ( isPiggyBack () )
        {
          BYTE bMsg, bSeq;
          getPiggyBack ( &bMsg, &bSeq );
          pwparWriteHeader->bMessage       = bMsg;
          pwparWriteHeader->bMessageSeqNum = bSeq;
        }
//NEU! (Begin) -> Damit die Messages immer definiert sind        
        else 
        {
          pwparWriteHeader->bMessage       = 0;
          pwparWriteHeader->bMessageSeqNum = 0;
        }
//NEU! (Ende)
        pwparWriteHeader->bLastFrame = 1;
        pwparWriteHeader->bVersion   = 0;
        pwparWriteHeader->bReserved  = 0;
//NEU! (Begin) -> Ausrichtungsprobleme gel"ost        
        pwparWriteHeader->bFrameNumLo  = 0;
        pwparWriteHeader->bFrameNumHi  = 0;
        pwparWriteHeader->bFrameSizeLo = 0;
        pwparWriteHeader->bFrameSizeHi = 0;
        pwparWriteHeader->bCRC16Lo     = 0;
        pwparWriteHeader->bCRC16Hi     = 0;
//NEU! (Ende)
        wCount = 0;
        while ( wCount < (WPAR_BUFFER_SIZE-4-sizeof(struct WPARHeader)) )
        {
          if ( L2RingBuffer_takeChar(pbWPARWritePointer, L2RINGBUFFER_WRITE, TRUE) != 0 )
            break;
          pbWPARWritePointer++;
          wCount++;
        }
        pwparWriteHeader->bFrameSizeLo = LOWORD(wCount);
        pwparWriteHeader->bFrameSizeHi = HIWORD(wCount);
        wCRC16 = makeCRC16( &bWPARWriteBuffer[0], sizeof(struct WPARHeader)-2 );
        pwparWriteHeader->bCRC16Lo = LOBYTE(wCRC16);
        pwparWriteHeader->bCRC16Hi = HIBYTE(wCRC16);
//NEU! (Begin) -> ReadBuffer mit WriteBuffer ausgetauscht
        ulCRC32 = makeCRC32 ( (&bWPARWriteBuffer[0]+sizeof(struct WPARHeader)), wCount );
//NEU! (Ende)
        printf ( "wCount: %d\n", wCount );
        printf ( "CRC32-Code (%lx)\n", ulCRC32 );
        printf ( "----------\n" );
        *pbWPARWritePointer = LOBYTE(LOWORD(ulCRC32));
        printf ( "LoLo: %d\n", *pbWPARWritePointer );
        pbWPARWritePointer++;
        *pbWPARWritePointer = HIBYTE(LOWORD(ulCRC32));
        printf ( "HiLo: %d\n", *pbWPARWritePointer );
        pbWPARWritePointer++;
        *pbWPARWritePointer = LOBYTE(HIWORD(ulCRC32));
        printf ( "LoHi: %d\n", *pbWPARWritePointer );
        pbWPARWritePointer++;
        *pbWPARWritePointer = HIBYTE(HIWORD(ulCRC32));
        printf ( "HiHi: %d\n", *pbWPARWritePointer );
        pbWPARWritePointer++;
        wCount += sizeof(struct WPARHeader) + 4;
        printf ( "/dev/rab0(WPAR): Zeichen: " );
//NEU! (Begin)
        L1RingBuffer_insertChar ( WPAR_START_OF_HEADER, L1RINGBUFFER_READ, TRUE );
        // ACHTUNG! L1RINGBUFFFER_READ sollte ... _WRITE sein --------^ (nur f"ur Debug ge"andert)
//NEU! (Ende)
        for ( i = 0; i < wCount; i++ )
        {
          printf ( " %x", bWPARWriteBuffer[i] );
//NEU! (Begin) -> Sonderzeichen ausfiltern
          switch ( bWPARWriteBuffer[i] )
          {
            case 0xFF:
            case WPAR_START_OF_HEADER:
              L1RingBuffer_insertChar(0xFF, L1RINGBUFFER_READ, TRUE);
              break;
          }
//NEU! (Ende)
          L1RingBuffer_insertChar(bWPARWriteBuffer[i], L1RINGBUFFER_READ, TRUE);
        // ACHTUNG! L1RINGBUFFFER_READ sollte ... _WRITE sein --------^ (nur f"ur Debug ge"andert)
        }        
        printf ( "\n" );
      }
}


void main ()
{
  BYTE bChar;
  
  printf ( "WotanPAR Simulation V 1.0d.001\n" );
  printf ( "------------------------------\n\n" );
  // RingBuffer initialisieren

//NEU! (Begin) Variablen initialisieren 
  bReadState = 0;
  bReadSeqNum = 0;
  bWriteSeqNum = 0;
//NEU! (Ende)
 
  L1RingBuffer_init ();
  L2RingBuffer_init ();
  
  // "AB" in L2 RingBuffer einf"ugen
  L2RingBuffer_insertChar ( 'A', L2RINGBUFFER_WRITE, TRUE );
  L2RingBuffer_insertChar ( 'B', L2RINGBUFFER_WRITE, TRUE );
  L2RingBuffer_insertChar ( 'C', L2RINGBUFFER_WRITE, TRUE );
  L2RingBuffer_insertChar ( 'D', L2RINGBUFFER_WRITE, TRUE );  

  // Schreiben eines Frames simulieren
  printf ( "... Frame auf Kanal schreiben ...\n" );
  Protocol_writeFrame ();
  
  // Lesen des Frames simulieren
//NEU! (Begin) -> Initialisierung vergessen!!!!!  
  bReadState     = WPAR_STATE_SEARCH_SOH;
  fSonderZeichen = FALSE;
//NEU! (Ende)
  printf ( "... Frame von Kanal lesen ...\n" );
  Protocol_readFrame ();
  
  printf ( "Empfangener Text: " );
  while ( L2RingBuffer_takeChar(&bChar, L2RINGBUFFER_READ, TRUE) == 0 )
    printf ( "%c", bChar );
  printf ( "\n" );
}