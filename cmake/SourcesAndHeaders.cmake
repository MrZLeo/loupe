set(sources
  src/log_parser.cpp
  src/markdown_text.cpp
  src/structured_text.cpp
)

set(exe_sources
  src/main.cpp
  ${sources}
)

set(headers
  include/project/log_message.hpp
  include/project/log_parser.hpp
  include/project/markdown_text.hpp
  include/project/structured_text.hpp
)

set(test_sources
  src/log_parser_test.cpp
  src/markdown_text_test.cpp
  src/structured_text_test.cpp
)
