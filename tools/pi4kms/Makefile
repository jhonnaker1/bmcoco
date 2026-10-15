#
# Builds the PC tests for the direct display drivers: the Pi 4's
# (src/pi4kms/, see test.cpp) and the Pi 5's display lists and PHY
# settings (src/pi5kms/, test_pi5.cpp).
#
#   tools/pi4kms/run_tests.sh
#

ROOT := ../..
CXX ?= c++
CXXFLAGS = -O2 -g -Wall -Wextra -std=c++17
SRCS := test.cpp test_modes.cpp $(ROOT)/src/pi4kms/filter.cpp \
        $(ROOT)/src/pi4kms/dlist.cpp $(ROOT)/src/pi4kms/modes.cpp

OUT := build

all: $(OUT)/pi4kms_test $(OUT)/pi5kms_test

$(OUT)/pi4kms_test: $(SRCS) tests.h $(wildcard $(ROOT)/src/pi4kms/*.h)
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) -lm

$(OUT)/pi5kms_test: test_pi5.cpp $(ROOT)/src/pi5kms/dlist.cpp $(ROOT)/src/pi5kms/phy.cpp \
                   $(wildcard $(ROOT)/src/pi5kms/*.h)
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) -o $@ test_pi5.cpp $(ROOT)/src/pi5kms/dlist.cpp $(ROOT)/src/pi5kms/phy.cpp

clean:
	rm -rf $(OUT)
