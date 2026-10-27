cmake_minimum_required(VERSION 3.16)
project(mempressure
  VERSION 0.2.0
  DESCRIPTION "libmempressure: a Linux answer to Android's onTrimMemory"
  HOMEPAGE_URL "https://github.com/kutuso/libmempressure"
  LANGUAGES C)

option(MP_WERROR "treat compiler warnings as errors" OFF)
option(MP_BUILD_TESTS "build the test suites, examples and ctest entries" ON)
set(MP_WARN_FLAGS -Wall -Wextra)
if(MP_WERROR)
  list(APPEND MP_WARN_FLAGS -Werror)
endif()

if(MP_BUILD_TESTS)
  enable_language(CXX)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_library(mempressure SHARED src/mempressure.c)
target_include_directories(mempressure PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_options(mempressure PRIVATE ${MP_WARN_FLAGS})
target_link_libraries(mempressure PRIVATE Threads::Threads)
set_target_properties(mempressure PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION 0
  C_VISIBILITY_PRESET hidden)

include(GNUInstallDirs)
install(TARGETS mempressure)
install(FILES include/mempressure.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES bindings/c++/mempressure.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

foreach(dir LIBDIR INCLUDEDIR)
  if(IS_ABSOLUTE "${CMAKE_INSTALL_${dir}}")
    set(MP_PC_${dir} "${CMAKE_INSTALL_${dir}}")
  else()
    set(MP_PC_${dir} "\${prefix}/${CMAKE_INSTALL_${dir}}")
  endif()
endforeach()
configure_file(mempressure.pc.in ${PROJECT_BINARY_DIR}/mempressure.pc @ONLY)
install(FILES ${PROJECT_BINARY_DIR}/mempressure.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

enable_testing()

if(MP_BUILD_TESTS)
  add_executable(mp_test tests/test_core.c)
  target_link_libraries(mp_test PRIVATE mempressure)
  target_compile_options(mp_test PRIVATE ${MP_WARN_FLAGS})
  add_test(NAME core COMMAND mp_test)

  find_program(MP_NM nm)
  if(MP_NM)
    add_test(NAME symbols COMMAND sh ${PROJECT_SOURCE_DIR}/tests/check_symbols.sh $<TARGET_FILE:mempressure>)
  endif()

  add_test(NAME versions COMMAND sh ${PROJECT_SOURCE_DIR}/tests/check_versions.sh ${PROJECT_SOURCE_DIR})

  add_executable(mp_test_cpp tests/test_cpp.cpp)
  target_link_libraries(mp_test_cpp PRIVATE mempressure)
  target_include_directories(mp_test_cpp PRIVATE ${PROJECT_SOURCE_DIR}/bindings/c++)
  target_compile_options(mp_test_cpp PRIVATE ${MP_WARN_FLAGS})
  add_test(NAME cpp COMMAND mp_test_cpp)

  add_executable(mp_example_c examples/c/basic.c)
  target_link_libraries(mp_example_c PRIVATE mempressure)
  target_compile_options(mp_example_c PRIVATE ${MP_WARN_FLAGS})

  add_executable(mp_example_cpp examples/c++/basic.cpp)
  target_link_libraries(mp_example_cpp PRIVATE mempressure)
  target_include_directories(mp_example_cpp PRIVATE ${PROJECT_SOURCE_DIR}/bindings/c++)
  target_compile_options(mp_example_cpp PRIVATE ${MP_WARN_FLAGS})
endif()

set(MP_PYTHON "AUTO" CACHE STRING "python binding: ON, OFF or AUTO")
set_property(CACHE MP_PYTHON PROPERTY STRINGS ON OFF AUTO)
if(NOT MP_PYTHON STREQUAL "OFF")
  find_package(Python3 QUIET COMPONENTS Interpreter Development)
  if(Python3_Development_FOUND)
    Python3_add_library(mempressure_python MODULE WITH_SOABI
      bindings/python/mempressure_module.c)
    target_link_libraries(mempressure_python PRIVATE mempressure)
    target_compile_options(mempressure_python PRIVATE ${MP_WARN_FLAGS})
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
    target_compile_options(mempressure_jni PRIVATE ${MP_WARN_FLAGS})
    find_package(Java QUIET COMPONENTS Development)
    if(MP_BUILD_TESTS AND Java_Development_FOUND)
      set(JAVA_CLASSES ${PROJECT_BINARY_DIR}/java-classes)
      file(MAKE_DIRECTORY ${JAVA_CLASSES})
    add_custom_command(
      OUTPUT ${JAVA_CLASSES}/io/kutu/mempressure/MemPressure.class
      COMMAND ${Java_JAVAC_EXECUTABLE} -d ${JAVA_CLASSES}
              ${PROJECT_SOURCE_DIR}/bindings/jvm/java/io/kutu/mempressure/MemPressure.java
              ${PROJECT_SOURCE_DIR}/bindings/jvm/test/MemPressureTest.java
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
