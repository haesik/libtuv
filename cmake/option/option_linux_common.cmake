# Copyright 2015 Samsung Electronics Co., Ltd.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


set(FLAGS_COMMON
      "-fno-builtin"
      )

set(FLAGS_CXXONLY
      "-fpermissive"
      "-fno-exceptions"
      "-fno-rtti"
      )

set(CMAKE_C_FLAGS_DEBUG     "-O0 -g -DDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE   "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

# override TUV_PLATFORM_PATH for Linux
# use "linux" for Linux platform dependent source files
set(TUV_PLATFORM_PATH ${PLATFORM_NAME_L})

# unix common source files
set(UNIX_PATH "${SOURCE_ROOT}/unix")

set(PLATFORM_SRCFILES "${UNIX_PATH}/uv_unix.cpp"
                      "${UNIX_PATH}/uv_unix_async.cpp"
                      "${UNIX_PATH}/uv_unix_io.cpp"
                      "${UNIX_PATH}/uv_unix_process.cpp"
                      "${UNIX_PATH}/uv_unix_thread.cpp"
                      )

# linux specific source files
set(LINUX_PATH "${SOURCE_ROOT}/${TUV_PLATFORM_PATH}")

set(PLATFORM_SRCFILES ${PLATFORM_SRCFILES}
                      "${LINUX_PATH}/uv_linux.cpp"
                      "${LINUX_PATH}/uv_linux_loop.cpp"
                      "${LINUX_PATH}/uv_linux_clock.cpp"
                      "${LINUX_PATH}/uv_linux_fs.cpp"
                      "${LINUX_PATH}/uv_linux_io.cpp"
                      "${LINUX_PATH}/uv_linux_syscall.cpp"
                      "${LINUX_PATH}/uv_linux_thread.cpp"
                      )

set(PLATFORM_TESTFILES "${TEST_ROOT}/runner_linux.cpp"
                       "${TEST_ROOT}/test_fs.cpp"
)

set(TUV_LINK_LIBS "pthread")
