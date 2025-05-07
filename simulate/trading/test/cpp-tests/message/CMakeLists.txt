file(GLOB message_test_sources CONFIGURE_DEPENDS "*.cpp")

list(APPEND test_sources ${message_test_sources})
set(test_sources ${test_sources} PARENT_SCOPE)