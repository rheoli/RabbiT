
/****************************************************************************
**                                                                         **
**            XYModem Implementation                                       **
**                                                                         **
** Copyright (C) 1994 Tim Kientzle.                                        **
** Distributed by Dr. Dobb's Journal with permission of the Author.        **
**                                                                         **
****************************************************************************/

/****************************************************************************
    Standard C library requirements
*/
#include <stdio.h>    /* NULL, stderr */
int fprintf();
int sprintf();
char *strcpy(char *s1, const char *s2);
int strlen(const char *s1);
int atoi(const char *s);
void memset( void *, unsigned char, int);

#include <time.h>  /* struct tm, time, localtime */

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

/****************************************************************************
    Interfaces
*/
#include "xy.h"

/****************************************************************************
    Useful definitions
*/
typedef unsigned char BYTE;

#define EOT (0x04)
#define ACK (0x06)
#define NAK (0x15)
#define CAN (0x18)
#define SUB (0x1A)

/****************************************************************************
    Return codes

    Every function returns one of the following codes to indicate the
    success or failure of the operation.
*/
enum { xyOK = 0,    /* No error */
        xyFail,     /* Transfer failed, send cancel sequence */
        xyFailed,   /* Transfer failed, don't send cancel sequence */
        xyBadPacket,/* Packet just received has bad CRC or checksum */
        xyEOT,      /* Received EOT instead of data packet */
        xyEndOfSession, /* No more files to transfer */
        xyEOF,      /* end-of-file on file read */
        xyTimeout,  /* Timeout waiting for data */
        xySerial    /* Non-fatal serial error, e.g., Frame or overflow error */
};

/****************************************************************************
    Encapsulates a common idiom: test a return code and return it if
    there was an error.
*/
#define StsRet(e)   do{int tmp_s; if ((tmp_s = (e)) != xyOK)\
                    return StsWarn(tmp_s);}while(FALSE)


/****************************************************************************
    Debugging hooks.  Replace StsWarn with
        #define StsWarn(s)  (s)
    to disable debugging messages.
*/
#define StsWarn(s)  XYDebugWarn(s,__FILE__,__LINE__)

static int XYDebugWarn(const int s,const char *file,const int line)
{
    if (s != xyOK)
        fprintf(stderr,"!?:%s:%d:",file,line);
    switch(s) {
        case xyOK:           break;
        case xyFail:         fprintf(stderr,"xyFail\n");            break;
        case xyFailed:       fprintf(stderr,"xyFailed\n");          break;
        case xyBadPacket:    fprintf(stderr,"xyBadPacket\n");       break;
        case xyEOT:          fprintf(stderr,"xyEOT\n");             break;
        case xyEndOfSession: fprintf(stderr,"xyEndOfSession\n");    break;
        case xyEOF:          fprintf(stderr,"xyEOF\n");             break;
        case xyTimeout:      fprintf(stderr,"xyTimeout\n");         break;
        case xySerial:       fprintf(stderr,"xySerial\n");          break;
        default:             fprintf(stderr,"Error %d\n",s);        break;
    }
    return s;
}


/****************************************************************************
    A `capability' has two fields.  The `enabled' field determines
    the current state of the capability, while the `certain' field
    determines whether the capability can still change.
        Whenever we have concrete evidence of a capability (for
    example, we receive an acknowledge for a long packet), we
    set the `certain' field to TRUE.
*/
struct CAPABILITY;
typedef struct CAPABILITY CAPABILITY;
struct CAPABILITY {
    int enabled;
    int certain;
};

/****************************************************************************
    Progress Status codes

    These are used to signal the progress reporting machinery.
*/
enum STATUS {stsNegotiating, stsSending, stsReceiving, stsEnding,
    stsDone, stsFailed, stsAborted};
typedef enum STATUS STATUS;

/****************************************************************************
    This structure contains the current state of the transfer.
    By keeping all information about the transfer in a dynamically-allocated
    structure, we can allow multiple simultaneous transfers in
    a multi-threaded system.
*/
struct XYMODEM;
typedef struct XYMODEM XYMODEM;
struct XYMODEM {
    CAPABILITY  crc,        /* Does other end support CRC? */
                longPacket, /* Does other end support 1K blocks? */
                batch,      /* Does other end support batch? */
                G;          /* Does other end support `G'? */
    int         timeout;    /* Number of seconds timeout */
    int         retries;    /* Number of times to retry a packet */
    int         userAbort;  /* Set when user asks for abort */
    int         packetNumber; /* Current packet number */
    FILE     *  f;          /* Current file */
    char     *  fileName;   /* Current filename */

    long        fileSize;   /* Size of file being transferred, -1 if not known */
    long        transferred;/* Number of bytes transferred so far */

    /* Other file information may need to be kept here */
    /* See XYSendPacketZero and XYFileWriteOpen for more information */

    /* PORT_TYPE keeps information about the serial port. */
    PORT_TYPE   port;       /* Port to use */

    char        **fileNames; /* Used by XYFileReadOpenNext */
    int         currentFileName;
    int         numFileNames;
};


/****************************************************************************
**                                                                         **
**            Utility Routines                                             **
**                                                                         **
****************************************************************************/

/****************************************************************************
    Clean out XYMODEM structure for new file
*/
void XYNewFile(XYMODEM *pXY)
{
    pXY->fileSize = -1;
    pXY->transferred = 0;
}

/****************************************************************************
    InitCRC16 pre-computes a table of constants, using a bitwise algorithm.
    These constants allow fast computation of the CRC.

    CCITT CRC16 generating polynomial is x^16+x^12+x^5+1, with
    binary representation 0x11021.  The leading term is implied.
*/
static unsigned int crcTable[256];

static void InitCRC16()
{
    static int crcDone = 0; /* Only compute it once */
    unsigned i, j, crc;

    if (crcDone) return;
    for(i=0; i <256; i++) {
        crc = (i << 8);
        for(j=0; j < 8; j++)
            crc = (crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0);
        crcTable[i] = crc & 0xffff;
    }
    crcDone = 1;
}

