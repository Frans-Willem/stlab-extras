add_executable(boostcoro2 boostcoro2.cpp)
target_link_libraries(boostcoro2 PUBLIC stlab-extras Boost::coroutine)
