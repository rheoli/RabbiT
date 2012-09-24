/*  
 *  linux/drivers/char/RabbiT.c
 *
 *  Written (w) 1996 by RabbiT (St.Toggweiler)
 *
 *   Modul zum ansprechen der seriellen Schnittstelle mit integriertem
 *   Terminalprogramm f"ur Zeichen und Frame Uebertragung.
 *
 *    autoconfig() "ubernommen von Linux-Kernel aus dem File serial.c
 *    daher Copyright (c) 1991, 1992 Linus Torvalds et all.
 *
 */

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
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <asm/bitops.h>

#define RING_MAX  10000
#define DEV_MAX       2

static unsigned char uRPuffer[DEV_MAX][RING_MAX];
static unsigned char uWPuffer[DEV_MAX][RING_MAX];
static int           iCharInRPuffer[DEV_MAX];
static int           iCharInWPuffer[DEV_MAX];
static int           iRNextTake[DEV_MAX];
static int           iRNextInsert[DEV_MAX];
static int           iWNextTake[DEV_MAX];
static int           iWNextInsert[DEV_MAX];

#define RABBIT_DEVPORT_EMPTY 255

struct serial_info
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

struct int_info
{
  struct serial_info *info;
};

static struct serial_info  sinfo[] = {
  {0, PORT_UNKNOWN, 0x3F8, 4, 0, 1, 1, 2}, 
  {1, PORT_UNKNOWN, 0x2F8, 3, 0, 1, 1, 2}
};

static struct int_info  sint[14];
static char             *pszRabbiTName    = "RabbiT network driver";
static char             *pszRabbiTVersion = "Version 1.00Concept";

static void setTHREInt ( struct serial_info *info, int iEnable );
static void descrIER ( struct serial_info *info );
static void getBaud ( struct serial_info *info );
static void setBaud ( struct serial_info *info );
static void enableLoop ( struct serial_info *info );
static void disableLoop ( struct serial_info *info );
static void setLCR ( struct serial_info *info, int iWert );
static void setMCROut2 ( struct serial_info *info, int iEnable );
static void setDRInt ( struct serial_info *info, int iEnable );
static void sendChar ( struct serial_info *info, char cZeichen );
static void disableFIFO ( struct serial_info *info );
static void enableFIFO ( struct serial_info *info );
static void descrLSR ( struct serial_info *info );
static void descrIIR ( struct serial_info *info, int iCheck );
static void getChar ( struct serial_info *info );
static inline unsigned int serial_in(struct serial_info *info, int offset);
static inline unsigned int serial_inp(struct serial_info *info, int offset);
static inline void serial_out(struct serial_info *info, int offset, int value);
static inline void serial_outp(struct serial_info *info, int offset, int value );

static void rabbit_timer ( void )
{
  printk ( "RabbiT-Timer aufgerufen: %d\n", jiffies );
  timer_table[RABBIT_TIMER].expires = jiffies + 400;
  timer_active |= 1 << RABBIT_TIMER;
} 

static void initRingPuffer ( void )
{
  int i;
  for ( i = 0; i < 2; i++ )
  {
    iCharInRPuffer[i] = 0;
    iRNextTake[i]     = 0;
    iRNextInsert[i]   = 0;
    iCharInWPuffer[i] = 0;
    iWNextTake[i]     = 0;
    iWNextInsert[i]   = 0;
  }
}

static int insertRChar ( unsigned char _uItem, unsigned int _uiMinor )
{
  if ( iCharInRPuffer[_uiMinor] > (RING_MAX-1) )
    return ( -1 );
//  printk ( "/dev/rab%d: insertRChar\n", _uiMinor );
  uRPuffer[_uiMinor][iRNextInsert[_uiMinor]] = _uItem;
  iRNextInsert[_uiMinor] = (iRNextInsert[_uiMinor]+1)%RING_MAX;
  iCharInRPuffer[_uiMinor]++;
  return ( 0 );
}

static int takeRChar ( unsigned char *_puItem, unsigned int _uiMinor )
{
  if ( iCharInRPuffer[_uiMinor] == 0 )
    return ( -1 );
//  printk ( "/dev/rab%d: takeRChar\n", _uiMinor );
  cli();
  *_puItem = uRPuffer[_uiMinor][iRNextTake[_uiMinor]];
  iRNextTake[_uiMinor] = (iRNextTake[_uiMinor]+1)%RING_MAX;
  iCharInRPuffer[_uiMinor]--;
  sti();
  return ( 0 );
}

