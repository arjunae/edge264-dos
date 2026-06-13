CC = i586-pc-msdosdjgpp-gcc
CXX = i586-pc-msdosdjgpp-g++

CFLAGS = -g -O3 -Wall -Wextra -I../include -march=i586 -msse -msse2 -msse3 -mfpmath=sse -ffast-math -ftree-vectorize -flax-vector-conversions -include dos_stubs.h -std=gnu11 
CXXFLAGS = $(CFLAGS)
LDFLAGS = -lm

TARGET = dosplay.exe
OBJS = dosplay.o vesa.o edge264.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

dosplay.o: dosplay.c
	$(CC) $(CFLAGS) -c $< -o $@

vesa.o: vesa.cc vesa.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

edge264.o: ../src/edge264.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
