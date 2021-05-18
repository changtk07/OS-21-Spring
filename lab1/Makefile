CC = g++
CFLAGS = -std=gnu++11

TARGET = linker

all: $(TARGET)

$(TARGET): $(TARGET).cpp
	$(CC) $(CFLAGS) -o $(TARGET) $(TARGET).cpp

clean:
	$(RM) $(TARGET) $(TARGET).o