static int prozentRFull ( unsigned int _uiMinor )
{
  return ( (iCharInRPuffer[_uiMinor]*100)/RING_MAX );
}

static int numberInRPuffer ( unsigned int _uiMinor )
{
  return ( iCharInRPuffer[_uiMinor] );
}  

static int insertWChar ( unsigned char _uItem, unsigned int _uiMinor )
{
  if ( iCharInWPuffer[_uiMinor] > (RING_MAX-1) )
    return ( -1 );
//  printk ( "/dev/rab%d: insertWChar\n", _uiMinor );
  cli();
  uWPuffer[_uiMinor][iWNextInsert[_uiMinor]] = _uItem;
  iWNextInsert[_uiMinor] = (iWNextInsert[_uiMinor]+1)%RING_MAX;
  iCharInWPuffer[_uiMinor]++;
  sti();
  return ( 0 );
}

static int takeWChar ( unsigned char *_puItem, unsigned int _uiMinor )
{
  if ( iCharInWPuffer[_uiMinor] == 0 )
    return ( -1 );
//  printk ( "/dev/rab%d: takeWChar\n", _uiMinor );
  *_puItem = uWPuffer[_uiMinor][iWNextTake[_uiMinor]];
  iWNextTake[_uiMinor] = (iWNextTake[_uiMinor]+1)%RING_MAX;
  iCharInWPuffer[_uiMinor]--;
  return ( 0 );
}

static int prozentWEmty ( unsigned int _uiMinor )
{
  return ( (iCharInWPuffer[_uiMinor]*100)/RING_MAX );
}

static int numberInWPuffer ( unsigned int _uiMinor )
{
  return ( iCharInWPuffer[_uiMinor] );
}  

struct RabbiT_device
{
  int          iBusy;
  int          iPortType;
  int          iProtocolType;
};

#define RABBIT_PORTTYPE_NONE       0x00
#define RABBIT_PORTTYPE_SERIAL     0x01
#define RABBIT_PORTTYPE_ETHERNET   0x02 
#define RABBIT_SERIAL_SPEED        0x03

static struct RabbiT_device sRabbiTBusy[] =
{
  {0, RABBIT_PORTTYPE_SERIAL, 0},
  {0, RABBIT_PORTTYPE_SERIAL, 0}
};

static int RabbiT_open ( struct inode *inode, struct file *file )
{
  unsigned int   uiMinor = MINOR(inode->i_rdev);
  unsigned short usFlags = inode->i_flags;
  if ( sRabbiTBusy[uiMinor].iBusy )
    return ( -EBUSY );
//  printk ( "/dev/rab%d: open device\n", uiMinor );
  if ( usFlags&RABBIT_PORTTYPE_ETHERNET )
  {
    if ( uiMinor == 1 )
      return ( -EBUSY );
    sRabbiTBusy[uiMinor].iPortType = RABBIT_PORTTYPE_ETHERNET;
  }
  sRabbiTBusy[uiMinor].iBusy = 1;
  if ( sRabbiTBusy[uiMinor].iPortType == RABBIT_PORTTYPE_SERIAL )
  {
    // Clear Register
    (void) serial_inp ( &sinfo[0], UART_RX );
    (void) serial_inp ( &sinfo[0], UART_IIR );
    (void) serial_inp ( &sinfo[0], UART_MSR );
    setBaud ( &sinfo[uiMinor] );	// Baudrate einstellen
    descrIER ( &sinfo[uiMinor] );
    getBaud ( &sinfo[uiMinor] );
    if ( (sinfo[uiMinor].iChipType==PORT_16550A) && (sRabbiTBusy[uiMinor].iProtocolType!=0) )
      enableFIFO ( &sinfo[uiMinor] );
    else
    {
      sinfo[uiMinor].iFIFOSize = 1;
      disableFIFO ( &sinfo[uiMinor] );
    } 
    setLCR ( &sinfo[uiMinor], 0x1B );	// Parity usw. initialisieren
    setMCROut2 ( &sinfo[uiMinor], 1 );  
    setDRInt ( &sinfo[uiMinor], 1 );
  }  
  return ( 0 );
}

