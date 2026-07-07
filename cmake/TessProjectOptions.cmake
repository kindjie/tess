function(tess_target_warning_options target)
  if(NOT TESS_ENABLE_WARNINGS)
    return()
  endif()

  set(warning_options)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    list(
      APPEND
      warning_options
      -Wall
      -Wextra
      -Wpedantic
      -Wconversion
      -Wsign-conversion
      -Wshadow
      -Wold-style-cast
      -Woverloaded-virtual
      -Wformat=2
      -Wundef
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      list(APPEND warning_options -Wextra-semi)
    endif()
  elseif(MSVC)
    list(APPEND warning_options /W4 /permissive- /EHsc)
  endif()

  if(TESS_WARNINGS_AS_ERRORS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
      list(APPEND warning_options -Werror)
    elseif(MSVC)
      list(APPEND warning_options /WX)
    endif()
  endif()

  target_compile_options(${target} PRIVATE ${warning_options})
endfunction()

function(tess_target_clang_tidy_options target)
  if(NOT TESS_ENABLE_CLANG_TIDY)
    return()
  endif()

  set(clang_tidy_command "${TESS_CLANG_TIDY_EXE}")
  if(TESS_CLANG_TIDY_CONFIG)
    list(
      APPEND
      clang_tidy_command
      "--config-file=${TESS_CLANG_TIDY_CONFIG}"
    )
  endif()

  set_property(
    TARGET ${target}
    PROPERTY CXX_CLANG_TIDY "${clang_tidy_command}"
  )
endfunction()

function(tess_target_cppcheck_options target)
  if(NOT TESS_ENABLE_CPPCHECK)
    return()
  endif()

  set_property(
    TARGET ${target}
    PROPERTY
      CXX_CPPCHECK
        "${TESS_CPPCHECK_EXE}"
        "--enable=warning,portability"
        "--error-exitcode=1"
        "--inline-suppr"
        "--suppress=internalError:${PROJECT_SOURCE_DIR}/include/tess/core/shape.h"
        # cppcheck's parser trips false-positive syntaxErrors on modern
        # constructs in gtest-macro-heavy test files (a different file each
        # release). Tests compile under clang on six other gating presets,
        # so suppress for tests/ as a whole; product headers stay checked.
        "--suppress=syntaxError:${PROJECT_SOURCE_DIR}/tests/*"
        "--suppress=missingIncludeSystem"
        "--std=c++20"
  )
endfunction()

function(tess_target_sanitizer_options target)
  if(NOT TESS_ENABLE_SANITIZERS)
    return()
  endif()

  string(REPLACE "," ";" sanitizer_list "${TESS_SANITIZERS}")
  if("address" IN_LIST sanitizer_list AND "thread" IN_LIST sanitizer_list)
    message(
      FATAL_ERROR
      "TESS_SANITIZERS='${TESS_SANITIZERS}' combines address and thread; "
      "ASan and TSan cannot be linked into the same binary"
    )
  endif()

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(sanitizer_options
      -fsanitize=${TESS_SANITIZERS}
      -fno-omit-frame-pointer
    )
    if("undefined" IN_LIST sanitizer_list)
      # -fno-sanitize-recover makes UBSan findings fatal; without it the
      # runtime prints and continues with exit code 0, so CI cannot fail.
      list(APPEND sanitizer_options -fno-sanitize-recover=undefined)
    endif()
    target_compile_options(${target} PRIVATE ${sanitizer_options})
    target_link_options(${target} PRIVATE ${sanitizer_options})
  else()
    message(
      WARNING
      "TESS_ENABLE_SANITIZERS is only configured for Clang and GCC"
    )
  endif()
endfunction()

function(tess_apply_project_options target)
  tess_target_warning_options(${target})
  tess_target_clang_tidy_options(${target})
  tess_target_cppcheck_options(${target})
  tess_target_sanitizer_options(${target})
endfunction()
