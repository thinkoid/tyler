# -*- mode: makefile; -*-

OSNAME := $(shell uname)

VERSION := 0.1

FT2_CPPFLAGS = $(shell pkg-config --cflags freetype2)
FT2_LIBS     = $(shell pkg-config --libs   freetype2)

PANGO_CPPFLAGS = $(shell pkg-config --cflags pangoxft)
PANGO_LIBS     = $(shell pkg-config --libs   pangoxft)

X11_CPPFLAGS = $(shell pkg-config --cflags x11 xinerama xft)
X11_LIBS     = $(shell pkg-config --libs   x11 xinerama xft)

WARNINGS =										\
	-Wno-unused-function						\
	-Wno-deprecated-declarations

CFLAGS = -g -O3 -std=c89 -W -Wall $(WARNINGS)

CPPFLAGS  = -DXINERAMA -DVERSION=$(VERSION) \
	-I. $(X11_CPPFLAGS) $(FT2_CPPFLAGS) $(PANGO_CPPFLAGS)

LDFLAGS =
LIBS = $(X11_LIBS) $(PANGO_LIBS) $(FT2_LIBS)

INSTALLDIR = ~/bin

DEPENDDIR = ./.deps
DEPENDFLAGS = -M

SRCS := $(wildcard *.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))

TARGET = tyler

all: $(TARGET)

DEPS = $(patsubst %.o,$(DEPENDDIR)/%.d,$(OBJS))
-include $(DEPS)

$(DEPENDDIR)/%.d: %.c $(DEPENDDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPENDFLAGS) $< >$@

$(DEPENDDIR):
	@[ ! -d $(DEPENDDIR) ] && mkdir -p $(DEPENDDIR)

%: %.c

%.o: %.c
	${CC} -c ${CPPFLAGS} ${CFLAGS} $<

tyler: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	@rm -rf $(TARGET) $(OBJS)

realclean:
	@rm -rf $(TARGET) $(OBJS) $(DEPENDDIR)

install: tyler
	install $< $(INSTALLDIR)
