#ifndef _H_RABDEF
#define _H_RABDEF

struct RabbiT_device
{
  int          iBusy;
  int          iPortType;
  int          iProtocol;
};

extern struct RabbiT_device sRabbiTBusy[];

#define RABBIT_PORTTYPE_NONE       0x00
#define RABBIT_PORTTYPE_SERIAL     0x1000
#define RABBIT_PORTTYPE_ETHERNET   0x2000
#define RABBIT_PORTTYPE_SERIALTEST 0x4000
#define RABBIT_SERIAL_SPEED        0x03

#define RABBIT_PROTOCOL_NONE       0x00
#define RABBIT_PROTOCOL_CHAT       0x01
#define RABBIT_PROTOCOL_WPAR       0x02
#define RABBIT_PROTOCOL_XMODEM     0x03

#endif
