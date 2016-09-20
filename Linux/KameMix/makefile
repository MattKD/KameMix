CXX ?= g++
CFLAGS := -Wall -pedantic -std=c++14 -O2

LIBS := -lSDL2 -lvorbisfile -lpthread
SDL_INCDIR ?= /usr/include/SDL2

INCDIR := ../../include
SRCDIR := ../../src
SRCS := audio_system.cpp sound_buffer.cpp stream_buffer.cpp vorbis_helper.cpp

DEPDIR := deps
DEPS := $(patsubst %.cpp,$(DEPDIR)/%.makefile,$(SRCS))

ODIR := build
OBJS := $(patsubst %.cpp,$(ODIR)/%.o,$(SRCS))

libKameMix.so: $(OBJS) 
	$(CXX) -shared -Wl,-soname,$@ -o $@ $(CFLAGS) $(OBJS) $(LIBS)

$(DEPDIR):
	mkdir $@

$(ODIR):
	mkdir $@

-include $(DEPS)

$(DEPDIR)/%.makefile: | $(DEPDIR)
	$(CXX) $(CFLAGS) -I$(INCDIR) -I$(SDL_INCDIR) -MM $(SRCDIR)/$*.cpp \
		-MT "$(ODIR)/$*.o $@" > $@
	
$(ODIR)/%.o: | $(ODIR)
	$(CXX) -o $@ -I$(INCDIR) -I$(SDL_INCDIR) -fpic -c $(SRCDIR)/$*.cpp \
		$(CFLAGS) 
		
.PHONY: clean
clean:
	rm -f $(DEPS)
	rm -f $(OBJS)
	rm -f libKameMix.so
