add_executable(lunasvg-tests test_filter.cpp)
target_link_libraries(lunasvg-tests PRIVATE lunasvg::lunasvg)
add_test(NAME lunasvg-tests COMMAND lunasvg-tests)
