project('hellocpp', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3',
                     'cpp_std=c++17'])

exe = executable('hellocpp', 'hellocpp.cpp', 'McfAgent.cpp', 'RestAgent.cpp',
  install : true)

test('basic', exe)
