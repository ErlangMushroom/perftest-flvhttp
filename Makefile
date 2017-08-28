.PHONY : all clean

CROSS_COMPILE :=

CC := $(CROSS_COMPILE)g++

TARGET := perftest
SRCS := $(wildcard *.cpp)
OBJS := $(patsubst %.cpp, %.o, $(SRCS))

INCLUDE_FLAGS := -Iurdl/include
INCLUDE_FLAGS += -Iurdl/include/urdl

CXXFLAGS += -O3 -DURDL_HEADER_ONLY $(INCLUDE_FLAGS)

all : $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS) -lboost_system -lboost_program_options -lboost_thread -lboost_chrono -lpthread

clean :
	-rm -rf *.o $(TARGET)
