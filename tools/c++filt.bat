@echo off
rem Wrapper for MetaModule SDK scripts expecting `c++filt`.
rem Uses the Arm GNU toolchain demangler already on PATH.
arm-none-eabi-c++filt %*
