.PHONY: build test python-test clean

build:
	cmake -S . -B build -DJAVA_HOME=$${JAVA_HOME:-/usr/lib/jvm/default}
	cmake --build build

test: build
	cmake --build build --target test -- -k 2>/dev/null || cmake --build build --target test
	python3 -m pytest tests/python -q

clean:
	rm -rf build
