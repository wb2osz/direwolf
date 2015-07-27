# Select proper Makefile for operating system.
# The Windows version is built with the help of Cygwin. 

win := $(shell uname | grep CYGWIN)
ifneq ($(win),)
include Makefile.win
else
include Makefile.linux
endif
