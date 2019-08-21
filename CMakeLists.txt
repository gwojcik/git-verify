cmake_minimum_required(VERSION 3.11)
PROJECT(git-verify VERSION 1.0.0 LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GIT2 libgit2 REQUIRED)
# TODO require pstreams

add_executable(git-verify main.cpp taskBase.cpp configLoader.cpp gitWrapper.cpp taskCreator.cpp)

target_compile_features(git-verify PRIVATE cxx_std_17)

target_compile_options(git-verify PRIVATE -O3 -Wall -Wextra -std=c++17)

# target_include_directories(git-verify PRIVATE /mnt/nvme/test/libgit2/include) # TODO rm

target_link_libraries(git-verify pthread yaml-cpp  stdc++fs)
# target_link_libraries(git-verify /mnt/nvme/test/libgit2/build/libgit2.so) # TODO rm
target_link_libraries(git-verify ${GIT2_LIBRARIES} )

install(TARGETS git-verify RUNTIME DESTINATION bin)
