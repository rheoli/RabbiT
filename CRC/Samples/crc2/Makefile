# Makefile fuer PAR-Protokoll-Emulation

all: sender receiver snetwork enetwork

sender.o: sender.cc PARProtocol.h cCRC.h
	gcc -c sender.cc

receiver.o: receiver.cc PARProtocol.h
	gcc -c receiver.cc

snetwork.o: snetwork.cc
	gcc -c snetwork.cc

enetwork.o: enetwork.cc
	gcc -c enetwork.cc

cCRC.o: cCRC.h cCRC.cc
	gcc -c cCRC.cc

sender: sender.o cCRC.o
	gcc -o sender sender.o cCRC.o

receiver: receiver.o
	gcc -o receiver receiver.o

snetwork: snetwork.o
	gcc -o snetwork snetwork.o

enetwork: enetwork.o
	gcc -o enetwork enetwork.o
