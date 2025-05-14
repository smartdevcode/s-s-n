macro(collect_test_sources)
    file(GLOB local_test_sources CONFIGURE_DEPENDS "*.cpp")
    set(test_sources ${test_sources} ${local_test_sources} PARENT_SCOPE)
endmacro()
