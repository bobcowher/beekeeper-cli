CC = g++
CFLAGS = -std=c++17 -Iinclude -pthread
TARGET = beekeeper

all: $(TARGET)

$(TARGET): beekeeper.cpp
	$(CC) $(CFLAGS) beekeeper.cpp -o $(TARGET)

clean:
	rm -f $(TARGET)
