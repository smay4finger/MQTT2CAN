TARGET=mqtt2can
OBJECTS=mqtt2can.o

INCLUDES=
LDLIBS=-lmosquitto


CFLAGS += -W -Wall -Wstrict-prototypes -Werror -std=c11 -D_GNU_SOURCE -O2 $(INCLUDES)

all: $(TARGET)
$(TARGET): $(OBJECTS)
clean:
	$(RM) $(TARGET) $(OBJECTS)
install: all
	install --mode 755 mqtt2can $(DESTDIR)/usr/bin/mqtt2can

.PHONY: all clean install
