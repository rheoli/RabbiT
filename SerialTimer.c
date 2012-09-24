//***************************************************************************
//***                         Project: Wotan                              ***
//***                 PAR - Protokoll Implementierung                     ***
//***************************************************************************
//*         Written (w) 1996-97 by Stephan Toggweiler (RabbiT)              *
//*-------------------------------------------------------------------------*
//*                    Version 1.00.004, 03.01.1997                         *
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
//*   - PiggyBack-Funktionen in eigenes Modul gesetzt                       *
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

#include "Allgemein.h"
#include "CRC16.h"
#include "CRC32.h"
#include "L1RingBuffer.h"
#include "L2RingBuffer.h"
#include "RABDef.h"
#include "WPARProtocol.h"
#include "SerialPort.h"

#define SERIAL_WAIT_TIME        80   // 80ms
#define SERIAL_CHAT_TIMEOUT    400   // 4sec
#define WPAR_BUFFER_SIZE     10000
#define WPAR_START_OF_HEADER  0x01

#define WPAR_STATE_SEARCH_SOH    0x01
#define WPAR_STATE_READ_HEADER   0x02
#define WPAR_STATE_READ_TEXT     0x03

static int           iStationSearch[2];
static int           iStationFound[2];
static unsigned long ulTime[2];

// Read-Abschnitt f"ur WPAR-Protokoll
static BYTE               bWPARReadBuffer[WPAR_BUFFER_SIZE];
static BYTE               *pbWPARReadPointer;
static BYTE               bReadHeaderCount;
static WORD               wReadTextCount;
static struct WPARHeader  *pwparReadHeader;
static BOOL               fSonderZeichen;
static BYTE               bReadState;
static BYTE               bReadSeqNum;

// Write-Abschnitt f"ur WPAR-Protokoll
static BYTE               bWPARWriteBuffer[WPAR_BUFFER_SIZE];
static BYTE               *pbWPARWritePointer;
static BYTE               bWriteHeaderCount;
static struct WPARHeader  *pwparWriteHeader;
static BYTE               bWriteSeqNum;