/****************************************************************************
    Convert time in seconds since Jan. 1, 1970 to broken-down
    time in the ANSI-standard "tm" structure.
*/
static void YMTimeToTm(long s, struct tm *pT)
{
    long m,h;  /* minutes, hours */
    int d, M, y; /* day, Month, year */

    if (s <= 0) { /* If time undefined, just use current time */
        time_t t;
        t = time(NULL);
        *pT = *localtime(&t);
        return;
    }

    /* Reduce everything */
    m = s/60; h = m/60; d = h/24; y = d/365;
    d %= 365; h %= 24; m %= 60; s %= 60;
    d -= (y+1)/4; /* Correct for leap years since 1970 */
    if (d<0) { y--; d+=365; }

    /* Before we destroy the day number, stash it */
    pT->tm_sec = s;
    pT->tm_min = m;
    pT->tm_hour = h;
    pT->tm_yday = d;

    /* Day numbers differ between leap and non-leap years, so */
    /* we first convert to leap-year numbering */
    if (((y-2)%4 != 0) && (d >= 59)) d++;

    /* It's easy to compute the month if we assume February has 30 days */
    /* So, we convert our day number to the day number in a year */
    /* with 377 days, and work from there. */
    if (d >= 60) d++; /* day number if Feb has 30 days */
    /* Now we can compute the month and day of month */
    M = (d>214)?7:0 + ((d%214)/61)*2 + ((d%214)%61)/31;  /* Jan is zero */
    d = ((d%214)%61)%31 + 1;

    pT->tm_mday = d;
    pT->tm_mon = M;
    pT->tm_year = y+70;
    pT->tm_isdst = -1;  /* DST unknown */
    pT->tm_wday = -1;  /* Day of week unknown */
}

/****************************************************************************
    Convert broken-down time in the ANSI-standard "tm" structure
    to time in seconds since Jan. 1, 1970.
*/
static long YMTime(struct tm *pT)
{
    static int mon[] = {0,31,60,91,121,152,182,213,244,274,305,335};
    int y=pT->tm_year-70, M=pT->tm_mon;
    int d=pT->tm_mday-1+mon[M];

    if (((y+2)%4 != 0) & (M>1)) d--; /* Adjust for non-leap years */
    d += (y+1)/4; /* Correct for leap years since 1970 */
    return (((((long)y) * 365 + d)*24 + pT->tm_hour)*60 + pT->tm_min)*60
                 + pT->tm_sec;
}

/****************************************************************************
**                                                                         **
**            Serial Interface                                             **
**                                                                         **
****************************************************************************/
/*
    This layer consists of routines to interface with the underlying
    operating system and/or application.  These are stubbed out since
    they are necessarily OS-specific.
*/

/****************************************************************************
    XYReadBytesWithTimeout

        Read bytes from serial port into buffer, ending when the buffer is
    filled or there is a delay longer than `timeout' between bytes.

        We accept a timeout parameter rather than using the timeout value
    in the XYMODEM structure because at certain points in the protocol
    different timeouts apply.  These other timeouts are computed from the
    timeout value in the XYMODEM structure.

    Error Handling:
        * returns xyFail if pXY->userAbort becomes set.
        * returns xyFailed if there is a fatal serial port error
            (i.e., loss of carrier or other hardware-level failure)
        * returns xyTimeout if the timeout expires
        * returns xySerial if a framing or overflow error is detected
        * returns xyOK otherwise
*/
static int XYReadBytesWithTimeout(XYMODEM *pXY, int timeout,
                                  BYTE *pBuffer, int length)
{
    if (pXY->userAbort) return StsWarn(xyFail);
    /* Get bytes from serial port */
    return xyOK;
}

/****************************************************************************
    XYSendBytes

        Send the bytes through the serial port.  If possible, return
    as soon as the bytes are queued, and implement XYWaitForSentBytes()
    to wait until the bytes are actually sent.

    Error Handling:
        * returns xyFail if pXY->userAbort becomes set.
        * returns xyFailed if there is a fatal serial port error
            (i.e., loss of carrier or other hardware-level error)
        * returns xyOK otherwise
*/
static int XYSendBytes(XYMODEM *pXY, BYTE *pBuffer, int length)
{
    if (pXY->userAbort) return StsWarn(xyFail);
    /* Send bytes */
    return xyOK;
}

/****************************************************************************
    XYWaitForSentBytes

        Delay until the send queue is empty.  This allows the protocol to
    exploit the time during which the outgoing serial queue is being emptied
    while still providing accurate timing.
        This functionality is not available on all systems.  It can be
    left as a stub with no ill effect except at very slow speeds, where
    the timeout interval should probably be enlarged.

    Error Handling:
        * returns xyFail if pXY->userAbort becomes set.
        * returns xyFailed if there is a fatal serial port error
            (i.e., loss of carrier or similar hardware-level failure)
        * returns xyOK otherwise
*/
static int XYWaitForSentBytes(XYMODEM *pXY)
{
    if (pXY->userAbort) return StsWarn(xyFail);
    /* Wait for send buffer to clear */
    return xyOK;
}

/****************************************************************************
    XYSendByte

        Send a single byte.
*/
static int XYSendByte(XYMODEM *pXY, BYTE b)
{
    return StsWarn(XYSendBytes(pXY, &b, 1));
}

/****************************************************************************
    XYGobble

        Read characters from serial port until indicated timeout occurs.
    XYGobble(0) simply flushes the receive queue.
*/
static int XYGobble(XYMODEM *pXY, int timeout)
{
    int err;
    BYTE junk[50];

    do {
        err = XYReadBytesWithTimeout(pXY,timeout,junk,sizeof(junk));
        if (err == xySerial) err = xyOK; /* Ignore framing errors */
    } while (err == xyOK);
    if (err == xyTimeout) return xyOK;
    return StsWarn(err);
}

