//***************************************************************************
//***                 PAR - Protokoll Implementierung                     ***
//***                         Project: WotanPAR                           ***
//***************************************************************************
//*                     Written (w) 1996 by RabbiT                          *
//*-------------------------------------------------------------------------*
//*                    Version 1.00.002, 18.12.1996                         *
//***************************************************************************
//*  Version 1.00.001, 15.12.96:                                            *
//*   - Prototyp-Version                                                    *
//*  Version 1.00.002, 18.12.96:                                            *
//*   - Device-Driver RAB?                                                  *
//*   - Trennung zwischen Layer 1 und Layer 2                               *
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

#include "L1RingBuffer.h"
#include "L2RingBuffer.h"
#include "SerialPort.h"
#include "RABDef.h"

//***************************  Defnitionen  ***************************

#define RABBIT_DEVPORT_EMPTY 255

// Netzwerktype (kann nur beim Oeffnen definiert werden, wird nicht von
// ioctl unterst"utzt)
#define RABBIT_PORTTYPE_NONE       0x0000
#define RABBIT_PORTTYPE_SERIAL     0x1000
#define RABBIT_PORTTYPE_ETHERNET   0x2000 

// Geschwindigkeit "uber ioctl() einstellen
#define RABBIT_SET_SERIAL_SPEED    0x0001
#define RABBIT_SET_PROTOCOLTYPE    0x0002

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


//*****************************  Variablen  ***************************

// Defaultwerte f"ur Schnittstelle
struct RabbiT_device sRabbiTBusy[] =
{
  {0, RABBIT_PORTTYPE_SERIAL, 0},
  {0, RABBIT_PORTTYPE_SERIAL, 0}
};

// Auszugebende Information zur Device
static char             *pszRabbiTName    = "RabbiT network driver";
static char             *pszRabbiTVersion = "Version 1.00Concept";

static int RabbiT_open ( struct inode *inode, struct file *file )
{
  unsigned int   uiMinor = MINOR(inode->i_rdev);
  unsigned short usFlags = inode->i_flags;
  if ( sRabbiTBusy[uiMinor].iBusy )
    return ( -EBUSY );
  if ( (usFlags&RABBIT_PORTTYPE_ETHERNET) == RABBIT_PORTTYPE_ETHERNET )
  {
    printk ( "RabbiT_open: ETHERNET gewaehlt.\n" );
    if ( uiMinor == 1 )
      return ( -EBUSY );
    sRabbiTBusy[uiMinor].iPortType = RABBIT_PORTTYPE_ETHERNET;
    return ( 0 );
  }
  if ( sRabbiTBusy[uiMinor].iPortType == RABBIT_PORTTYPE_SERIAL )
  {
    SerialPort_start ( uiMinor );
  }  
  sRabbiTBusy[uiMinor].iBusy = 1;  
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
  return ( -EINVAL );
}
      
static int RabbiT_read ( struct inode *inode, struct file *file, char *buffer, int count )
{
  unsigned int  uiMinor = MINOR(inode->i_rdev);
  unsigned char cZeichen;
  int           i       = 0;
  while ( i < count )
  {
    if ( L2RingBuffer_takeChar(&cZeichen,(uiMinor*2+1)) != 0 )
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

  while ( i < count )
  {
    if ( L2RingBuffer_insertChar(get_user(buffer), (uiMinor*2+1)) != 0 )
      return ( i );
    i++;
    buffer++;
  }
  return ( i );
}

  
static void RabbiT_release ( struct inode *inode, struct file *file )
{
  unsigned int uiMinor = MINOR(inode->i_rdev);

  if ( sRabbiTBusy[uiMinor].iPortType == RABBIT_PORTTYPE_SERIAL )
    SerialPort_release ( uiMinor );
  
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

void RabbiT_init ( void )
{
  int y;
  printk ( "Init: %s %s\n", pszRabbiTName, pszRabbiTVersion );
  L1RingBuffer_init ();
  L2RingBuffer_init ();
 
  SerialPort_init ();

  if ( register_chrdev(60, "rab", &RabbiT_fops) )
  {
    printk ( "RabbiT: unable to get major %d\n", 60 );
    return;
  }
  
  timer_table[RABBIT_TIMER].fn      = rabbit_timer;
  timer_table[RABBIT_TIMER].expires = jiffies + 400;
  timer_active |= 1 << RABBIT_TIMER;
}
