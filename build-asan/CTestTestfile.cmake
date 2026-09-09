# CMake generated Testfile for 
# Source directory: /run/media/ixaxaar/src/code/src/ORGS/kutuso/libmempressure
# Build directory: /run/media/ixaxaar/src/code/src/ORGS/kutuso/libmempressure/build-asan
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(core "/run/media/ixaxaar/src/code/src/ORGS/kutuso/libmempressure/build-asan/mp_test")
set_tests_properties(core PROPERTIES  _BACKTRACE_TRIPLES "/run/media/ixaxaar/src/code/src/ORGS/kutuso/libmempressure/CMakeLists.txt;34;add_test;/run/media/ixaxaar/src/code/src/ORGS/kutuso/libmempressure/CMakeLists.txt;0;")
add_test(cpp "/run/media/ixaxaar/src/code/src/ORGS/kutuso/libmempressure/build-asan/mp_test_cpp")
set_tests_properties(cpp PROPERTIES  _BACKTRACE_TRIPLES "/run/media/ixaxaar/src/code/src/ORGS/kutuso/libmempressure/CMakeLists.txt;40;add_test;/run/media/ixaxaar/src/code/src/ORGS/kutuso/libmempressure/CMakeLists.txt;0;")
add_test(java "/usr/bin/java" "-Djava.library.path=/run/media/ixaxaar/src/code/src/ORGS/kutuso/libmempressure/build-asan" "-cp" "/run/media/ixaxaar/src/code/src/ORGS/kutuso/libmempressure/build-asan/java-classes" "MemPressureTest")
set_tests_properties(java PROPERTIES  ENVIRONMENT "LD_LIBRARY_PATH=/run/media/ixaxaar/src/code/src/ORGS/kutuso/libmempressure/build-asan" _BACKTRACE_TRIPLES "/run/media/ixaxaar/src/code/src/ORGS/kutuso/libmempressure/CMakeLists.txt;82;add_test;/run/media/ixaxaar/src/code/src/ORGS/kutuso/libmempressure/CMakeLists.txt;0;")