/****************************************************************************
**                                                                         **
**            File Interface                                               **
**                                                                         **
****************************************************************************/
/*
    File manipulation routines.  These can be mostly implemented
    using ANSI-standard library functions.
*/

/****************************************************************************
    XYFileReadOpenNext

    Open next file to be sent.

    ANSI doesn't provide any portable mechanism to determine the size of
    a file other than reading the entire file.  (On some implementations,
    it may be possible to fseek() to the end of the file and use ftell()
    to get the file position and hence length, but ANSI doesn't gaurantee
    this to work; fseek() needn't accept SEEK_END for binary files, and
    the value returned by ftell() for text files may have a complex
    interpretation.)
*/
static int XYFileReadOpenNext( XYMODEM *pXY )
{
    while(1) {
        if (pXY->currentFileName == pXY->numFileNames)
            return xyEndOfSession;

        pXY->fileName = pXY->fileNames[pXY->currentFileName++];
        pXY->f = fopen(pXY->fileName,"r");
        if (pXY->f != NULL) {
            /* Set pXY->fileSize to size of file */
            pXY->fileSize = -1;  /* File size unknown */
            return xyOK;
        }
    }
}

/****************************************************************************
    XYFileRead
*/
static int XYFileRead( XYMODEM *pXY, BYTE *pBuffer, long *pLength )
{
    if (feof(pXY->f)) return xyEOF;
    *pLength = fread(pBuffer, sizeof(BYTE), *pLength, pXY->f);
    if ( (*pLength == 0) || ferror(pXY->f) ) return StsWarn(xyFail);
    return xyOK;
}

/****************************************************************************
    XYFileReadClose
*/
static int XYFileReadClose( XYMODEM *pXY)
{
    int returnVal;

    if (fclose(pXY->f)==0) returnVal = xyOK;
    else returnVal = xyFail;
    pXY->f = NULL;
    pXY->fileName = NULL;
    return StsWarn(returnVal);
}

/****************************************************************************
    XYFileWriteOpen

    The buffer contains a YModem batch header.  This function parses
    that header, setting up values in the XYMODEM structure and opening
    the file accordingly.

    NOTE: This function _must_ check the received filename to make
    sure it is valid for this system.

        This function should also handle filename collision avoidance,
    if desired.
*/
static int XYFileWriteOpen( XYMODEM *pXY, BYTE *pBuffer, int length)
{
    const char *fileName = (char *)pBuffer;
    long fileMode = -1;
    struct tm fileDate;

    /* Initialize to defaults */
    if (fileName == NULL) fileName = "xymodem.000\0\0";
    fileMode = -1;  /* default mode is unknown */
    { /* default file date is now */
        time_t t = time(NULL);
        fileDate = *localtime(&t);
    }

    { /* Parse the YModem filename and file information */
        const char *p = fileName;
        p += strlen(p) + 1;
        pXY->fileSize = -1;  /* Initialize to default values */
        if (*p) {   /* Get the file size */
            pXY->fileSize = atoi(p);
            while ((*p) && (*p != ' ')) p++; /* Advance to next field */
            if (*p) p++;
        }
        if (*p) {  /* Get the mod date */
            long fileDateSeconds = 0;
            while ((*p) && (*p != ' ')) {
                fileDateSeconds = fileDateSeconds * 8 + (*p) - '0';
                p++;
            } /* Note: fileDate is in GMT!! May need to convert to local time */
            YMTimeToTm(fileDateSeconds, &fileDate);
            if (*p) p++; /* Advance to next field */
        }
        if (*p) {  /* Get the file mode */
            fileMode = 0;
            while ((*p) && (*p != ' ')) {
                fileMode = fileMode * 8 + (*p) - '0';
                p++;
            }
            if (*p) p++; /* Advance to next field */
        }
    }

    /* ANSI doesn't portably support the notions of fileDate or fileMode */
    /* fileMode is -1 if unknown, otherwise it specifies a Unix-style */
    /* permissions map */
    pXY->f = fopen(fileName,"w");
    pXY->fileName = malloc(strlen(fileName)+1); /* Stash fileName */
    strcpy(pXY->fileName,fileName);
    if (pXY->f == NULL) return StsWarn(xyFail);
    return xyOK;
}

/****************************************************************************
    XYFileWrite
*/
static int XYFileWrite( XYMODEM *pXY, const BYTE *pBuffer, int length)
{
    int sizeWritten;

    sizeWritten = fwrite(pBuffer, sizeof(BYTE), length, pXY->f);
    if (sizeWritten < length) return xyFail;
    return xyOK;
}

/****************************************************************************
    XYFileWriteClose
*/
static int XYFileWriteClose( XYMODEM *pXY )
{
    int returnVal;

    if (fclose(pXY->f)==0) returnVal = xyOK;
    else returnVal = xyFail;
    pXY->f = NULL;
    if (pXY->fileName) free(pXY->fileName);
    return returnVal;
}

/****************************************************************************
**                                                                         **
**            Progress Reporting                                           **
**                                                                         **
****************************************************************************/

