From wotan@ia94togg.cs.htl-bw.ch  Tue Dec 17 14:45:41 1996
Return-Path: wotan
Received: (from wotan@localhost) by ia94togg.cs.htl-bw.ch (8.7.5/8.7.3) id OAA00828 for kernel; Tue, 17 Dec 1996 14:45:41 +0100
Date: Tue, 17 Dec 1996 14:45:41 +0100
From: Wotan der Wind <wotan@ia94togg.cs.htl-bw.ch>
Message-Id: <199612171345.OAA00828@ia94togg.cs.htl-bw.ch>
To: kernel@ia94togg.cs.htl-bw.ch
Status: RO
X-Status: 

#include <stdio.h>
#include "Allgemein.h"

#define WPAR_STALE 0x00
#define WPAR_FRA   0x01
#define WPAR_ACK   0x02
#define WPAR_NAK   0x03
#define WPAR_NONE  0xFF

struct WPARSTATEEVENTTABLE
{
  BYTE bDescr;
  BYTE bDescrSeq;
  BYTE bSendState;
  BYTE bReceiveState;
  BYTE bNewFrameState;
  BYTE bGoodState;
  BYTE bBadState;
  BYTE bTimeoutState;
};

struct WPARSTATEEVENTTABLE setTable[] = {
{ WPAR_STALE, 0, 0, 0, 1,         WPAR_NONE, WPAR_NONE, WPAR_NONE },
{ WPAR_FRA,   0, 0, 0, WPAR_NONE, 2,         6,         1         },
{ WPAR_ACK,   0, 0, 1, WPAR_NONE, 3,         7,         7         },
{ WPAR_STALE, 0, 1, 1, 4,         WPAR_NONE, WPAR_NONE, WPAR_NONE },
{ WPAR_FRA,   1, 1, 1, WPAR_NONE, 5,         9,         4         },
{ WPAR_ACK,   1, 1, 0, WPAR_NONE, 0,         10,        10        },
{ WPAR_NAK,   0, 0, 0, WPAR_NONE, 1,         1,         1         },
{ WPAR_FRA,   0, 0, 1, WPAR_NONE, 2,         8,         7         },
{ WPAR_NAK,   1, 0, 1, WPAR_NONE, 3,         7,         7         },
{ WPAR_NAK,   1, 1, 1, WPAR_NONE, 4,         4,         4         },
{ WPAR_FRA,   1, 1, 0, WPAR_NONE, 5,         11,        10        },
{ WPAR_NAK,   0, 1, 0, WPAR_NONE, 0,         10,        10        } };

struct FRAME
{
  BYTE bSeq;
  BYTE bMess;
  BYTE bText;
};

void main ()
{
  struct FRAME sFrame;
  BYTE bSAktState = 3;
  BYTE bSNextState;
  BYTE bSSeq = 0;
  BYTE bEAktState = 1;
  BYTE bENextState;  
  BYTE bESeq = 0;
  BYTE bFrame = 0;
  BYTE bFrameSend   = 0;
  BYTE bFrameRequest = 0;
  char cChar;
  
  printf ( "State Maschine for WotanPAR Protocol\n" );
  printf ( "------------------------------------\n\n" );
  
  while ( 1 )
  {
    if ( setTable[bSAktState].bNewFrameState != WPAR_NONE )
    {
      bSNextState = setTable[bSAktState].bNewFrameState;
      bFrame++;
      printf ( "Neues Frame (Text: A%c) geladen.\n", bFrame+'E' );
      bSAktState = bSNextState;
      continue;
    }
    
    if ( (bFrameSend==0)&&(bFrameRequest==0) )
    {
      bFrameSend   = 1;
      sFrame.bSeq  = setTable[bSAktState].bDescrSeq;
      sFrame.bMess = 0;
      sFrame.bText = bFrame;
      printf ( "Frame gesendet (Seq: %d/Text: A%c)\n", sFrame.bSeq,
                                  sFrame.bText+'E' );
      printf ( "\n" );
      continue;
    }

    if ( bFrameRequest == 1 )
    {
      int i;
      for ( i = 0; i < 12; i++ )
      {
        if ( (setTable[i].bDescrSeq==sFrame.bSeq)&&
                    (setTable[i].bDescr==sFrame.bMess)&&
                    (setTable[i].bSendState==bSSeq) )
          break;
      }
      printf ( "Ist Quittung gut angekommen (g,v): " );
      do
      {
        scanf ( "%c", &cChar );
      }
      while ( cChar < 'A' );
      switch ( cChar )
      {
        case 'g':
          bSAktState    = setTable[i].bGoodState;
          break;
        case 'v':
          bSAktState    = setTable[i].bBadState;
          break;
      }
      printf ( "Neuer State: %d\n", bSAktState );
      bSSeq         = setTable[bSAktState].bSendState;
      bFrameRequest = 0;
      printf ( "\n" );
      continue;
    }
    
    if ( (bFrameSend==1)&&(bFrameRequest==0) )
    {
      int i;
      for ( i = 0; i < 12; i++ )
      {
        if ( (setTable[i].bDescrSeq==sFrame.bSeq)&&
                           (setTable[i].bDescr==WPAR_FRA)&&
                           (setTable[i].bReceiveState==bESeq) )
          break;
      }      
      printf ( "Ist Frame gut angekommen (g,v): " );
      do
      {
        scanf ( "%c", &cChar );
      }
      while ( cChar < 'A' );
      switch ( cChar )
      {
        case 'g':
          bEAktState    = setTable[i].bGoodState;
          break;
        case 'v':
          bEAktState    = setTable[i].bBadState;
          break;
      }
      printf ( "Neuer State: %d\n", bEAktState );      
      sFrame.bSeq   = setTable[bEAktState].bDescrSeq;
      sFrame.bMess  = setTable[bEAktState].bDescr;
      sFrame.bText  = 0;
      printf ( "Quittung gesendet (Seq: %d/Mess: %s)\n", sFrame.bSeq,
                             sFrame.bMess==WPAR_ACK?"ACK":"NAK" );
      bESeq         = setTable[bEAktState].bReceiveState;
      bFrameSend    = 0;
      bFrameRequest = 1;
      printf ( "\n" );
      continue;
    }
  }
}

