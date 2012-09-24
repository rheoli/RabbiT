all: Protocol

CRC32.o: CRC32.c CRC32.h
	gcc -c CRC32.c

CRC16.o: CRC16.c CRC16.h
	gcc -c CRC16.c

L1RingBuffer.o: L1RingBuffer.c L1RingBuffer.h
	gcc -c L1RingBuffer.c

L2RingBuffer.o: L2RingBuffer.c L2RingBuffer.h
	gcc -c L2RingBuffer.c

Protocol.o: Protocol.c WPARProtocol.h
	gcc -c Protocol.c

Protocol: Protocol.o L1RingBuffer.o L2RingBuffer.o CRC32.o CRC16.o
	gcc -o Protocol Protocol.o L1RingBuffer.o L2RingBuffer.o CRC32.o CRC16.o
