# Disables C++ exception handling for a target.
#
# MSVC: turns off the /EH exception model and defines _HAS_EXCEPTIONS=0 so the
# standard library selects its no-exceptions code paths. /wd4530 and /wd4577
# silence the warnings MSVC emits for code compiled without unwind semantics.
# GCC/Clang: -fno-exceptions makes try/catch/throw hard errors and lets the
# standard library substitute std::terminate/abort for throws.
#
# Third-party dependencies are intentionally left untouched: simdjson, argum
# and Catch2 auto-detect these flags in the consuming translation units.
function(project_disable_exceptions target)
  if(NOT TARGET ${target})
    message(AUTHOR_WARNING "${target} is not a target, thus exceptions were not disabled.")
    return()
  endif()

  get_target_property(target_type ${target} TYPE)
  if(target_type STREQUAL "INTERFACE_LIBRARY")
    set(visibility INTERFACE)
  else()
    set(visibility PRIVATE)
  endif()

  if(MSVC)
    target_compile_options(${target} ${visibility} /EHs-c- /wd4530 /wd4577)
    target_compile_definitions(${target} ${visibility} _HAS_EXCEPTIONS=0)
  else()
    target_compile_options(${target} ${visibility} -fno-exceptions)
  endif()
endfunction()