static int RabbiT_ioctl ( struct inode *inode, struct file *file,
                           unsigned int cmd, unsigned long arg )
{
  unsigned int uiMinor = MINOR(inode->i_rdev);
  switch ( cmd )
  {
    case RABBIT_SERIAL_SPEED:
      if ( sRabbiTBusy[uiMinor].iPortType == RABBIT_PORTTYPE_ETHERNET )
        return ( -EINVAL );
      sinfo[uiMinor].iDivisor = arg;      
      return ( 0 );
  }
//  printk ( "RabbiT_Network*ioctl: No command defined.\n" );
  return ( -EINVAL );
}
      
static int RabbiT_read ( struct inode *inode, struct file *file, char *buffer, int count )
{
  unsigned int  uiMinor = MINOR(inode->i_rdev);
  unsigned char cZeichen;
  int           i       = 0;
//  printk ( "/dev/rab%d: read device\n", uiMinor );
  while ( i < count )
  {
    if ( takeRChar(&cZeichen,uiMinor) != 0 )
      return ( i );
    put_user ( cZeichen, buffer );
    buffer++;
    i++;
  } 
  return ( i );
}

static int RabbiT_write ( struct inode *inode, struct file *file, const char *buffer, int count )
{
  unsigned int uiMinor = MINOR(inode->i_rdev);
  int          i       = 0;
//  printk ( "/dev/rab%d: write device\n", uiMinor );
  while ( i < count )
  {
    if ( insertWChar(get_user(buffer), uiMinor) != 0 )
      return ( i );
    i++;
    buffer++;
  }
  setTHREInt ( &sinfo[uiMinor], 1 );  
  return ( i );
}

  
static void RabbiT_release ( struct inode *inode, struct file *file )
{
  unsigned int uiMinor = MINOR(inode->i_rdev);
//  printk ( "/dev/rab%d: close device\n", uiMinor );
  if ( sRabbiTBusy[uiMinor].iPortType == RABBIT_PORTTYPE_SERIAL )
  {
    setMCROut2 ( &sinfo[uiMinor], 0 );  
    setDRInt ( &sinfo[uiMinor], 0 );
    setTHREInt ( &sinfo[uiMinor], 0 );
  }
  sRabbiTBusy[uiMinor].iPortType = RABBIT_PORTTYPE_SERIAL;
  sRabbiTBusy[uiMinor].iBusy = 0;
}

static struct file_operations RabbiT_fops = {
  NULL,			/* RabbiT_seek    */
  RabbiT_read,		/* RabbiT_read    */
  RabbiT_write,		/* RabbiT_write   */
  NULL,			/* RabbiT_readdir */
  NULL,			/* RabbiT_select  */
  RabbiT_ioctl,		/* RabbiT_ioctl   */
  NULL,			/* RabbiT_mmap    */
  RabbiT_open,		/* RabbiT_open    */
  RabbiT_release	/* RabbiT_release */
};
 
static inline unsigned int serial_in(struct serial_info *info, int offset)
{
  return ( inb(info->iPortAddr+offset) );
}

static inline unsigned int serial_inp(struct serial_info *info, int offset)
{
#ifdef CONFIG_SERIAL_NOPAUSE_IO
  return ( inb(info->iPortAddr+offset) );
#else
  return ( inb_p(info->iPortAddr+offset) );
#endif
}

static inline void serial_out(struct serial_info *info, int offset, int value)
{
  outb ( value, info->iPortAddr+offset );
}

static inline void serial_outp(struct serial_info *info, int offset, int value)
{
#ifdef CONFIG_SERIAL_NOPAUSE_IO
  outb ( value, info->iPortAddr+offset );
#else
  outb_p ( value, info->iPortAddr+offset );
#endif
}

/*
 * This routine is called by rs_init() to initialize a specific serial
 * port.  It determines what type of UART chip this serial port is
 * using: 8250, 16450, 16550, 16550A.  The important question is
 * whether or not this UART is a 16550A or not, since this will
 * determine whether or not we can use its FIFO features or not.
 */
