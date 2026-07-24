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
    # MSVC 19.51 reports C4702 throughout valid constexpr/template branches,
    # including third-party SYSTEM headers. This suppresses that diagnostic
    # only on MSVC; retry without /wd4702 when the gating toolset changes.
    list(APPEND warning_options /W4 /wd4702 /permissive- /EHsc)
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
  # Cppcheck 2.21 crashes in its template simplifier on several valid,
  # template-heavy test instantiations. Analyze the umbrella-header smoke TU;
  # compiler and clang-tidy gates retain per-instantiation coverage. Retry all
  # local targets whenever the supported cppcheck version changes.
  if(NOT target STREQUAL "tess_smoke")
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
        # cppcheck misparses std::byte* as void* in BlockScratch::allocate;
        # std::byte pointer arithmetic is well-defined.
        "--suppress=arithOperationsOnVoidPointer:${PROJECT_SOURCE_DIR}/include/tess/block/block.h"
        # cppcheck's parser trips false-positive syntaxErrors on modern
        # constructs in gtest-macro-heavy test files (a different file each
        # release). Tests compile under clang on six other gating presets,
        # so suppress for tests/ as a whole; product headers stay checked.
        "--suppress=syntaxError:${PROJECT_SOURCE_DIR}/tests/*"
        # cppcheck probes configurations beyond the one the build uses,
        # including optional ECS adapters without their third-party headers;
        # the include-order #errors are intended, not defects.
        "--suppress=preprocessorErrorDirective:${PROJECT_SOURCE_DIR}/include/tess/ecs/entt/entt_adapter.h"
        "--suppress=preprocessorErrorDirective:${PROJECT_SOURCE_DIR}/include/tess/ecs/flecs/flecs_adapter.h"
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
