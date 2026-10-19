#
# Builds the PC tests for the shared Pi 4/5 ARM smooth scaling and CRT shader
# (src/av/scaler.*, see test.cpp) and for sharing a frame between cores
# (src/av/frameshare.h, see test_share.cpp).
#
#   tools/pi4scaler/run_tests.sh
#

ROOT := ../..
CXX ?= c++
# -O3, as the Pi 4 build compiles scaler.cpp (see the Makefile at the top).
CXXFLAGS = -O3 -g -Wall -std=c++17

OUT := build

all: $(OUT)/pi4scaler_test $(OUT)/share_test

$(OUT)/pi4scaler_test: test.cpp $(ROOT)/src/av/scaler.cpp $(ROOT)/src/av/scaler.h
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) -o $@ test.cpp $(ROOT)/src/av/scaler.cpp -lm

# The frame sharing between cores, with threads, under ThreadSanitizer.
$(OUT)/share_test: test_share.cpp $(ROOT)/src/av/frameshare.h
	@mkdir -p $(OUT)
	$(CXX) -O2 -g -Wall -std=c++17 -fsanitize=thread -o $@ test_share.cpp -pthread

clean:
	rm -rf $(OUT)