/****************************************************************************
    XYProgress.

    Bare-bones progress reporting dumps the current status to the
    console.
*/
static void XYProgress(XYMODEM *pXY, STATUS status)
{
    const char *statString;
    int fflush(FILE *f);

    switch(status) {
        case stsNegotiating: statString = "Negotiating..."; break;
        case stsSending:     statString = "Sending..."; break;
        case stsReceiving:   statString = "Receiving..."; break;
        case stsEnding:      statString = "Finishing..."; break;
        case stsDone:        statString = "Finished."; break;
        case stsFailed:      statString = "Failed."; break;
        case stsAborted:     statString = "Aborted."; break;
        default:             statString = "?!?!?!?!"; break;
    }

    /* Mostly for debugging, this gives detailed information on the
        current protocol decision.  For end-user reporting,
        this should be distilled down to: XModem, YModem, or YModem-G.
            batch.enabled -> YModem
            G.enabled -> YModem-G
            else XModem
    */
    fprintf(stderr,"%-14s  %c%c%c%c  ",statString,
        pXY->crc.enabled?(pXY->crc.certain?'C':'c')
                        :(pXY->crc.certain?'-':'.'),
        pXY->longPacket.enabled?(pXY->longPacket.certain?'K':'k')
                               :(pXY->longPacket.certain?'-':'.'),
        pXY->batch.enabled?(pXY->batch.certain?'B':'b')
                          :(pXY->batch.certain?'-':'.'),
        pXY->G.enabled?(pXY->G.certain?'G':'g')
                      :(pXY->G.certain?'-':'.'));
    /* This displays a percentage progress if the total filesize
        is known, otherwise just a count of bytes transferred.
    */
    if (pXY->fileSize > 0) {
        int percent = pXY->transferred * 100 / pXY->fileSize;
        if (percent > 100) percent = 100;
        fprintf(stderr,"    %3d%%",percent);
    } else
        fprintf(stderr,"%8d",pXY->transferred);
    /* Display the filename */
    if (pXY->f && pXY->fileName) {
        fprintf(stderr," %s",pXY->fileName);
    } else
        fprintf(stderr,"                             ");
    fprintf(stderr,"\n");
    fflush(stderr);
}

/****************************************************************************
**                                                                         **
**            Packet Layer                                                 **
**                                                                         **
****************************************************************************/
/*
    This layer contains routines to send and receive data packets.
*/

/****************************************************************************
    XYSendPacket

        Send an XYModem data packet.  The length must be less than 1024.
    Packets shorter than 128 or longer than 128 but shorter than 1024 are
    padded with SUB characters.
        We use XYWaitForSentBytes() to delay our return until the
    transmit queue is empty.  That helps to gaurantee that future
    timeouts will be accurate (we want to time from the last character
    we sent).
*/
static int XYSendPacket(XYMODEM *pXY, BYTE *pBuffer, int length)
{
    int i;

    if (length <= 128)  StsRet(XYSendByte(pXY, 0x01));
    else                StsRet(XYSendByte(pXY, 0x02));

    StsRet(XYSendByte(pXY, pXY->packetNumber));
    StsRet(XYSendByte(pXY, ~pXY->packetNumber));

    /* Send the data, pad to 128 or 1024 bytes */
    StsRet(XYSendBytes(pXY, pBuffer, length));
    for (i=length; i < 128; i++) StsRet(XYSendByte(pXY,SUB));
    if (i > 128) for (; i<1024; i++) StsRet(XYSendByte(pXY, SUB));
    
    /* Compute and send the check value */
    if (pXY->crc.enabled) {
        int crc = 0;           /* Accumulate CRC for data and padding */
        for (i=0; i<length; i++)
            crc = crcTable[((crc >> 8) ^ *pBuffer++) & 0xFF] ^ (crc << 8);
        for (; i<128; i++)
            crc = crcTable[((crc >> 8) ^ SUB) & 0xFF] ^ (crc << 8);
        if (i > 128)
            for (; i<1024; i++)
                crc = crcTable[((crc >> 8) ^ SUB) & 0xFF] ^ (crc << 8);
        StsRet(XYSendByte(pXY, crc >> 8));
        StsRet(XYWaitForSentBytes(pXY));
        StsRet(XYGobble(pXY,0)); /* Clear any garbage from receive queue */
        StsRet(XYSendByte(pXY, crc));
    } else {
        int checksum = 0;     /* Accumulate checksum for data and padding */
        for (i=0; i<length; i++)  checksum += *pBuffer++;
        if (i > 128)              checksum += (1024 - i) * SUB;
        else                      checksum += (128 - i) * SUB;
        StsRet(XYWaitForSentBytes(pXY));
        StsRet(XYGobble(pXY,0)); /* Clear any garbage from receive queue */
        StsRet(XYSendByte(pXY, checksum));
    }
    return xyOK;
}

/****************************************************************************
    Read ACK/NAK/CAN control packets
*/
static int XYSendReadAckNak(XYMODEM *pXY, BYTE *pResponse)
{
    int err;
    int canCount = 0;

    do { /* Read ACK or NAK response */
        err = XYReadBytesWithTimeout(pXY, pXY->timeout*5, pResponse, 1);
        if (err == xySerial) err = xyTimeout;
        StsRet(err);
        if (*pResponse == CAN) {
            if (++canCount >= 2) return StsWarn(xyFailed);
        } else canCount = 0;
    } while ((*pResponse != ACK) && (*pResponse != NAK));
    return StsWarn(err);
}

