CXX      = g++
CXXFLAGS = -std=c++17 -O2 \
           -ID:/mingw64/MinGW/include \
           -DBOOST_THREAD_USE_LIB \
           -D_WIN32_WINNT=0x0601
LDFLAGS  = -LD:/mingw64/MinGW/lib \
           -lboost_json \
           -lboost_system \
           -lboost_thread \
           -lws2_32 -lwsock32 -lpthread
TARGET   = drone_backend.exe
SRC      = src/main.cpp

$(TARGET): $(SRC) src/config.hpp src/models.hpp src/registry.hpp \
           src/udp_stub.hpp src/ws_manager.hpp
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	del /Q $(TARGET) 2>NUL || rm -f $(TARGET)

.PHONY: clean
