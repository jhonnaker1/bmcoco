#
# Builds the PC tests for the direct display drivers: the Pi 4's
# (src/kms/pi4/, see test.cpp) and the Pi 5's display lists and PHY
# settings (src/kms/pi5/, test_pi5.cpp).
#
#   tools/pi4kms/run_tests.sh
#

ROOT := ../..
CXX ?= c++
CXXFLAGS = -O2 -g -Wall -Wextra -std=c++17
SRCS := test.cpp test_modes.cpp $(ROOT)/src/kms/common/filter.cpp \
        $(ROOT)/src/kms/pi4/dlist.cpp $(ROOT)/src/kms/pi4/phy.cpp \
        $(ROOT)/src/kms/common/modes.cpp

OUT := build

all: $(OUT)/pi4kms_test $(OUT)/pi5kms_test

$(OUT)/pi4kms_test: $(SRCS) tests.h $(wildcard $(ROOT)/src/kms/pi4/*.h $(ROOT)/src/kms/common/*.h)
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) -lm

$(OUT)/pi5kms_test: test_pi5.cpp $(ROOT)/src/kms/pi5/dlist.cpp $(ROOT)/src/kms/pi5/phy.cpp \
                   $(wildcard $(ROOT)/src/kms/pi5/*.h)
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) -o $@ test_pi5.cpp $(ROOT)/src/kms/pi5/dlist.cpp $(ROOT)/src/kms/pi5/phy.cpp

clean:
	rm -rf $(OUT)
