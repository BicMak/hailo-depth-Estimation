cmake_minimum_required(VERSION 3.5.0)
project(hailocpp)

# 패키지 찾기
find_package(HailoRT 4.23.0 EXACT REQUIRED)
find_package(OpenCV REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(yaml-cpp REQUIRED)

# GStreamer 패키지 찾기
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-app-1.0)

# 인클루드 디렉토리 설정
include_directories(
    ${GSTREAMER_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    ${YAML_CPP_INCLUDE_DIR}  # yaml-cpp 인클루드 추가
)

# 라이브러리 디렉토리 설정
link_directories(
    ${GSTREAMER_LIBRARY_DIRS}
    ${GLIB_LIBRARY_DIRS}
)

# Hailo 예제 실행파일
add_executable(appsink_infer_pipeline_example 
    main.cpp 
    Hailoinfer.cpp
    gstreaming.cpp
)

target_link_libraries(appsink_infer_pipeline_example PRIVATE 
    HailoRT::libhailort
    ${OpenCV_LIBS}
    ${GSTREAMER_LIBRARIES}
    ${GLIB_LIBRARIES}
    yaml-cpp  # yaml-cpp 라이브러리 링킹 추가
)

if(WIN32)
    target_compile_options(appsink_infer_pipeline_example PRIVATE
        /DWIN32_LEAN_AND_MEAN
        /DNOMINMAX
        /wd4201 /wd4251
    )
endif()

set_target_properties(appsink_infer_pipeline_example PROPERTIES CXX_STANDARD 17)