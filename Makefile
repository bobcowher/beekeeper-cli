CC = g++
CFLAGS = -std=c++17 -Iinclude -pthread
TARGET = beekeeper

all: $(TARGET)

$(TARGET): main.cpp
	$(CC) $(CFLAGS) main.cpp -o $(TARGET)

clean:
	rm -f $(TARGET)
