//***************************************************************************
//***                         Project: Wotan                              ***
//***                 PAR - Protokoll Implementierung                     ***
//***************************************************************************
//*          Written (w) 1997 by Stephan Toggweiler (RabbiT)                *
//*-------------------------------------------------------------------------*
//*                    Version 1.00.001, 03.01.1997                         *
//***************************************************************************
//*  Version 1.00.001, 03.01.97:                                            *
//*   - Prototyp-Version, "ubernommen von SerialTimer.c                     *
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

static BYTE  bPiggyBack[2][2][2];

#define WPAR_PIGGYBACK_NONE    0x00
#define WPAR_PIGGYBACK_ACK     0x01
#define WPAR_PIGGYBACK_NAK     0x02

void PiggyBack_init ( void )
{
  

void PiggyBack_set ( int _iPort, BYTE _bMsg, BYTE _bSeq )
{
  cli ();
  if ( bPiggyBack[0][0] != WPAR_PIGGYBACK_NONE )
  {
    bPiggyBack[_iPort][1][0] = _bMsg;
    bPiggyBack[1][1] = _bSeq;
  }
  else
  {
    bPiggyBack[0][0] = _bMsg;
    bPiggyBack[0][1] = _bSeq;
  }
  sti ();
}

static int isPiggyBack ( int _iPort )
{
  cli ();
  return ( bPiggyBack[0][0] );
  sti ();
}

static void getPiggyBack ( int _iPort, BYTE *_pbMsg, BYTE *_pbSeq )
{
  cli ();
  *_pbMsg = bPiggyBack[0][0];
  *_pbSeq = bPiggyBack[0][1];
  if ( bPiggyBack[1][0] != WPAR_PIGGYBACK_NONE )
  {
    bPiggyBack[0][0] = bPiggyBack[1][0];
    bPiggyBack[0][1] = bPiggyBack[1][1];
    bPiggyBack[1][0] = WPAR_PIGGYBACK_NONE;
  }
  sti ();
}