static void autoconfig(struct serial_info * info)
{
  unsigned char status1, status2, scratch, scratch2;
  unsigned port = info->iChipType;
  unsigned long flags;

  info->iChipType = PORT_UNKNOWN;
  if ( !port )
    return;

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
  scratch = serial_inp ( info, UART_IER );
  serial_outp ( info, UART_IER, 0 );
  outb ( 0xff, 0x080 );
  scratch2 = serial_inp ( info, UART_IER );
  serial_outp ( info, UART_IER, scratch );
  if ( scratch2 ) 
  {
    restore_flags ( flags );
    return;		/* We failed; there's nothing here */
  }

  scratch2 = serial_in ( info, UART_LCR );
  serial_outp ( info, UART_LCR, scratch2|UART_LCR_DLAB );
  serial_outp ( info, UART_EFR, 0 );	/* EFR is the same as FCR */
  serial_outp ( info, UART_LCR, scratch2 );
  serial_outp ( info, UART_FCR, UART_FCR_ENABLE_FIFO );
  scratch = serial_in ( info, UART_IIR ) >> 6;
  info->iFIFOSize = 1;
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
      serial_outp ( info, UART_LCR, scratch2|UART_LCR_DLAB );
      if ( serial_in(info, UART_EFR) == 0 )
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
      serial_outp ( info, UART_LCR, scratch2 );
      break;
  }
  if ( info->iChipType == PORT_16450 )
  {
    scratch = serial_in ( info, UART_SCR );
    serial_outp ( info, UART_SCR, 0xa5 );
    status1 = serial_in ( info, UART_SCR );
    serial_outp ( info, UART_SCR, 0x5a );
    status2 = serial_in ( info, UART_SCR );
    serial_outp ( info, UART_SCR, scratch );
    if ( (status1!=0xa5) || (status2!=0x5a) )
      info->iChipType = PORT_8250;
  }
  request_region ( info->iPortAddr, 8, "serial(auto)" );

  /*
   * Reset the UART.
   */
  serial_outp ( info, UART_MCR, 0x00 );
  serial_outp ( info, UART_FCR, (UART_FCR_CLEAR_RCVR|UART_FCR_CLEAR_XMIT) );
  (void)serial_in ( info, UART_RX );
  restore_flags ( flags );
}

static void RabbiT_interrupt (int irq, void *dev_id, struct pt_regs * regs)
{
  int iIIR;
  struct serial_info *info;
  info = sint[irq].info;
  if ( info == NULL )
    return;
//  printk ( "/dev/rab%d: Interrupt:", info->iPortType );
  iIIR = serial_inp ( info, UART_IIR );    
  if ( (iIIR&15) == UART_IIR_RDI )
  {
//    printk ( " read chars from line\n" );
    while ( serial_inp(info, UART_LSR)&0x01 )
    {
      unsigned char ch = serial_inp(info, UART_RX);
//      printk ( "/dev/rab%d: Zeichen von Line gelesen (%c)\n", info->iPortType, ch );
      insertRChar ( ch, info->iPortType );	// Zeichen in Ringbuffer einfuegen
    }
    return;
  }
  if ( (iIIR&15) == UART_IIR_THRI )
  {
    int           i;
    unsigned char ucItem;
//    printk ( " write chars to line\n" );
    if ( numberInWPuffer(info->iPortType) == 0 )
    {
      setTHREInt ( info, 0 );
      return;
    }
    printk ( "iFIFOSize %d\n", info->iFIFOSize );
    for ( i = 0; i < info->iFIFOSize; i++ )
    {
      if ( takeWChar(&ucItem, info->iPortType) != 0 )
        break;
//      printk ( "/dev/rab%d: Zeichen geschrieben (%c)\n", info->iPortType, ucItem );
      serial_outp ( info, UART_TX, ucItem );
    }
    return;
  } 
}

static void descrIER ( struct serial_info *info )
{
  int iIER = serial_inp ( info, UART_IER );
  
  if ( (iIER&0x01) )
    printk ( "Data Ready interrupt enabled\n" );
  if ( (iIER&0x02) )
    printk ( "THR Empty interrupt enabled\n" );
  if ( (iIER&0x04) )
    printk ( "Status interrupt enabled\n" );
  if ( (iIER&0x08) )
    printk ( "Modem status interrupt enabled\n" );
}