/****************************************************************************
    XYReceivePacket

    The receiver must be able to receive three different types of packets:
        - data packets start with a recognizable 3-byte sequence
        - EOT packets consist of a single EOT character
        - CAN packets consist of two consecutive CAN
    We use a shorter timeout between bytes within a single packet
    than we do between packets.  This helps speed error recovery.
*/
static int XYReceivePacket(XYMODEM *pXY, int *pPacketNumber,
                           BYTE *pBuffer, int *pLength)
{
    BYTE  startOfPacket = 0;
    BYTE  packet = 0;
    BYTE  packetCheck = 0;

    /* This loop searches the incoming bytes for a valid packet start. */
    /* This reduces our sensitivity to inter-packet noise. */
    /* We also check here for EOT and CAN packets. */
    StsRet(XYReadBytesWithTimeout(pXY,pXY->timeout,&packetCheck,1));
    if (packetCheck == EOT) return xyEOT;
    do {
        startOfPacket = packet; packet = packetCheck;
        StsRet(XYReadBytesWithTimeout(pXY,pXY->timeout,&packetCheck,1));
        if ((packetCheck == CAN) && (packet == CAN)) return StsWarn(xyFailed);
    } while (  ( (startOfPacket != 0x01) && (startOfPacket != 0x02) )
            || ( ((packet ^ packetCheck) & 0xFF) != 0xFF ) );

    /* We've received a valid packet start, receive the packet data */
    if (startOfPacket == 0x01) *pLength = 128;
    else                       *pLength = 1024;
    StsRet(XYReadBytesWithTimeout(pXY,2,pBuffer,*pLength));
    *pPacketNumber = packet;

    /* Compute the check value and compare it to the received one. */
    if (pXY->crc.enabled) {
        unsigned crc = 0;
        int length = *pLength;
        BYTE crcByte;
        int rxCRC;
        while (length-- > 0)        /* Accumulate CRC */
            crc = crcTable[((crc >> 8) ^ *pBuffer++) & 0xFF] ^ (crc << 8);
        crc &= 0xFFFF;
        StsRet(XYReadBytesWithTimeout(pXY,2,&crcByte,1));
        rxCRC = (crcByte & 0xFF) << 8;
        StsRet(XYReadBytesWithTimeout(pXY,2,&crcByte,1));
        rxCRC |= (crcByte & 0xFF);
        if (crc != rxCRC) return StsWarn(xyBadPacket);
    } else {
        unsigned checksum = 0;
        BYTE receivedChecksum;
        int length = *pLength;        

        while (length-- > 0)        /* Accumulate checksum */
            checksum += *pBuffer++;
        checksum &= 0xFF;
        StsRet(XYReadBytesWithTimeout(pXY,2,&receivedChecksum,1));
        if (checksum != receivedChecksum) return StsWarn(xyBadPacket);
    }
    pXY->crc.certain = TRUE; /* Checksum/crc mode is now known */
    if (*pLength > 128) {
        pXY->longPacket.enabled = TRUE;
        pXY->longPacket.certain = TRUE;
    }
    return xyOK;
}

/****************************************************************************
**                                                                         **
**            Reliability Layer                                            **
**                                                                         **
****************************************************************************/
/*
    This layer sends and receives packets with gauranteed success.
*/

/****************************************************************************
    XYSendPacketReliable

    Repeatedly sends a packet until it is acknowledged.  Note that only
    a slight change is required to handle the YModem-G protocol.
       We could add code here to attempt to recover from spurious
    ACKs.  A spurious ACK causes us to send the next packet, while
    the receiver is still waiting on this one.  The solution is
    to fall back to the previous packet after a large number of NAKs.
    This requires saving an ACKed packet somewhere in the XYMODEM
    structure, along with some additional bookkeeping information.
*/
static int XYSendPacketReliable(XYMODEM *pXY, BYTE *pBuffer, int length)
{
    int err;
    BYTE response = ACK;

    do {
        StsRet(XYSendPacket(pXY, pBuffer, length));
        if (pXY->G.enabled) return xyOK;
        err = XYSendReadAckNak(pXY,&response);
        if (err == xyTimeout) return StsWarn(xyFail);
        StsRet(err);
    } while (response != ACK);
    pXY->crc.certain = TRUE; /* Checksum/crc mode is now known */
    if (length > 128) {
        pXY->longPacket.enabled = TRUE;
        pXY->longPacket.certain = TRUE;
    }
    return xyOK;
}

/****************************************************************************
    Send EOT and wait for acknowledgement.
*/
static int XYSendEOTReliable(XYMODEM *pXY)
{
    BYTE b;
    int retries = pXY->retries;
    int err;

    do {
        StsRet(XYSendByte(pXY,EOT));
        err = XYReadBytesWithTimeout(pXY,pXY->timeout,&b,1);
        if (err == xyOK) {
            if (b == ACK) return xyOK;
            else StsRet(XYGobble(pXY,3));
        }
        else if (err != xyTimeout) return StsWarn(err);
        if (retries-- == 0) return StsWarn(xyFail);
    } while (TRUE);
    return xyOK;
}


/****************************************************************************
    XYReceivePacketReliable

        NAKs of data packets until a valid packet is
    received.  The next layer up is responsible for sending
    the ACK and dealing with packet sequencing issues.

        EOT packets are handled here by the following logic:  An
    EOT is considered reliable if it is repeated twice by the
    sender or if we get three consecutive timeouts after an EOT.
    (The latter case handles old XModem senders that terminate
    as soon as they send an EOT.)  We handle this by incrementing
    eotCount by one for each timeout after an EOT, and by three
    for each EOT.  When eotCount reaches six, the EOT is reliable.
        Cancel packets (two CANs) are already reliable.

        We don't ACK packets here so the next layer up can do some
    work (opening files, etc.) before the ACK is sent.  That way,
    we can avoid having to deal with the issue of overlapped serial
    and disk I/O.
*/
static int XYReceivePacketReliable(XYMODEM *pXY, int *pPacket,
                                    BYTE *pBuffer, int *pLength)
{
    int err;
    int eotCount = 0;
    int retries = pXY->retries;

    do {
        err = XYReceivePacket(pXY, pPacket, pBuffer, pLength);
        if (err == xyEOT) { /* EOT packet seen */
            if (pXY->G.enabled) return xyEOT; /* Don't challenge in YM-G */
            eotCount+=3;
            if (eotCount >= 6) return xyEOT;
        } else if ((err == xyTimeout) && (eotCount > 0)) {
            eotCount++;
            if (eotCount >= 6) return StsWarn(xyEOT);
        } else { /* Data packet or timeout seen */
            eotCount = 0;
            if ((err != xyBadPacket) && (err != xyTimeout)) return StsWarn(err);
            else if (pXY->G.enabled) return StsWarn(xyFail);
        }
        StsRet(XYSendByte(pXY, NAK));
    } while (retries-- > 0);
    return StsWarn(xyFail);
}

/****************************************************************************
**                                                                         **
**            File Layer                                                   **
**                                                                         **
****************************************************************************/

