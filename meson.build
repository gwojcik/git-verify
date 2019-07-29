project('git-verify', ['cpp'])
add_global_arguments('-std=c++17', '-O3', language: 'cpp')

git2_lib = meson.get_compiler('cpp').find_library('git2')
pthreads_lib = meson.get_compiler('cpp').find_library('pthread')
yaml_cpp_lib = meson.get_compiler('cpp').find_library('yaml-cpp')
std_fs_lib = meson.get_compiler('cpp').find_library('stdc++fs')

file_list = files('main.cpp', 'configLoader.cpp', 'gitWrapper.cpp', 'taskBase.cpp', 'taskCreator.cpp')

executable('git-verify', file_list,
    dependencies: [git2_lib, pthreads_lib, yaml_cpp_lib, std_fs_lib]
)
