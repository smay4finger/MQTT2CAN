TARGET=mqtt2can
OBJECTS=mqtt2can.o doublet_filter.o

INCLUDES=
LDLIBS=-lmosquitto


CFLAGS += -W -Wall -Wstrict-prototypes -Werror -std=c11 -D_GNU_SOURCE -O2 $(INCLUDES)

all: $(TARGET)
$(TARGET): $(OBJECTS)
clean:
	$(RM) $(TARGET) $(OBJECTS)

.PHONY: all clean
