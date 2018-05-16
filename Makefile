# Select proper Makefile for operating system.
# The Windows version is built with the help of Cygwin. 

# In my case, I see CYGWIN_NT-6.1-WOW so we don't check for 
# equal to some value.   Your mileage my vary.

win := $(shell uname | grep CYGWIN)
dar := $(shell uname | grep Darwin)
obsd := $(shell uname | grep OpenBSD)

ifneq ($(win),)
   include Makefile.win
else ifeq ($(dar),Darwin)
   include Makefile.macosx
else ifeq ($(obsd),OpenBSD)
   include Makefile.openbsd
else
   include Makefile.linux
endif
