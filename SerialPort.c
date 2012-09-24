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

#include "SerialPort.h"
#include "RABDef.h"
#include "L1RingBuffer.h"

// Interrupt-Struktur mit Verweis auf SERIALINFO
struct INTRINFO
{
  struct SERIALINFO *psInfo;
};

// Standardm"assig definierte Ports, z.Zt. keine anderen Zul"assig
static struct SERIALINFO sSerialInfo[] = 
{
  {0, PORT_UNKNOWN, 0x3F8, 4, 0, 1, 1, 2}, 
  {1, PORT_UNKNOWN, 0x2F8, 3, 0, 1, 1, 2}
};

// Verweis-Array von IRQ-Nummer zur SERIALINFO-Struktur
static struct INTRINFO  sIntr[14];


static inline unsigned int SerialPort_in (struct SERIALINFO *info, int offset)
{
  return ( inb(info->iPortAddr+offset) );
}

static inline unsigned int SerialPort_inp (struct SERIALINFO *info, int offset)
{
#ifdef CONFIG_SERIAL_NOPAUSE_IO
  return ( inb(info->iPortAddr+offset) );
#else
  return ( inb_p(info->iPortAddr+offset) );
#endif
}

static inline void SerialPort_out (struct SERIALINFO *info, int offset, int value)
{
  outb ( value, info->iPortAddr+offset );
}

static inline void SerialPort_outp (struct SERIALINFO *info, int offset, int value)
{
#ifdef CONFIG_SERIAL_NOPAUSE_IO
  outb ( value, info->iPortAddr+offset );
#else
  outb_p ( value, info->iPortAddr+offset );
#endif
}

static void SerialPort_setBaud ( struct SERIALINFO *info )
{
  int iScratch = SerialPort_inp ( info, UART_LCR );
  SerialPort_outp ( info, UART_LCR, iScratch|UART_LCR_DLAB );
  SerialPort_outp ( info, UART_DLM, info->iDivisor/256 );
  SerialPort_outp ( info, UART_DLL, info->iDivisor%256 );
  SerialPort_outp ( info, UART_LCR, iScratch );
}

static void SerialPort_setLoop ( struct SERIALINFO *info, int iEnable )
{
  int iScratch = SerialPort_inp ( info, UART_MCR );
  if ( iEnable )
    SerialPort_outp ( info, UART_MCR, iScratch|UART_MCR_LOOP );
  else
    SerialPort_outp ( info, UART_MCR, iScratch&(~UART_MCR_LOOP) );  
}

static void SerialPort_setLCR ( struct SERIALINFO *info, int iWert )
{
  SerialPort_outp ( info, UART_LCR, iWert );
}

static void SerialPort_setMCROut2 ( struct SERIALINFO *info, int iEnable )
{
  int iScratch = SerialPort_inp ( info, UART_MCR );
  if ( iEnable )
    SerialPort_outp ( info, UART_MCR, iScratch|UART_MCR_OUT2 );
  else
    SerialPort_outp ( info, UART_MCR, iScratch&~UART_MCR_OUT2 );
}
  
static void SerialPort_setDRInt ( struct SERIALINFO *info, int iEnable )
{
  int iScratch = SerialPort_inp ( info, UART_IER );
  if ( iEnable )
    SerialPort_outp ( info, UART_IER, iScratch|UART_IER_RDI );
  else
    SerialPort_outp ( info, UART_IER, iScratch&~UART_IER_RDI );
}

static void SerialPort_setTHREInt ( struct SERIALINFO *info, int iEnable )
{
  int iScratch = SerialPort_inp ( info, UART_IER );
  if ( iEnable )
    SerialPort_outp ( info, UART_IER, iScratch|UART_IER_THRI );
  else
    SerialPort_outp ( info, UART_IER, iScratch&~UART_IER_THRI );
}
  

static void SerialPort_setFIFO ( struct SERIALINFO *info, int iEnable )
{
  if ( iEnable )
  {
    int iFCR = UART_FCR_ENABLE_FIFO|UART_FCR_CLEAR_RCVR|UART_FCR_CLEAR_XMIT|info->iFIFOTrigger;
    SerialPort_outp ( info, UART_FCR, iFCR );
  }
  else
    SerialPort_outp ( info, UART_FCR, 0 );
}