static void getBaud ( struct serial_info *info )
{
  int iScratch = serial_inp ( info, UART_LCR );
  int iBaud;
  serial_outp ( info, UART_LCR, iScratch|UART_LCR_DLAB );
  iBaud = serial_inp ( info, UART_DLM ) * 256;
  iBaud += serial_inp ( info, UART_DLL );
//  printk ( "/dev/rab%d: Aktuelle Baudrate: %d\n", info->iPortType, iBaud );
  serial_outp ( info, UART_LCR, iScratch );
}

static void setBaud ( struct serial_info *info )
{
  int iScratch = serial_inp ( info, UART_LCR );
  serial_outp ( info, UART_LCR, iScratch|UART_LCR_DLAB );
  serial_outp ( info, UART_DLM, info->iDivisor/256 );
  serial_outp ( info, UART_DLL, info->iDivisor%256 );
  serial_outp ( info, UART_LCR, iScratch );
//  printk ( "/dev/rab%d: Baudrate neu eingestellt (%d).\n", info->iPortType, info->iDivisor );
}

static void enableLoop ( struct serial_info *info )
{
  int iScratch = serial_inp ( info, UART_MCR );
  serial_outp ( info, UART_MCR, iScratch|UART_MCR_LOOP );
  printk ( "Loop eingeschaltet\n" );
}

static void disableLoop ( struct serial_info *info )
{
  int iScratch = serial_inp ( info, UART_MCR );
  if ( (iScratch&UART_MCR_LOOP) )
    printk ( "Loop ist eingeschaltet.\n" );
  serial_outp ( info, UART_MCR, iScratch&(~UART_MCR_LOOP) );
  printk ( "Loop ausgeschaltet\n" );
}

static void setLCR ( struct serial_info *info, int iWert )
{
  serial_outp ( info, UART_LCR, iWert );
}

static void setMCROut2 ( struct serial_info *info, int iEnable )
{
  int iScratch = serial_inp ( info, UART_MCR );
  if ( iEnable )
  {
    serial_outp ( info, UART_MCR, iScratch|UART_MCR_OUT2 );
//    printk ( "/dev/rab%d: Out2 Interrupt eingeschaltet.\n", info->iPortType );
  }
  else
  {
    serial_outp ( info, UART_MCR, iScratch&~UART_MCR_OUT2 );
//    printk ( "/dev/rab%d: Out2 Interrupt ausgeschaltet.\n", info->iPortType );
  }
}
  
static void setDRInt ( struct serial_info *info, int iEnable )
{
  int iScratch = serial_inp ( info, UART_IER );
  if ( iEnable )
  {
    serial_outp ( info, UART_IER, iScratch|UART_IER_RDI );
//    printk ( "/dev/rab%d: Data Ready Interrupt eingeschaltet.\n", info->iPortType );
  }
  else
  {
    serial_outp ( info, UART_IER, iScratch&~UART_IER_RDI );
//    printk ( "/dev/rab%d: Data Ready Interrupt ausgeschaltet.\n", info->iPortType );
  }
}

static void setTHREInt ( struct serial_info *info, int iEnable )
{
  int iScratch = serial_inp ( info, UART_IER );
  if ( iEnable )
  {
    serial_outp ( info, UART_IER, iScratch|UART_IER_THRI );
//    printk ( "/dev/rab%d: THR Empty Interrupt eingeschaltet.\n", info->iPortType );
  }
  else
  {
    serial_outp ( info, UART_IER, iScratch&~UART_IER_THRI );
//    printk ( "/dev/rab%d: THR Empty Interrupt ausgeschaltet.\n", info->iPortType );
  }
}
  
static void sendChar ( struct serial_info *info, char cZeichen )
{
  while ( (serial_inp(info, UART_LSR)&0x20) == 0 );
  serial_outp ( info, UART_TX, cZeichen );
//  printk ( "Zeichen gesendet (%c).\n", cZeichen );
}

static void getChar ( struct serial_info *info )
{
  int x;
  if ( serial_inp(info, UART_LSR)&0x01 )
  {
    x = serial_inp(info, UART_RX);
    printk ( "Eingelesenes Zeichen: %d (%c).\n", x, x );
  }
  else
    printk ( "kein Zeichen gelesen.\n" );
}

static void disableFIFO ( struct serial_info *info )
{
  serial_outp ( info, UART_FCR, 0 );
//  printk ( "FIFO ausgeschaltet.\n" );
}

