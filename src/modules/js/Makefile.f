CC=g++
LFSFLAGS= $(shell getconf LFS_CFLAGS) -D_GLIBCXX_USE_CXX11_ABI=0
CFLAGS= -fPIC -fvisibility=hidden -g  -Wall -c -D_REENTRANT -I../../../include/ -I./ -I../ -I../../  $(LFSFLAGS)
ifeq ($(BUILDSTATIC), 1)
    ALLLIB := -nodefaultlibs $(shell g++ -print-file-name='libstdc++.a') -lm -lc -lgcc_eh  -lc_nonshared -lgcc
endif

OS := $(shell uname)
ifeq ($(OS), Darwin)
        LDFLAGS=  $(ALLLIB) -fPIC -g -undefined dynamic_lookup  -Wall $(LFSFLAGS) -shared
else
        LDFLAGS=  $(ALLLIB) -fPIC -g -Wall $(LFSFLAGS) -shared
endif

SOURCES = lsjsengine.cpp modjs.cpp

$(shell rm *.o)

OBJECTS=$(SOURCES:.cpp=.o)
TARGET  = mod_js.so

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC)  $(OBJECTS) -o $@  $(LDFLAGS)

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@
        
clean:
	rm *.o