static void SerialPort_readTimer1 ( void )
{ 
  BYTE bChar;

  switch ( sRabbiTBusy[0].iProtocol )
  {
    case RABBIT_PROTOCOL_NONE:
      break;
      
    case RABBIT_PROTOCOL_CHAT:
      if ( (jiffies > ulTime[0]) && (iStationSearch[0]==2) )
      {
        iStationFound[0]  = 1;   // Not found
        iStationSearch[0] = 0;
      }
      while ( L2RingBuffer_charInBuffer(L2RINGBUFFER_READ) < L1RINGBUFFER_CHAR_MAX-1 )
      {
        if ( L1RingBuffer_takeChar(&bChar, L2RINGBUFFER_READ, TRUE) != 0 )
          break;
        printk ( "/dev/rab0: Zeichen empfangen: %d\n", bChar );
        if ( bChar == 0xFF )
        {
          L1RingBuffer_insertChar ( 0xFE, L1RINGBUFFER_WRITE, TRUE );
          SerialPort_writeIntOn ( 0 );
        }
        else
        {
          if ( bChar == 0xFE )
          {
            printk ( "/dev/rab0: Station gefunden.\n" );
            iStationFound[0]  = 2;
            iStationSearch[0] = 0;
          }
          else
          {
            L2RingBuffer_insertChar(bChar, L1RINGBUFFER_READ, TRUE);
          }
        }
      }
      break;  
      
    case RABBIT_PROTOCOL_WPAR:
      while ( L1RingBuffer_charInBuffer(L1RINGBUFFER_READ) > 0 )
      {      
        switch ( bReadState )
        {
          case WPAR_STATE_SEARCH_SOH:
            while ( L1RingBuffer_takeChar(&bChar, L2RINGBUFFER_READ, TRUE) == 0 )
            {           
              printk ( "/dev/rab0-WPAR_SOH: Suche nach SOH (%d)\n", bChar );
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
                printk ( "/dev/rab0-WPAR_SOH: SOH gefunden.\n" );
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
              printk ( "/dev/rab0-WPAR_HEADER: lese Header (%d).\n", bChar );
              if ( (bChar==0xFF) && (!fSonderZeichen) )
              {
                fSonderZeichen = TRUE;
                continue;
              }
              if ( (bChar==WPAR_START_OF_HEADER) && (!fSonderZeichen) )
              {  // ein Frame wurde verst"ummelt
                printk ( "/dev/rab0-WPAR_HEADER: Header wurde verstuemmelt; habe ein SOH gefunden.\n" );
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
              if ( bReadHeaderCount > sizeof(struct WPARHeader) )
              {
                pwparReadHeader = (struct WPARHeader *)&bWPARReadBuffer[0];
                // CRC Ueberpr"ufen
                if ( pwparReadHeader->wCRC16 != makeCRC16(&bWPARReadBuffer[0], sizeof(struct WPARHeader)-2) )
                {  // Header CRC ist falsch
                  setPiggyBack ( WPAR_PIGGYBACK_NAK, bReadSeqNum );
                  printk ( "/dev/rab0-WPAR_HEADER: verstuemmelten Header empfangen (CRC falsch).\n" );
                  bReadState = WPAR_STATE_SEARCH_SOH;
                  break;
                }
                printk ( "/dev/rab0-WPAR_HEADER: Header eines Frames korrekt angekommen.\n" );
                wReadTextCount  = 0;
                bReadState      = WPAR_STATE_READ_TEXT;              
                break;
              }
            }
            break;
          case WPAR_STATE_READ_TEXT:
            while ( L1RingBuffer_takeChar(&bChar, L2RINGBUFFER_READ, TRUE) == 0 )
            {
              printk ( "/dev/rab0-WPAR_TEXT: Text lesen (%d)\n", bChar );
              if ( (bChar==0xFF) && (!fSonderZeichen) )
              {
                fSonderZeichen = TRUE;
                continue;
              }
              if ( (bChar==WPAR_START_OF_HEADER) && (!fSonderZeichen) )
              {  // ein Frame wurde verst"ummelt
                setPiggyBack ( WPAR_PIGGYBACK_NAK, bReadSeqNum );
                printk ( "/dev/rab0-WPAR_TEXT: verstuemmeltes Frame empfangen.\n" );
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
              if ( wReadTextCount > (pwparReadHeader->wFrameSize+4) )
              {
                WORD  wCRC32Lo = MAKEWORD(*(pbWPARReadPointer-4),*(pbWPARReadPointer-5));
                WORD  wCRC32Hi = MAKEWORD(*(pbWPARReadPointer-2),*(pbWPARReadPointer-3));
                ULONG ulCRC32  = MAKEULONG(wCRC32Lo,wCRC32Hi);
                // Frame CRC pr"ufen
                if ( ulCRC32 != makeCRC32((&bWPARReadBuffer[0]+sizeof(struct WPARHeader)), pwparReadHeader->wFrameSize) )
                {  // Frame CRC ist falsch
                  printk ( "/dev/rab0-WPAR_TEXT: Frame mit falschem CRC32 angekommen.\n" );
                  setPiggyBack ( WPAR_PIGGYBACK_NAK, bReadSeqNum );
                  bReadState = WPAR_STATE_SEARCH_SOH;
                  break;
                }
                // Frame verarbeiten
                printk ( "/dev/rab0-WPAR_TEXT: Frame ist korrekt angekommen\n" );
                setPiggyBack ( WPAR_PIGGYBACK_ACK, bReadSeqNum );
                bReadState = WPAR_STATE_SEARCH_SOH;
                break;
              }
            }
            break;
        }
      }
      break;
      
    case RABBIT_PROTOCOL_XMODEM:
      printk ( "/dev/rab0: XModem-Protocol not Implemented.\n" );
      break;
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

  switch ( sRabbiTBusy[0].iProtocol )
  {  
    case RABBIT_PROTOCOL_NONE:
      break;
      
    case RABBIT_PROTOCOL_CHAT:
      if ( iStationSearch[0] == 1 )
      {
        iStationSearch[0] = 2;
        iStationFound[0]  = 0;
        fWriteChar        = TRUE;
        ulTime[0]         = jiffies+SERIAL_CHAT_TIMEOUT;
        L1RingBuffer_insertChar ( 0xFF, L1RINGBUFFER_WRITE, TRUE );
        printk ( "/dev/rab0: Sende Connection-Test-Zeichen\n" );
      }
      else
      { 
        while ( L1RingBuffer_charInBuffer(L1RINGBUFFER_WRITE) < L1RINGBUFFER_CHAR_MAX-1 )
        {
          if ( L2RingBuffer_takeChar(&bChar, L2RINGBUFFER_WRITE, TRUE) != 0 )
            break;
          fWriteChar = TRUE;
          L1RingBuffer_insertChar(bChar, L1RINGBUFFER_WRITE, TRUE);
        }
      }
      break;
      
    case RABBIT_PROTOCOL_WPAR:
      printk ( "/dev/rab0(WPAR): Anfang\n" );
      if ( L2RingBuffer_charInBuffer(L2RINGBUFFER_WRITE) > 0 )
      {
        WORD  wCount;
        int   i;
        ULONG ulCRC32;
        printk ( "/dev/rab0(WPAR) Judihui es haet Zeichae fuer mich\n" );
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
        pwparWriteHeader->bLastFrame = 1;
        pwparWriteHeader->bVersion   = 0;
        pwparWriteHeader->bReserved  = 0;
        pwparWriteHeader->wFrameNum  = 0;
        pwparWriteHeader->wFrameSize = 0;   // Vorl"aufig 
        pwparWriteHeader->wCRC16     = 0;   // Vorl"aufig 
        wCount = 0;
        while ( wCount < (WPAR_BUFFER_SIZE-4-sizeof(struct WPARHeader)) )
        {
          if ( L2RingBuffer_takeChar(pbWPARWritePointer, L2RINGBUFFER_WRITE, TRUE) != 0 )
            break;
          pbWPARWritePointer++;
          wCount++;
        }
        pwparWriteHeader->wFrameSize = wCount;
        pwparWriteHeader->wCRC16     = makeCRC16( &bWPARWriteBuffer[0], sizeof(struct WPARHeader)-2 );
        ulCRC32 = makeCRC32 ( (&bWPARReadBuffer[0]+sizeof(struct WPARHeader)), wCount );
        *pbWPARWritePointer = LOBYTE(LOWORD(ulCRC32));
        pbWPARWritePointer++;
        *pbWPARWritePointer = HIBYTE(LOWORD(ulCRC32));
        pbWPARWritePointer++;
        *pbWPARWritePointer = LOBYTE(HIWORD(ulCRC32));
        pbWPARWritePointer++;
        *pbWPARWritePointer = HIBYTE(HIWORD(ulCRC32));
        pbWPARWritePointer++;
        wCount += sizeof(struct WPARHeader) + 4;
        printk ( "/dev/rab0(WPAR): Zeichen: " );
        for ( i = 0; i < wCount; i++ )
        {
          printk ( " %x", bWPARWriteBuffer[i] );
          L1RingBuffer_insertChar(bWPARWriteBuffer[i], L1RINGBUFFER_WRITE, TRUE);        
        }        
        printk ( "\n" );
        fWriteChar = TRUE;
      }
      break;

    case RABBIT_PROTOCOL_XMODEM:
      printk ( "/dev/rab0: XModem-Protocol not Implemented.\n" );
      break;    
  }

  // Wenn n"otig, den Schreib-Interrupt der seriellen Schnittstelle
  // einschalten.
  if ( fWriteChar )
    SerialPort_writeIntOn ( 0 );
  
  // Timer wieder aktivieren f"ur n"achsten Durchlauf
  timer_table[RABBIT_TIMER_WRITE1].expires = jiffies + SERIAL_WAIT_TIME;
  timer_active |= 1 << RABBIT_TIMER_WRITE1;
} 

static void SerialPort_readTimer2 ( void )
{ 
  BYTE bChar;

  if ( sRabbiTBusy[1].iProtocol == RABBIT_PROTOCOL_CHAT )
  {
    if ( (jiffies > ulTime[1]) && (iStationSearch[1]==2) )
    {
      iStationFound[1]  = 1;   // Not found
      iStationSearch[1] = 0;
    }
    while ( L2RingBuffer_charInBuffer(2+L2RINGBUFFER_READ) < L1RINGBUFFER_CHAR_MAX-1 )
    {
      if ( L1RingBuffer_takeChar(&bChar, 2+L2RINGBUFFER_READ, TRUE) != 0 )
        break;
      printk ( "/dev/rab1: Zeichen empfangen: %d\n", bChar );
      if ( bChar == 0xFF )
      {
        printk ( "/dev/rab1: Connection-Zeichen empfangen, zuruecksenden.\n" );
        L1RingBuffer_insertChar ( 0xFE, 2+L1RINGBUFFER_WRITE, TRUE );
        SerialPort_writeIntOn ( 1 );
      }
      else
      {
        if ( bChar == 0xFE )
        {
          iStationFound[1]  = 2;
          iStationSearch[1] = 0;
        }
        else
        {
          L2RingBuffer_insertChar(bChar, 2+L1RINGBUFFER_READ, TRUE);
        }
      }
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
    if ( iStationSearch[1] == 1 )
    {
      iStationSearch[1] = 2;
      iStationFound[1]  = 0;
      fWriteChar        = TRUE;
      ulTime[1]         = jiffies+SERIAL_CHAT_TIMEOUT;
      L1RingBuffer_insertChar ( 0xFF, 2+L1RINGBUFFER_WRITE, TRUE );
    }
    else
    {
      while ( L1RingBuffer_charInBuffer(2+L2RINGBUFFER_WRITE) < L1RINGBUFFER_CHAR_MAX-1 )
      {
        if ( L2RingBuffer_takeChar(&bChar, 2+L2RINGBUFFER_WRITE, TRUE) != 0 )
          break;
        fWriteChar = TRUE;
        L1RingBuffer_insertChar(bChar, 2+L1RINGBUFFER_WRITE, TRUE);
      }
    }    
    if ( fWriteChar )
      SerialPort_writeIntOn ( 1 );
  }
  
  if ( sRabbiTBusy[1].iProtocol == RABBIT_PROTOCOL_WPAR )
  {  
    printk ( "/dev/rab1: WPAR-Protocol not Implemented.\n" );
  }

  // Timer wieder aktivieren f"ur n"achsten Durchlauf
  timer_table[RABBIT_TIMER_WRITE2].expires = jiffies + SERIAL_WAIT_TIME;
  timer_active |= 1 << RABBIT_TIMER_WRITE2;
} 

void SerialPort_searchStation ( int _iPort )
{
  iStationSearch[_iPort] = 1;
}

int SerialPort_isStationFound ( int _iPort )
{
  return ( iStationFound[_iPort] );
}

void SerialPort_timerInit ( void )
{
  iStationSearch[0]     = 0;
  iStationFound[0]      = 0;
  iStationSearch[1]     = 0;
  iStationFound[1]      = 0;
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