/*
 * This routine is called by rs_init() to initialize a specific serial
 * port.  It determines what type of UART chip this serial port is
 * using: 8250, 16450, 16550, 16550A.  The important question is
 * whether or not this UART is a 16550A or not, since this will
 * determine whether or not we can use its FIFO features or not.
 */
static void SerialPort_autoConfig (struct SERIALINFO * info)
{
  unsigned char status1, status2, scratch, scratch2;
  unsigned port = info->iChipType;
  unsigned long flags;

  info->iChipType = PORT_UNKNOWN;
//  if ( !port )
//    return;

  save_flags ( flags );
  cli();
	
  /*
   * Do a simple existence test first; if we fail this, there's
   * no point trying anything else.
   *
   * 0x80 is used as a nonsense port to prevent against false
   * positives due to ISA bus float.  The assumption is that
   * 0x80 is a non-existent port; which should be safe since
   * include/asm/io.h also makes this assumption.
   */
  scratch = SerialPort_inp ( info, UART_IER );
  SerialPort_outp ( info, UART_IER, 0 );
  outb ( 0xff, 0x080 );
  scratch2 = SerialPort_inp ( info, UART_IER );
  SerialPort_outp ( info, UART_IER, scratch );
  if ( scratch2 ) 
  {
    printk ( "autoconfig: We failed; there's nothing here.\n" );
    restore_flags ( flags );
    return;		/* We failed; there's nothing here */
  }

  scratch2 = SerialPort_in ( info, UART_LCR );
  SerialPort_outp ( info, UART_LCR, scratch2|UART_LCR_DLAB );
  SerialPort_outp ( info, UART_EFR, 0 );	/* EFR is the same as FCR */
  SerialPort_outp ( info, UART_LCR, scratch2 );
  SerialPort_outp ( info, UART_FCR, UART_FCR_ENABLE_FIFO );
  scratch = SerialPort_in ( info, UART_IIR ) >> 6;
  info->iFIFOSize = 1;
//  printk ( "autoconfig: Scratch: %d\n", scratch );
  switch (scratch)
  {
    case 0:
      info->iChipType = PORT_16450;
      break;
    case 1:
      info->iChipType = PORT_UNKNOWN;
      break;
    case 2:
      info->iChipType = PORT_16550;
      break;
    case 3:
      SerialPort_outp ( info, UART_LCR, scratch2|UART_LCR_DLAB );
      if ( SerialPort_in(info, UART_EFR) == 0 )
      {
        // It is an 16C660
        info->iChipType = PORT_16650;
        info->iFIFOSize = 32;
      }
      else
      {
        info->iChipType = PORT_16550A;
        info->iFIFOSize = 16;
      }
      SerialPort_outp ( info, UART_LCR, scratch2 );
      break;
  }
  if ( info->iChipType == PORT_16450 )
  {
    scratch = SerialPort_in ( info, UART_SCR );
    SerialPort_outp ( info, UART_SCR, 0xa5 );
    status1 = SerialPort_in ( info, UART_SCR );
    SerialPort_outp ( info, UART_SCR, 0x5a );
    status2 = SerialPort_in ( info, UART_SCR );
    SerialPort_outp ( info, UART_SCR, scratch );
    if ( (status1!=0xa5) || (status2!=0x5a) )
      info->iChipType = PORT_8250;
  }
  request_region ( info->iPortAddr, 8, "serial(auto)" );

  /*
   * Reset the UART.
   */
  SerialPort_outp ( info, UART_MCR, 0x00 );
  SerialPort_outp ( info, UART_FCR, (UART_FCR_CLEAR_RCVR|UART_FCR_CLEAR_XMIT) );
  (void)SerialPort_in ( info, UART_RX );
  restore_flags ( flags );
}

