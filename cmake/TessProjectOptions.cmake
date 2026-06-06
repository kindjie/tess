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
    list(APPEND warning_options /W4)
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

  set_property(
    TARGET ${target}
    PROPERTY CXX_CLANG_TIDY "${TESS_CLANG_TIDY_EXE}"
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
        "--suppress=syntaxError:${PROJECT_SOURCE_DIR}/tests/tess_diagnostics_default_test.cc"
        "--suppress=syntaxError:${PROJECT_SOURCE_DIR}/tests/tess_diagnostics_enabled_test.cc"
        "--suppress=missingIncludeSystem"
        "--std=c++20"
  )
endfunction()

function(tess_target_sanitizer_options target)
  if(NOT TESS_ENABLE_SANITIZERS)
    return()
  endif()

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(
      ${target}
      PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(
      ${target}
      PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
    )
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
