cmake_minimum_required(VERSION 3.28...3.30)

add_executable(obs-process-priority)
add_executable(OBS::process-priority ALIAS obs-process-priority)
target_sources(obs-process-priority PRIVATE process-priority.cpp)
set_target_properties_obs(obs-process-priority)
