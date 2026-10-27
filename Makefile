BUILD ?= build

.PHONY: build test asan asan-run clean

build:
	cmake -S . -B $(BUILD) -DMP_WERROR=ON
	cmake --build $(BUILD)

test: build
	ctest --test-dir $(BUILD) --output-on-failure
	MP_BUILD_DIR=$(abspath $(BUILD)) python3 -m pytest tests/python -q

asan:
	$(MAKE) asan-run

asan-run:
	cmake -S . -B build-asan -DMP_WERROR=ON \
	  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
	  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
	  -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined" \
	  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
	cmake --build build-asan
	LD_PRELOAD=$$(cc -print-file-name=libasan.so) ASAN_OPTIONS=detect_leaks=0 \
	  ctest --test-dir build-asan --output-on-failure
	MP_BUILD_DIR=$(abspath build-asan) LD_PRELOAD=$$(cc -print-file-name=libasan.so) \
	  ASAN_OPTIONS=detect_leaks=0 python3 -m pytest tests/python -q

clean:
	rm -rf build build-asan