static void RabbiT_interrupt (int irq, void *dev_id, struct pt_regs * regs)
{
  int iIIR;
  struct SERIALINFO *info;
  info = sIntr[irq].psInfo;
  if ( info == NULL )
    return;
//  printk ( "Interrupt: /dev/rab%d\n", info->iPortType );
  iIIR = SerialPort_inp ( info, UART_IIR );    
  if ( (iIIR&15) == UART_IIR_RDI )
  {
//    printk ( " read chars from line\n" );
    while ( SerialPort_inp(info, UART_LSR)&0x01 )
    {
      unsigned char ch = SerialPort_inp(info, UART_RX);
//      printk ( "/dev/rab%d: Zeichen von Line gelesen (%c)\n", info->iPortType, ch );
      L1RingBuffer_insertChar ( ch, (info->iPortType*2+L1RINGBUFFER_READ), FALSE );	// Zeichen in Ringbuffer einfuegen
    }
    return;
  }
  if ( (iIIR&15) == UART_IIR_THRI )
  {
    int           i;
    unsigned char ucItem;
//    printk ( " write chars to line\n" );
    if ( L1RingBuffer_charInBuffer(info->iPortType*2+L1RINGBUFFER_WRITE) == 0 )
    {
//      printk ( "/dev/rab%d: Write-Interrupt ausgeschaltet\n", info->iPortType );
      SerialPort_setTHREInt ( info, 0 );
      return;
    }
//    printk ( "iFIFOSize %d\n", info->iFIFOSize );
    for ( i = 0; i < info->iFIFOSize; i++ )
    {
      if ( L1RingBuffer_takeChar(&ucItem, (info->iPortType*2+L1RINGBUFFER_WRITE), FALSE ) != 0 )
        break;
//      printk ( "/dev/rab%d: Zeichen geschrieben (%c)\n", info->iPortType, ucItem );
      SerialPort_outp ( info, UART_TX, ucItem );
    }
    return;
  } 
}


void SerialPort_start ( int _iPort )
{
  // Clear Register
  (void) SerialPort_inp ( &sSerialInfo[_iPort], UART_RX );
  (void) SerialPort_inp ( &sSerialInfo[_iPort], UART_IIR );
  (void) SerialPort_inp ( &sSerialInfo[_iPort], UART_MSR );
  SerialPort_setBaud ( &sSerialInfo[_iPort] );	// Baudrate einstellen
//  descrIER ( &sSerialInfo[_iPort] );
//  getBaud ( &sSerialInfo[_iPort] );
  if ( (sSerialInfo[_iPort].iChipType==PORT_16550A)
              && (sRabbiTBusy[_iPort].iProtocol!=RABBIT_PROTOCOL_CHAT) )
    SerialPort_setFIFO ( &sSerialInfo[_iPort], 1 );
  else
  {
    sSerialInfo[_iPort].iFIFOSize = 1;
    SerialPort_setFIFO ( &sSerialInfo[_iPort], 0 );
  } 
  SerialPort_setLCR ( &sSerialInfo[_iPort], 0x1B );	// Parity usw. initialisieren
  SerialPort_setMCROut2 ( &sSerialInfo[_iPort], 1 );  
  SerialPort_setDRInt ( &sSerialInfo[_iPort], 1 );
}  

void SerialPort_init ( void )
{
  int i;
  for ( i = 0; i < 14; i++ )
    sIntr[i].psInfo = NULL;
  for ( i = 0; i < 2; i++ )
  {
    SerialPort_autoConfig ( &sSerialInfo[i] );
    printk ( "COM%d at 0x%04x", i+1, sSerialInfo[i].iPortAddr );
//    printk ( "\nChipType: %d\n", sSerialInfo[i].iChipType );
    switch ( sSerialInfo[i].iChipType )
    {
      case PORT_8250:
        printk(" is a 8250\n");
        break;
      case PORT_16450:
        printk(" is a 16450\n");
        break;
      case PORT_16550:
        printk(" is a 16550\n");
        break;
      case PORT_16550A:
        printk(" is a 16550A\n");
        sSerialInfo[i].iFIFOTrigger = UART_FCR_TRIGGER_1;
        break;
      default:
        printk(" is ein default?!?!?\n");
        break;
    }
    if ( request_irq ( sSerialInfo[i].iIRQ, RabbiT_interrupt, SA_INTERRUPT, "RabbiT", NULL) )
      printk ( "Error in Interrupt (IRQ %d).\n", sSerialInfo[i].iIRQ );
    sIntr[sSerialInfo[i].iIRQ].psInfo = &sSerialInfo[i];
  }
}

void SerialPort_release ( int _iPort )
{
  SerialPort_setMCROut2 ( &sSerialInfo[_iPort], 0 );
  SerialPort_setDRInt ( &sSerialInfo[_iPort], 0 );
  SerialPort_setTHREInt ( &sSerialInfo[_iPort], 0 );
}

void SerialPort_writeIntOn ( int _iPort )
{
//  printk ( "Write Interrupt eingeschaltet (RAB%d)\n", _iPort );
  SerialPort_setTHREInt ( &sSerialInfo[_iPort], 1 );
}
