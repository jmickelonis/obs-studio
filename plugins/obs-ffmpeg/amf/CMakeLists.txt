cmake_minimum_required(VERSION 3.28...3.30)

target_sources(
  obs-ffmpeg
  PRIVATE
    amf.cpp
    encoder.cpp
    fallback.cpp
    module.cpp
    properties.cpp
    settings.cpp
)

if(OS_WINDOWS)
  target_sources(obs-ffmpeg PRIVATE windows.cpp)
  target_link_libraries(obs-ffmpeg PRIVATE AMF::AMF)
else()
  target_sources(obs-ffmpeg PRIVATE linux.cpp)
endif()