static void enableFIFO ( struct serial_info *info )
{
  int iFCR = UART_FCR_ENABLE_FIFO|UART_FCR_CLEAR_RCVR|UART_FCR_CLEAR_XMIT|info->iFIFOTrigger;
  serial_outp ( info, UART_FCR, iFCR );
//  printk ( "/dev/rab%d: FIFO eingeschaltet.\n", info->iPortType );
}
  
static void descrLSR ( struct serial_info *info )
{
  int iScratch = serial_inp ( info, UART_LSR );
  if ( iScratch&0x01 )
    printk ( "Data Ready\n" );
  if ( iScratch&0x02 )
    printk ( "Overrun Error\n" );
  if ( iScratch&0x04 )
    printk ( "Parity Error\n" );
  if ( iScratch&0x08 )
    printk ( "Framing Error\n" );
  if ( iScratch&0x10 )
    printk ( "Break Indicator\n" );
  if ( iScratch&0x20 )
    printk ( "Transmitter Holding Register Empty\n" );
  if ( iScratch&0x40 )
    printk ( "Transmitter Empty\n" );
  if ( iScratch&0x80 )
    printk ( "RX FIFO Error (16550+ only)\n" );
}
  
static void descrIIR ( struct serial_info *info, int iCheck )
{
  int iIIR = serial_inp ( info, UART_IIR );
  int iBit0, iBit1, iBit2, iBit3;
  
  if ( iCheck )
  {
    if ( ((iIIR&0x40)==0x40) && ((iIIR&0x80)==0x80) )
      printk ( "Dieser Baustein ist ein 16550A\n" );
    if ( ((iIIR&0x40)==0) && ((iIIR&0x80)==0x80) )
      printk ( "Dieser Baustein ist ein 16550\n" );
    return;
  }
  
  iBit0 = ((iIIR&0x01)==0x01);
  iBit1 = ((iIIR&0x02)==0x02);
  iBit2 = ((iIIR&0x04)==0x04);
  iBit3 = ((iIIR&0x08)==0x08);
  
//  printk ( "Aktueller Zustand IIR: %d %d %d %d\n", iBit3, iBit2, iBit1, iBit0 );
  
  if ( (iBit0&&!iBit1) && (!iBit2&&!iBit3) )
    printk ( "kein Interrupt vorhanden.\n" );
  if ( (!iBit0&&iBit1) && (iBit2&&!iBit3) )
    printk ( "OE, PE, PE oder BI veraendert.\n" );
  if ( (!iBit0&&!iBit1) && (iBit2&&!iBit3) )
    printk ( "Zeichen von RBR auslesen bitte.\n " );
  if ( (!iBit0&&!iBit1) && (iBit2&&iBit3) )
    printk ( "FIFO-Aktionsprobleme.\n " );
  if ( (!iBit0&&iBit1) && (!iBit2&&!iBit3) )
    printk ( "Schreibe nach THR bitte.\n " );
  if ( (!iBit0&&!iBit1) && (!iBit2&&!iBit3) )
    printk ( "Detaflag von Modemsteuerungs.\n" );
}

void RabbiT_init ( void )
{
  int y;
  printk ( "Init: %s %s\n", pszRabbiTName, pszRabbiTVersion );
  initRingPuffer ();
  for ( y = 0; y < 14; y++ )
    sint[y].info = NULL;
  for ( y = 0; y < 2; y++ )
  {
    autoconfig ( &sinfo[y] );
    printk( "/dev/rab%d at 0x%04x", y+1, sinfo[y].iPortAddr );
    switch (sinfo[y].iChipType)
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
        sinfo[0].iFIFOTrigger = UART_FCR_TRIGGER_1;
        break;
      default:
        printk("\n"); break;
    }
    if ( request_irq ( sinfo[y].iIRQ, RabbiT_interrupt, SA_INTERRUPT, "RabbiT", NULL) )
      printk ( "Error in Interrupt.\n" );
    sint[sinfo[y].iIRQ].info = &sinfo[y];      
  }
  if ( register_chrdev(60, "rab", &RabbiT_fops) )
  {
    printk ( "RabbiT: unable to get major %d\n", 60 );
    return;
  }
  
  timer_table[RABBIT_TIMER].fn      = rabbit_timer;
  timer_table[RABBIT_TIMER].expires = jiffies + 400;
  timer_active |= 1 << RABBIT_TIMER;
}
