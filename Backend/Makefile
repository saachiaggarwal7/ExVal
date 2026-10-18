CXX=g++

# Detect macOS vs Linux and set MySQL flags accordingly
UNAME := $(shell uname)
ifeq ($(UNAME), Darwin)
  MYSQL_CFLAGS := -I$(shell mysql_config --variable=pkgincludedir)/..
  MYSQL_LIBS   := -L/opt/homebrew/lib $(shell mysql_config --libs)
else
  MYSQL_CFLAGS := -I/usr/include/mariadb
  MYSQL_LIBS   := -lmariadb
endif

CXXFLAGS=-std=c++17 -Wall -Wextra -pthread $(MYSQL_CFLAGS)
LIBS=$(MYSQL_LIBS)
TARGET=exval_server
SRC=main.cpp common.cpp auth.cpp student.cpp examiner.cpp admin.cpp code_runner.cpp database.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET)