/****************************************************************************
    Build packet zero and send it.
*/
static int XYSendPacketZero(XYMODEM *pXY)
{
    BYTE data[1024];
    int length = 128;
    char *p;

    memset(data,0,sizeof(data));
    p = (char *)data;
    if ((pXY->f) && (pXY->fileName) && (*(pXY->fileName))) {
        strcpy(p,pXY->fileName);
        p += strlen(p) + 1;
        if ( (pXY->fileSize >= 0) /* && (file is not text file)*/ ){
            sprintf(p,"%ld",pXY->fileSize);
            p += strlen(p);
/*
            sprintf(p," %lo",YMTime(&(pXY->fileDate)));
            p += strlen(p);
            if (pXY->fileMode >= 0) {
                sprintf(p," %lo",pXY->fileMode);
                p += strlen(p);
            }
*/
        }
    }
    if (p >= ((char *)data)+128) length = 1024;
    pXY->packetNumber = 0;
    return StsWarn(XYSendPacket(pXY, data, length));
}


/****************************************************************************
    Read and interpret receiver's handshake
*/
static int XYSendReadHandshake(XYMODEM *pXY, BYTE *pResponse)
{
    int err;
    int canCount = 0;

    do { /* Read handshake */
        err = XYReadBytesWithTimeout(pXY, pXY->timeout*5, pResponse, 1);
        if (err == xyTimeout) return StsWarn(xyFail);
        if (err != xyOK) return StsWarn(err);

        if (*pResponse == CAN) {
            if (++canCount >= 2) return StsWarn(xyFailed);
        } else canCount = 0;

        /* Interpret the receiver's handshake */
        switch(*pResponse) {
            case 'G':
                if (!pXY->crc.certain) pXY->crc.enabled = TRUE;
                if (!pXY->crc.enabled) break;
                if (!pXY->batch.certain) pXY->batch.enabled = TRUE;
                if (!pXY->batch.enabled) break;
                if (pXY->G.enabled) return xyOK;
                if (!pXY->G.certain) pXY->G.enabled = TRUE;
                if (!pXY->G.enabled) break;
                return xyOK;

            case 'C':
                if (!pXY->G.certain) pXY->G.enabled = FALSE;
                if (pXY->G.enabled) break;
                if (pXY->crc.enabled) return xyOK;
                if (!pXY->crc.certain) pXY->crc.enabled = TRUE;
                if (!pXY->crc.enabled) break;
                if (!pXY->batch.certain) pXY->batch.enabled = TRUE;
                if (!pXY->longPacket.certain)
                       pXY->longPacket.enabled = pXY->batch.enabled;
                return xyOK;

            case NAK:
                if (!pXY->G.certain) pXY->G.enabled = FALSE;
                if (pXY->G.enabled) break;
                if (!pXY->crc.enabled) return xyOK;
                if (!pXY->crc.certain) pXY->crc.enabled = FALSE;
                if (pXY->crc.enabled) break;
                if (!pXY->batch.certain) pXY->batch.enabled = FALSE;
                if (pXY->batch.enabled) break;
                if (!pXY->longPacket.certain)
                        pXY->longPacket.enabled = FALSE;
                return xyOK;

            case ACK: return xyOK;

            default: break;
        }
    } while (TRUE);
}

/****************************************************************************
    Send packet zero or one, depending on batch mode.  If there are
    too many failures, we swap batch mode.

    The buffer is passed as a parameter to reduce our stack requirements.
*/
static int XYSendFirstPacket(XYMODEM *pXY, BYTE *pBuffer, int bufferLength )
{
    int err;
    int totalRetries = pXY->retries;
    int retries = pXY->retries / 2;
    BYTE firstHandshake;
    BYTE acknowledge;  /* Acknowledge received for packet */
    BYTE handshake;  /* Repeat handshake for YModem */
    int handshakeErr = xyOK;
    long dataLength = 0;

    /* Get initial handshake */
    do {
        StsRet(XYSendReadHandshake(pXY,&firstHandshake));
    } while (firstHandshake == ACK); /* Ignore spurious ACKs */

    do {
        /* Send packet 0 or 1, depending on current batch mode */
        if (pXY->batch.enabled) {
            StsRet(XYSendPacketZero(pXY));
        } else {
            if (dataLength == 0) { /* Get packet 1 */
                dataLength = (pXY->longPacket.enabled)?1024:128;
                err = XYFileRead(pXY, pBuffer, &dataLength);
            }
            pXY->packetNumber = 1;
            StsRet(XYSendPacket(pXY,pBuffer,dataLength));
        }

        /*
            The most interesting case is if we just sent a YBatch
            file data packet, in which case there are several
            responses we might see:
                - repeated handshake -> receiver didn't see our packet,
                    or XModem receiver ignored it
                - ACK not followed by handshake -> XModem receiver
                    interpreted packet zero as duplicate packet.
                - ACK followed by handshake -> YModem receiver accepted
                    our file header and we should continue.
        */
        StsRet(XYSendReadHandshake(pXY,&acknowledge));
        if ((acknowledge == ACK) && (pXY->batch.enabled)) {
            do { /* Wait for error (timeout) or repeat handshake */
                handshakeErr = XYSendReadHandshake(pXY,&handshake);
            } while ((handshakeErr == xyOK) && (handshake != firstHandshake));
            if ((handshakeErr != xyOK) && (handshakeErr != xyTimeout))
                return StsWarn(handshakeErr);
        }

        /* Count down number of retries */
        if ( (acknowledge != ACK)
            || ( pXY->batch.enabled && (handshakeErr != xyOK))
            )
        {
            if (retries-- == 0) {
                if (!pXY->batch.certain)
                    pXY->batch.enabled = !pXY->batch.enabled;
                if (!pXY->longPacket.certain)
                    pXY->longPacket.enabled = pXY->batch.enabled;
                retries = 2;
            }
            if (totalRetries-- == 0) return StsWarn(xyFail);
        }
    } while ( (acknowledge != ACK)
            || ( pXY->batch.enabled && (handshakeErr != xyOK))
            );

    pXY->batch.certain = TRUE; /* batch mode is now known */
    pXY->G.certain = TRUE;

    if ( (pXY->packetNumber == 0) && (dataLength > 0) ) {
        pXY->packetNumber++;
        StsRet(XYSendPacketReliable(pXY, pBuffer, dataLength));
        pXY->transferred += dataLength;
    }
    return xyOK;
}

