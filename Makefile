CC      = g++
CC_WIN  = x86_64-w64-mingw32-g++
CFLAGS  = -std=c++17 -Iinclude -pthread
TARGET  = beekeeper

.PHONY: all linux windows clean

all: linux windows

linux: beekeeper.cpp version.h
	$(CC) $(CFLAGS) beekeeper.cpp -o $(TARGET)

windows: beekeeper.cpp version.h
	$(CC_WIN) $(CFLAGS) -static beekeeper.cpp -o $(TARGET).exe -lws2_32

clean:
	rm -f $(TARGET) $(TARGET).exe
