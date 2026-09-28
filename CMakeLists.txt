cmake_minimum_required(VERSION 3.16)
project(mempressure
  VERSION 0.2.0
  DESCRIPTION "libmempressure: a Linux answer to Android's onTrimMemory"
  LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_library(mempressure SHARED src/mempressure.c)
target_include_directories(mempressure PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_options(mempressure PRIVATE -Wall -Wextra -Werror)
target_link_libraries(mempressure PRIVATE Threads::Threads)
set_target_properties(mempressure PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION 0)

include(GNUInstallDirs)
install(TARGETS mempressure)
install(FILES include/mempressure.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES bindings/c++/mempressure.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_file(mempressure.pc.in ${CMAKE_BINARY_DIR}/mempressure.pc @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/mempressure.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

enable_testing()

add_executable(mp_test tests/test_core.c)
target_link_libraries(mp_test PRIVATE mempressure)
target_compile_options(mp_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME core COMMAND mp_test)

add_executable(mp_test_cpp tests/test_cpp.cpp)
target_link_libraries(mp_test_cpp PRIVATE mempressure)
target_include_directories(mp_test_cpp PRIVATE ${CMAKE_SOURCE_DIR}/bindings/c++)
target_compile_options(mp_test_cpp PRIVATE -Wall -Wextra -Werror)
add_test(NAME cpp COMMAND mp_test_cpp)

add_executable(mp_example_c examples/c/basic.c)
target_link_libraries(mp_example_c PRIVATE mempressure)
target_compile_options(mp_example_c PRIVATE -Wall -Wextra -Werror)

add_executable(mp_example_cpp examples/c++/basic.cpp)
target_link_libraries(mp_example_cpp PRIVATE mempressure)
target_include_directories(mp_example_cpp PRIVATE ${CMAKE_SOURCE_DIR}/bindings/c++)
target_compile_options(mp_example_cpp PRIVATE -Wall -Wextra -Werror)

set(MP_PYTHON "AUTO" CACHE STRING "python binding: ON, OFF or AUTO")
set_property(CACHE MP_PYTHON PROPERTY STRINGS ON OFF AUTO)
if(NOT MP_PYTHON STREQUAL "OFF")
  find_package(Python3 QUIET COMPONENTS Interpreter Development)
  if(Python3_Development_FOUND)
    Python3_add_library(mempressure_python MODULE WITH_SOABI
      bindings/python/mempressure_module.c)
    target_link_libraries(mempressure_python PRIVATE mempressure)
    target_compile_options(mempressure_python PRIVATE -Wall -Wextra)
    set_target_properties(mempressure_python PROPERTIES OUTPUT_NAME mempressure)
    message(STATUS "python binding: enabled")
  elseif(MP_PYTHON STREQUAL "ON")
    message(FATAL_ERROR "MP_PYTHON=ON but Python3::Development was not found")
  else()
    message(STATUS "python binding: disabled (Python3::Development not found)")
  endif()
else()
  message(STATUS "python binding: disabled (MP_PYTHON=OFF)")
endif()

set(MP_JAVA "AUTO" CACHE STRING "jvm/jni binding: ON, OFF or AUTO")
set_property(CACHE MP_JAVA PROPERTY STRINGS ON OFF AUTO)
if(NOT MP_JAVA STREQUAL "OFF")
  find_package(JNI QUIET)
  if(JNI_FOUND)
    add_library(mempressure_jni SHARED bindings/jvm/jni/mempressure_jni.c)
    target_link_libraries(mempressure_jni PRIVATE mempressure Threads::Threads)
    target_include_directories(mempressure_jni PRIVATE ${JNI_INCLUDE_DIRS})
    target_compile_options(mempressure_jni PRIVATE -Wall -Wextra -Werror)
    find_package(Java QUIET COMPONENTS Development)
    if(Java_Development_FOUND)
      set(JAVA_CLASSES ${CMAKE_BINARY_DIR}/java-classes)
      file(MAKE_DIRECTORY ${JAVA_CLASSES})
    add_custom_command(
      OUTPUT ${JAVA_CLASSES}/io/kutu/mempressure/MemPressure.class
      COMMAND ${Java_JAVAC_EXECUTABLE} -d ${JAVA_CLASSES}
              ${CMAKE_SOURCE_DIR}/bindings/jvm/java/io/kutu/mempressure/MemPressure.java
              ${CMAKE_SOURCE_DIR}/bindings/jvm/test/MemPressureTest.java
      DEPENDS bindings/jvm/java/io/kutu/mempressure/MemPressure.java
              bindings/jvm/test/MemPressureTest.java)
      add_custom_target(java_classes ALL
        DEPENDS ${JAVA_CLASSES}/io/kutu/mempressure/MemPressure.class)
      add_test(NAME java
        COMMAND ${Java_JAVA_EXECUTABLE}
          -Djava.library.path=$<TARGET_FILE_DIR:mempressure_jni>
          -cp ${JAVA_CLASSES}
          MemPressureTest)
      set_property(TEST java PROPERTY
        ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:mempressure>")
      message(STATUS "jvm binding: enabled")
    else()
      message(STATUS "jvm binding: native library built, java compiler not found")
    endif()
  elseif(MP_JAVA STREQUAL "ON")
    message(FATAL_ERROR "MP_JAVA=ON but JNI was not found")
  else()
    message(STATUS "jvm binding: disabled (JNI not found)")
  endif()
else()
  message(STATUS "jvm binding: disabled (MP_JAVA=OFF)")
endif()