/****************************************************************************
    Send a single file.
*/
static int XYSendFile(XYMODEM *pXY)
{
    BYTE data[1024];
    long dataLength;
    int err = xyOK;

    XYProgress(pXY,stsNegotiating);
    StsRet(XYSendFirstPacket(pXY,data,sizeof(data)/sizeof(data[0])));

    while(err == xyOK) {
        int packetLength;
        BYTE *p = data;
        dataLength = (pXY->longPacket.enabled)?1024:128;
        err = XYFileRead(pXY, data, &dataLength);
        packetLength = (dataLength > 767)?1024:128;
        while ( (err == xyOK) && (dataLength > 0) ){
            pXY->packetNumber++;
            XYProgress(pXY,stsSending);
            if (packetLength > dataLength) packetLength = dataLength;
            StsRet(XYSendPacketReliable(pXY, p, packetLength));
            pXY->transferred += packetLength;
            dataLength -= packetLength;
            p += packetLength;
        }
    }
    if (err == xyEOF) {
        XYProgress(pXY,stsEnding);
        err = XYSendEOTReliable(pXY);
    }
    if (err != xyOK) return StsWarn(err);
    return xyOK;
}

/****************************************************************************
    Shuffles capabilities for falling back to a lower protocol.
    Note that we do actually `fall' from basic XModem to YModem-G,
    to handle obstinate senders that may be looking for only
    a `G' or `C' handshake.
*/
static int XYReceiveFallback(XYMODEM *pXY)
{
    if (pXY->G.enabled)
        pXY->G.enabled = FALSE;
    else if (pXY->crc.enabled)
        pXY->crc.enabled
            = pXY->batch.enabled
            = pXY->longPacket.enabled
            = FALSE;
    else
        pXY->G.enabled = pXY->batch.enabled
            = pXY->longPacket.enabled = pXY->crc.enabled = TRUE;
    return xyOK;
}

/****************************************************************************
    Send the correct handshake for the current capabilities.
*/
static int XYReceiveSendHandshake(XYMODEM *pXY)
{
    if (pXY->G.enabled) return StsWarn(XYSendByte(pXY,'G'));
    if (pXY->crc.enabled) return StsWarn(XYSendByte(pXY,'C'));
    return StsWarn(XYSendByte(pXY,NAK));
}

/****************************************************************************
    Receive a single file.
*/
static int XYReceiveFile(XYMODEM *pXY)
{
    BYTE data[1024];
    int dataLength;
    int err = xyOK;
    int packetNumber;
    int retries = pXY->retries/2 + 1;
    int totalRetries = (pXY->retries * 3)/2+1;

    /* Try different handshakes until we get the first packet */
    XYNewFile(pXY);
    XYProgress(pXY,stsNegotiating);
    do { 
        if (--retries == 0) {
            XYReceiveFallback(pXY);
            XYProgress(pXY,stsNegotiating);
            retries = (pXY->retries/3);
        }
        if (totalRetries-- == 0) return StsWarn(xyFail);
        StsRet(XYReceiveSendHandshake(pXY));
        err = XYReceivePacket(pXY, &packetNumber, data, &dataLength);
        if (err == xyEOT) {  /* EOT must be garbage... */
            StsRet(XYGobble(pXY, pXY->timeout/2));
        }
        if (err == xyBadPacket) { /* garbaged block */
            StsRet(XYGobble(pXY,pXY->timeout/3));
        }
    } while ( (err == xyTimeout) || (err == xyBadPacket) || (err == xyEOT) );

    StsRet(err);
    if ((packetNumber != 0) && (packetNumber != 1)) return StsWarn(xyFail);

    /* The first packet tells us the sender's batch mode */
    if (packetNumber == 0) {
        /* We can gaurantee that either batch.enabled is already TRUE */
        /* or batch.certain is FALSE */
        pXY->batch.enabled = TRUE;
    }
    else /* packetNumber == 1 */
    {
        /* If batch mode is certain, then a mismatch is fatal. */
        if (pXY->batch.certain && pXY->batch.enabled)
            return StsWarn(xyFail);
        pXY->batch.enabled = FALSE;
        pXY->G.enabled = FALSE;  /* Y-G is always batch */
    }
    pXY->batch.certain = TRUE;

    /* Open the file and make sure `data' contains the first part of file */
    if (packetNumber == 0) {
        if (data[0] == 0) {
            StsRet(XYSendByte(pXY,ACK)); /* Ack packet zero */
            return xyEndOfSession;
        }
        StsRet(XYFileWriteOpen(pXY, data, dataLength));
        StsRet(XYSendByte(pXY,ACK)); /* Ack packet zero */
        StsRet(XYReceiveSendHandshake(pXY));
        err = XYReceivePacketReliable(pXY, &packetNumber, data, &dataLength);
    } else {
        StsRet(XYFileWriteOpen(pXY, NULL, 0));
    }
    pXY->packetNumber = 1;
    pXY->transferred = 0;
    XYProgress(pXY,stsReceiving);

    /* We have the first packet of file data. */
    /* Receive remaining packets and write it all to the file. */
    /* Note that we're careful to ACK only after file I/O is complete. */
    while (err == xyOK) {
        if (packetNumber == (pXY->packetNumber & 0xFF)) {
            if ( (pXY->fileSize > 0)
                 && (dataLength + pXY->transferred > pXY->fileSize) )
                dataLength = pXY->fileSize - pXY->transferred;
            StsRet(XYFileWrite(pXY,data,dataLength));
            pXY->transferred += dataLength;
            pXY->packetNumber++;
            XYProgress(pXY,stsReceiving);
            if (!pXY->G.enabled)
                StsRet(XYSendByte(pXY,ACK)); /* Ack correct packet */
        } else if (packetNumber == (pXY->packetNumber-1) & 0xFF)
            StsRet(XYSendByte(pXY,ACK)); /* Ack repeat of previous packet */
        else {
            return StsWarn(xyFail); /* Fatal: wrong packet number! */
        }
        err = XYReceivePacketReliable(pXY, &packetNumber, data, &dataLength);
    }
    /* ACK the EOT.  Note that the Reliability layer has already */
    /* handled a challenge, if necessary. */
    if (err == xyEOT) {
        XYProgress(pXY,stsEnding);
        err = XYSendByte(pXY,ACK);
    }
    StsRet(XYFileWriteClose(pXY));
    return StsWarn(err);
}


