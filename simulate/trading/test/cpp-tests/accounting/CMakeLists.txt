file(GLOB accounting_test_sources CONFIGURE_DEPENDS "*.cpp")

list(APPEND test_sources ${accounting_test_sources})
set(test_sources ${test_sources} PARENT_SCOPE)
