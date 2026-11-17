# Builds the randomizer mod for the Super Mario Sunshine PC port:
#
#   cmake -S . -B build -DSMS_MODS_OUT=/path/to/the/game/mods
#   cmake --build build
#
# With -DFETCHCONTENT_SOURCE_DIR_SMS_MOD_SDK=<path> it builds against a checkout of the SDK.
cmake_minimum_required(VERSION 3.16)
project(sms_randomizer CXX)

include(FetchContent)
FetchContent_Declare(sms_mod_sdk
  GIT_REPOSITORY https://github.com/chasem-dev/sms-mod-sdk.git
  GIT_TAG v1.8.0-beta.3
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(sms_mod_sdk)

sms_add_mod(mod src/randomizer.cpp)
set_target_properties(${SMS_MOD_TARGET} PROPERTIES CXX_STANDARD 11)
if(MSVC)
  target_compile_options(${SMS_MOD_TARGET} PRIVATE /W3 /D_CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(${SMS_MOD_TARGET} PRIVATE -Wall -Wextra -O2)
endif()
