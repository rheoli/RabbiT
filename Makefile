#
# Makefile for the Linux RabbiT Layer
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := RabbiT.o
O_OBJS := RABDevice.o L1RingBuffer.o L2RingBuffer.o SerialPort.o\
          SerialTimer.o CRC32.o CRC16.o

MOD_LIST_NAME := RABBIT_MODULES
M_OBJS :=

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
