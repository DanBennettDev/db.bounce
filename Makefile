#########################################
#	Makefile for Bounce~				#
#	Daniel Bennett						#
# 	07/07/2015							#
#########################################

#--------------
# INGREDIENTS 
#--------------

# My Parameters
P=db.bounce~
COMPFLAGS= -DWIN_VERSION -DWIN_EXT_VERSION
MAXLD_FLAGS= -lMaxAPI -lMaxAudio -ljitlib
TESTFLAGS= 

# My Locations
CYGWIN= C:/cygwin64/lib 
MYL= C:/Programming/_Resources/Libraries/C
MAXINC= -I$(MYL)/MaxMSP6/jit-includes -I$(MYL)/MaxMSP6/msp-includes -I$(MYL)/MaxMSP6/max-includes
MAXLD_LOC= -L$(MYL)/MaxMSP6/jit-includes/x64 -L$(MYL)/MaxMSP6/msp-includes/x64 -L$(MYL)/MaxMSP6/max-includes/x64 
MAXLD_LOC32= -L$(MYL)/MaxMSP6/jit-includes -L$(MYL)/MaxMSP6/msp-includes -L$(MYL)/MaxMSP6/max-includes

# Standard bits
OBJECTS=
CFLAGS=  $(TESTFLAGS) -I$(CYGWIN) 
LDLIBS=
CC=x86_64-w64-mingw32-gcc	#64bit MinGW
CC32=i686-w64-mingw32-gcc 	#32bit MinGW
##CC=gcc					#64bit CYGWIN (requires dll distributing...)


#-----------
# RECIPES 
#-----------

all: clean 32 64

32: clean32 object32 mxe32

64: clean64 object64 mxe64

clean: clean32 clean64

clean32:
	-rm -f $(P).mxe $(P).o

clean64:
	-rm -f $(P).mxe64 $(P).o

object64:
	$(CC) -c -g -Wall -Wno-unknown-pragmas -O3 $(COMPFLAGS) $(MAXINC) $(P).c

mxe64:
	$(CC) -shared -Wall $(DLLFLAGS) -o $(P).mxe64 $(P).o $(P).def $(MAXLD_LOC) $(MAXLD_FLAGS)
	
object32:
	$(CC32) -c -g -Wno-unknown-pragmas -O3 $(COMPFLAGS) $(MAXINC) $(P).c	

mxe32:
	$(CC32) -shared -Wall $(DLLFLAGS) -o $(P).mxe $(P).o $(P).def $(MAXLD_LOC32) $(MAXLD_FLAGS)