/****************************************************************************
**                                                                         **
**            Session Layer                                                **
**                                                                         **
****************************************************************************/

/****************************************************************************
    This is only used to end the session.  For real files,
    XYSendFirstPacket handles the packet zero negotiation.
*/
static int XYSendSessionEnd(XYMODEM *pXY)
{
    int err;
    BYTE response;

    XYNewFile(pXY);
    XYProgress(pXY,stsEnding);
    do {
        StsRet(XYSendPacketZero(pXY));
        if (pXY->G.enabled) return xyOK;
        do { /* Read ACK or NAK response */
            err = XYReadBytesWithTimeout(pXY, pXY->timeout, &response, 1);
            if (err == xyTimeout) return StsWarn(xyFail);
            StsRet(err);
        } while ((response != ACK) && (response != NAK));
    } while (response != ACK);
    return xyOK;
}

/****************************************************************************
    Send all files.
*/
static int XYSend(XYMODEM *pXY)
{
    int err;

    XYNewFile(pXY);
    do {
        err = XYFileReadOpenNext(pXY);
        if (err == xyOK) {
            err = XYSendFile(pXY);
            XYFileReadClose(pXY);
            XYNewFile(pXY);
        }
    } while ( (err == xyOK) && (pXY->batch.enabled) );
    if (err == xyEndOfSession) {
        err = xyOK;
        if (pXY->batch.enabled)
            err = XYSendSessionEnd(pXY);
    }
    if (err == xyFail) {
        static BYTE cancel[] = {CAN,CAN,CAN,CAN,CAN,8,8,8,8,8};
        XYSendBytes(pXY,cancel,sizeof(cancel)/sizeof(cancel[0]));
        return StsWarn(xyFailed);
    }
    if (err == xyOK) XYProgress(pXY,stsDone);
    else if (pXY->userAbort) XYProgress(pXY,stsAborted);
    else XYProgress(pXY,stsFailed);
    return err;
}

/****************************************************************************
    The session layer simply receives successive files until we
    reach end-of-session.
*/
static int XYReceive(XYMODEM *pXY)
{
    int err;

    do {
        err = XYReceiveFile(pXY);
    } while ( (err == xyOK) && (pXY->batch.enabled) );
    if (err == xyEndOfSession) err = xyOK;
    if (err == xyFail) {
        static BYTE cancel[] = {CAN,CAN,CAN,CAN,CAN,8,8,8,8,8};
        XYSendBytes(pXY,cancel,sizeof(cancel)/sizeof(cancel[0]));
        err = xyFailed;
    }
    if (err == xyOK) XYProgress(pXY,stsDone);
    else if (pXY->userAbort) XYProgress(pXY,stsAborted);
    else XYProgress(pXY,stsFailed);
    XYGobble(pXY,2); /* Gobble any line garbage */
    return StsWarn(err);
}

/****************************************************************************
**                                                                         **
**            Public Interface Layer                                       **
**                                                                         **
****************************************************************************/
/*
    This layer provides a place where system- or application-specific
    customizations can be made.  This simplifies altering the
    interface.
*/

/****************************************************************************
    Currently, this simply returns a pointer to a static structure.
    If multiple simultaneous transfers are needed, then this can
    be changed to dynamically allocate the structure.
*/
int XYModemInit( void **ppXY, int protocol, int timeout, PORT_TYPE port)
{
    static XYMODEM xy;
    XYMODEM *pXY;

    InitCRC16();  /* Make sure the CRC table is computed */
    pXY = &xy;

    memset(pXY,0,sizeof(*pXY));  /* Initialize structure */

    pXY->timeout = timeout;
    pXY->retries = 10;
    pXY->port = port;

    /* None of these are certain yet */
    /* Conveniently, each of these implies all the capabilities of */
    /* the lower ones. */
    switch(protocol) {
        case YModemG:   pXY->G.enabled = TRUE; /* Fall through */
        case YModem:    pXY->batch.enabled = pXY->longPacket.enabled = TRUE;
                        /* Fall through */
        case XModemCRC: pXY->crc.enabled = TRUE; /* Fall through */
        case XModem:    break;
        default: return StsWarn(xyFailed);
    }
    *ppXY = pXY;
    return xyOK;
}

/****************************************************************************
    Set the abort flag and return.
*/
int XYModemAbort(void *pXY_public)
{
    XYMODEM *pXY = pXY_public;
    pXY->userAbort = TRUE;
    return xyOK;
}

/****************************************************************************
    This can be modified to accept additional parameters, such as a
    pointer to a progress-reporting function.  Typically, such information
    will be stashed in the XYMODEM structure to be used by the low-level
    file, serial, or progress functions.
        This version accepts a pointer to an array of filenames, which
    is used by XYFileReadOpenNext.
*/
int XYModemSend(void *pXY_public, char *fileNames[], int count)
{
    XYMODEM *pXY = pXY_public;
    pXY->currentFileName = 0;
    pXY->fileNames = fileNames;
    pXY->numFileNames = count;
    if (XYSend(pXY) == xyOK)
        return 0;
    else
        return 1;
}

/****************************************************************************
    This can be modified to accept additional parameters, such as a
    pointer to a progress-reporting function.  Typically, such information
    will be stashed in the XYMODEM structure to be used by the low-level
    file, serial, or progress functions.
*/
int XYModemReceive( void *pXY_public)
{
    XYMODEM *pXY = pXY_public;
    if (XYReceive(pXY) == xyOK)
        return 0;
    else
        return 1;
}

/* end-of-file */
