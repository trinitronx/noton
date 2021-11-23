# Minimalist GNU Makefile for simple build on systems w/pkg-config

LDLIBS ?= -lportmidi
CXXFLAGS:=(shell pkg-config --cflags sdl2) $(CXXFLAGS)
LDLIBS:=$(shell pkg-config --libs sdl2) $(LDLIBS)

all: noton
