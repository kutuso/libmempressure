BUILD ?= build
PYTHON ?= python3
CTEST ?= ctest
PYEXE = $(shell command -v $(PYTHON))

.PHONY: build test asan asan-run clean

build:
	cmake -S . -B $(BUILD) -DMP_WERROR=ON -DPython3_EXECUTABLE=$(PYEXE)
	cmake --build $(BUILD)

test: build
	$(CTEST) --test-dir $(BUILD) --output-on-failure
	MP_BUILD_DIR=$(abspath $(BUILD)) $(PYTHON) -m pytest tests/python -q

asan:
	$(MAKE) asan-run

asan-run:
	cmake -S . -B build-asan -DMP_WERROR=ON -DPython3_EXECUTABLE=$(PYEXE) \
	  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
	  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
	  -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined" \
	  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
	cmake --build build-asan
	LD_PRELOAD=$$(cc -print-file-name=libasan.so) ASAN_OPTIONS=detect_leaks=0 \
	  $(CTEST) --test-dir build-asan --output-on-failure
	MP_BUILD_DIR=$(abspath build-asan) LD_PRELOAD=$$(cc -print-file-name=libasan.so) \
	  ASAN_OPTIONS=detect_leaks=0 $(PYTHON) -m pytest tests/python -q

clean:
	rm -rf build build-asan
