Load Value Injection Optimization Plugin for LLVM* Project
==========================================================

*Other names and brands may be claimed as the property of others.

A document describing this mitigation can be found here:

https://software.intel.com/security-software-guidance/insights/optimized-mitigation-approach-load-value-injection

Prerequisites
-------------
The LVI optimizing plugin has the following dependencies:
- CMake
- SYMPHONY (version 3)

For example, for Ubuntu 16.04+
```bash
$ apt install cmake coinor-libsymphony-dev
```

Build and Install
-----------------
To obtain and build the plugin:
```bash
$ git clone https://github.com/intel/lvi-llvm-optimization-plugin.git
$ cd lvi-llvm-optimization-plugin
$ mkdir build
$ cd build
$ cmake -DCMAKE_BUILD_TYPE=RELEASE ..
$ make
```
(Optional) installation:
```bash
$ sudo make install
```

Usage
---------
This plugin is intended to be used with LLVM/clang LVI load hardening, for
example:
```bash
$ clang -O3 -lvi-hardening my_file.c \
    -mllvm -x86-lvi-load-opt-plugin=<path-to-plugin-install>/OptimizeCut.so
```

At the time of this writing, the patch that enables LVI load hardening can be
found here: https://reviews.llvm.org/D75938.

Debugging
---------
When using a DEBUG build (i.e., `CMAKE_BUILD_TYPE=DEBUG`), this plugin emits
detailed information about the optimizations it is performing.
