CXX ?= g++
CFLAGS := -Wall -pedantic -std=c++14 -O2

LIBS := -lKameMix -lpthread
KameMix_INCDIR := ../../include
KameMix_LIBDIR := ../KameMix

SRCDIR := ../../test
SRCS := kame_mix_test.cpp

DEPDIR := deps
DEPS := $(patsubst %.cpp,$(DEPDIR)/%.makefile,$(SRCS))

ODIR := build
OBJS := $(patsubst %.cpp,$(ODIR)/%.o,$(SRCS))

KameMixTest: $(OBJS) sound libKameMix.so
	$(CXX) -o $@ -I$(KameMix_INCDIR)  -Wl,-rpath,. \
		$(CFLAGS) $(OBJS) -L$(KameMix_LIBDIR) $(LIBS)

sound:
	ln -s ../../test/sound sound

libKameMix.so:
	ln -s ../KameMix/libKameMix.so libKameMix.so

$(DEPDIR):
	mkdir $@

$(ODIR):
	mkdir $@

-include $(DEPS)

$(DEPDIR)/%.makefile: | $(DEPDIR)
	$(CXX) $(CFLAGS) -I$(KameMix_INCDIR) -MM $(SRCDIR)/$*.cpp \
		-MT "$(ODIR)/$*.o $@" > $@
	
$(ODIR)/%.o: | $(ODIR)
	$(CXX) -o $@ -I$(KameMix_INCDIR) -c $(SRCDIR)/$*.cpp $(CFLAGS) 
		
.PHONY: clean
clean:
	rm -f $(DEPS)
	rm -f $(OBJS)
	rm -f KameMixTest
	rm -f libKameMix.so
