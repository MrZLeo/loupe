function(project_detect_cxx_standard out_var)
  if("cxx_std_26" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    set(_project_cxx_standard 26)
  elseif("cxx_std_23" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    set(_project_cxx_standard 23)
  elseif("cxx_std_20" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    set(_project_cxx_standard 20)
  else()
    message(
      FATAL_ERROR
      "Compiler ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} does not support C++20. "
      "Please use a compiler with at least C++20 support."
    )
  endif()

  message(STATUS "Using C++${_project_cxx_standard}")
  set(${out_var} ${_project_cxx_standard} PARENT_SCOPE)
endfunction()

function(project_apply_cxx_standard target visibility)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "Cannot apply the project C++ standard to unknown target `${target}`.")
  endif()

  set_target_properties(
    ${target}
    PROPERTIES
      CXX_STANDARD ${PROJECT_CXX_STANDARD}
      CXX_STANDARD_REQUIRED ON
      CXX_EXTENSIONS OFF
  )

  target_compile_features(${target} ${visibility} cxx_std_${PROJECT_CXX_STANDARD})
endfunction()
