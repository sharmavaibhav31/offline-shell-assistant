BUILD_DIR := build
CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=Release

.PHONY: all clean

all:
	cmake -S . -B $(BUILD_DIR) $(CMAKE_FLAGS)
	cmake --build $(BUILD_DIR) -j$$(nproc) --target myshell ai_worker

clean:
	rm -rf $(BUILD_DIR)
