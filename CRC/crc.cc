#include "cCRC.h"

void main ()
{
  cCRC oCRC;
  char hallo[] = "ABC";
  
  printf ( "CRC-Code: %08lX.\n", oCRC.makeCRC32 ( (BYTE *)&hallo[0], 3 ) );
}