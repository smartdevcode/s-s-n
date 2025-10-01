file(GLOB sources CONFIGURE_DEPENDS "*.cpp")
add_library(ipc STATIC ${sources})
add_library(taosim::ipc ALIAS ipc)

target_include_directories(ipc PUBLIC include)

target_link_libraries(ipc PRIVATE fmt::fmt)

find_library(RT_LIBRARY rt)
if(RT_LIBRARY)
    target_link_libraries(ipc PRIVATE ${RT_LIBRARY})
endif()