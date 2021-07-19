CXX = clang++
CPPFLAGS =        # put pre-processor settings (-I, -D, etc) here
CXXFLAGS = -std=c++2a -Ofast -march=skylake -DNDEBUG
CXXFLAGSDEBUG = -std=c++2a -g -march=skylake
LDFLAGS =         # put linker settings here

CarlSAT: CarlSAT.o
	$(CXX) -o $@ $(CXXFLAGS) $(LDFLAGS) CarlSAT.o

.cpp.o:
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $<

CarlSAT.cpp: CarlSAT.hpp

clean:
	$(RM) *.o *.d CarlSAT
