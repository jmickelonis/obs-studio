cmake_minimum_required(VERSION 3.28...3.30)

find_package(AMF 1.4.29 REQUIRED)

add_executable(obs-amf-test)
add_executable(OBS::amf-test ALIAS obs-amf-test)

if(OS_WINDOWS)
  target_sources(obs-amf-test PRIVATE obs-amf-test.cpp)
  target_link_libraries(
    obs-amf-test
    PRIVATE OBS::COMutils AMF::AMF d3d11 dxgi dxguid
  )
elseif(OS_LINUX)
  find_package(Vulkan REQUIRED)
  target_sources(obs-amf-test PRIVATE obs-amf-test-linux.cpp)
  target_link_libraries(
    obs-amf-test
    dl Vulkan::Vulkan AMF::AMF
  )
endif()

set_target_properties_obs(obs-amf-test PROPERTIES FOLDER plugins/obs-ffmpeg)
