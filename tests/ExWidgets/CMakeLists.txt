find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

add_executable(ExWidgetsNavigationHintTests tst_navigationhint.cpp)
target_link_libraries(ExWidgetsNavigationHintTests PRIVATE
    ExWidgets::ExWidgets
    Qt${QT_VERSION_MAJOR}::Test
)
add_test(NAME ExWidgetsNavigationHintTests COMMAND ExWidgetsNavigationHintTests)
