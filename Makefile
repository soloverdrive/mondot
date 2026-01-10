TARGET := mondot

CXX := g++
CXX_STANDARD := -std=c++20

SRC_DIR := src
BUILD_DIR := build

SRCS := $(shell find $(SRC_DIR) -name '*.cpp')
OBJS := $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

INCLUDES := -I$(SRC_DIR)

WARNINGS := -Wall -Wextra -Wpedantic
DEBUG_FLAGS   := -O0 -g -DDEBUG
RELEASE_FLAGS := -O3 -DNDEBUG

CXXFLAGS := $(CXX_STANDARD) $(WARNINGS) $(RELEASE_FLAGS)

all: release

release: CXXFLAGS := $(CXX_STANDARD) $(WARNINGS) $(RELEASE_FLAGS)
release: $(TARGET)

debug: CXXFLAGS := $(CXX_STANDARD) $(WARNINGS) $(DEBUG_FLAGS)
debug: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

rebuild: clean all

.PHONY: all debug release clean rebuild
