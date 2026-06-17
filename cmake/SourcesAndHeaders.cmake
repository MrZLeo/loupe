set(sources
  src/log_parser.cpp
  src/markdown_text.cpp
  src/scroll.cpp
  src/search.cpp
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
  include/project/scroll.hpp
  include/project/search.hpp
  include/project/structured_text.hpp
)

set(test_sources
  src/log_parser_test.cpp
  src/markdown_text_test.cpp
  src/scroll_test.cpp
  src/search_test.cpp
  src/structured_text_test.cpp
)
