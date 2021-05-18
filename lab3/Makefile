SOURCES := $(wildcard *.cpp)
OBJECTS := $(patsubst %.cpp,%.o,$(SOURCES))
DEPENDS := $(patsubst %.cpp,%.d,$(SOURCES))

WARNING := -Wall -Wextra
STD := -std=gnu++11

.PHONY: all clean

all: mmu

clean:
	$(RM) $(OBJECTS) $(DEPENDS) mmu

mmu: $(OBJECTS)
	$(CXX) $(WARNING) $(CXXFLAGS) $^ -o $@

-include $(DEPENDS)

%.o: %.cpp Makefile
	$(CXX) $(WARNING) $(STD) $(CXXFLAGS) -MMD -MP -c $< -o $@
