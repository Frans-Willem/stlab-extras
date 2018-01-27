add_executable(boostcoro2 boostcoro2.cpp)
target_link_libraries(boostcoro2 PUBLIC stlab-extras)

add_executable(observable observable.cpp)
target_link_libraries(observable PUBLIC stlab-extras)
