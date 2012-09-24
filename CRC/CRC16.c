#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Allgemein.h"

#define CRC16_MASK 0xA001

/* update crc reverse */
static UINT updcrcr ( UINT uCRC, char cChar, UINT uMask )
{
  int$ i;
  for ( i = 0; i < 8; i++ )
  {
    if ( (uCRC ^ cChar) & 1 )
      uCRC = (uCRC>>1) ^ uMask;
    else
      uCRC >>= 1;
    cChar >>= 1;
  }
  return ( uCRC );
}

UINT makeCRC16 ( BYTE *pByte, UINT uLength )
{
  UINT i;
  UINT uCRC16 = 0;
  for ( i = 0; i < uLength; i++ )
  {
    uCRC16 = updcrcr ( uCRC16, *pByte, CRC16_MASK );
    pByte++;
  }
  return ( uCRC16 );
}
