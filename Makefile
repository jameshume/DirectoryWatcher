.PHONY: all
all: monitor_fs

CXXFLAGS += -std=c++11 -Wall -Wextra

monitor_fs : monitor_fs.cpp

.PHONY: clean
clean:
	rm monitor_fs