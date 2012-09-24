
/****************************************************************************
**                                                                         **
**                 XYModem Public Interface                                **
**                                                                         **
**  Copyright (C) 1994 Tim Kientzle.                                       **
**  Distributed by Dr. Dobb's magazine with permission of the Author.      **
**                                                                         **
****************************************************************************/

/* #include "serial.h" */ /* Glue code for serial port: defines PORT_TYPE */
typedef int PORT_TYPE;

/****************************************************************************
    Protocol codes.

    These are taken by XYModemSend and XYModemReceive as ``hints.''
    The actual protocol will be determined by negotiation.
*/
#define XModem  0
#define XModemCRC 1
#define YModem  2
#define YModemG 3

/****************************************************************************
    Initialize

    This creates an XYModem transfer handle and initializes it with
    the information given by the client.

    Returns 0 on success, non-zero if there is some problem.
*/
int XYModemInit( void **ppXY, int protocol, int timeout, PORT_TYPE port);

/****************************************************************************
    Send/Receive

    These an XYModem handle as argument.  They return zero if the
    transfer was successful, non-zero otherwise.
*/

int XYModemSend( void *pXY, char *filenames[], int numFiles);

int XYModemReceive( void *pXY);

/****************************************************************************
    Abort transfer

    Aborts a transfer.  XYModemSend or XYModemReceive will return a
    non-zero exit status a few seconds after this function is called.
*/
int XYModemAbort( void *pXY);

/* end-of-file */
