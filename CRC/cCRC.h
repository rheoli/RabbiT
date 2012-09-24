#ifndef _H_CCRC
#define _H_CCRC

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#ifndef UINT
typedef unsigned int UINT;
#endif
#ifndef BYTE
typedef unsigned char BYTE;
#endif
#ifndef ULONG
typedef unsigned long ULONG;
#endif
#ifndef UINT32
typedef unsigned long int UINT32;
#endif


class cCRC
{
 private:
  const  UINT   kCRC16 = 0xA001;		/* crc-16 mask */
  const  UINT   kCCITT = 0x1021;		/* crc-ccitt mask */

  /* update crc */
  UINT updcrc ( UINT uCRC, int cChar, UINT uMask );
  /* update crc reverse */
  UINT updcrcr ( UINT uCRC, char cChar, UINT uMask );
  
public:
  UINT makeCRC16 ( BYTE *pByte, UINT uLength );
  UINT makeCCITT ( BYTE *pByte, UINT uLength );
  ULONG makeCRC32 ( BYTE *pByte, UINT uLength );
};

#endif  // _H_CCRC
