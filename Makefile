CC = gcc
CFLAGS = -Wall -O2
TARGET = logscan
PREFIX = /usr/local

all: $(TARGET)

$(TARGET): src/logscan.c
	$(CC) $(CFLAGS) src/logscan.c -o $(TARGET)

install: $(TARGET)
	install -m 0755 $(TARGET) $(PREFIX)/bin/$(TARGET)
	install -m 0644 man/logscan.1 $(PREFIX)/share/man/man1/$(TARGET).1
	mandb || true

uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)
	rm -f $(PREFIX)/share/man/man1/$(TARGET).1
	mandb || true

clean:
	rm -f $(TARGET)
