
target_sources(
  obs-studio
  PRIVATE
    boxshadowrenderer.cpp
    boxshadowrenderer.h
    metrics.h
    propertynames.cpp
    propertynames.h
    shadowhelper.cpp
    shadowhelper.h
    styleconfigdata.h
    tileset.cpp
    tileset.h
)

find_package(KF6 REQUIRED COMPONENTS WindowSystem)
target_link_libraries(
  obs-studio
  PRIVATE
    KF6::WindowSystem
)
