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
  include/loupe/log_message.hpp
  include/loupe/log_parser.hpp
  include/loupe/markdown_text.hpp
  include/loupe/scroll.hpp
  include/loupe/search.hpp
  include/loupe/structured_text.hpp
)

set(test_sources
  src/log_parser_test.cpp
  src/markdown_text_test.cpp
  src/scroll_test.cpp
  src/search_test.cpp
  src/structured_text_test.cpp
)
