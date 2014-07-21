TARGET=mqtt2can
OBJECTS=mqtt2can.o doublet_filter.o

INCLUDES=
LDLIBS=-lmosquitto


CFLAGS += -W -Wall -Wstrict-prototypes -O2 $(INCLUDES)

all: $(TARGET)
$(TARGET): $(OBJECTS)
clean:
	$(RM) $(TARGET) $(OBJECTS)

.PHONY: all clean
