CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude \
         $(shell pkg-config --cflags gtk+-3.0 gio-2.0 cairo)
LDFLAGS = $(shell pkg-config --libs gtk+-3.0 gio-2.0 cairo) -lm

TARGET = basilisk

SRCS = $(wildcard src/*.c)
OBJS = $(patsubst src/%.c, obj/%.o, $(SRCS))

.PHONY: all clean install

all: $(TARGET)
	@echo "✅ Build complete: $(TARGET)"

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

obj/%.o: src/%.c | obj
	$(CC) $(CFLAGS) -c $< -o $@

obj:
	mkdir -p obj

clean:
	rm -rf obj $(TARGET)
	@echo "🧹 Cleaned."

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)/usr/bin/$(TARGET)
	@echo "✅ Installed to $(DESTDIR)/usr/bin/$(TARGET)"

