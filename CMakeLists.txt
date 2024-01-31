# 获取当前目录下源码和头文件
macro(get_src_include)
    aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/src SRC)
    aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/Source SOURCE)

    list(APPEND SRC ${SOURCE})

    # message("SRC = ${SRC}")
    FILE(GLOB H_FILE_I ${CMAKE_CURRENT_LIST_DIR}/include/*.h)
    FILE(GLOB UI_FILES ${CMAKE_CURRENT_LIST_DIR}/src/*.ui)

    if(RC_FILE)
        source_group("Resource Files" FILES ${RC_FILE})
    endif()

    if(UI_FILES)
        qt4_wrap_ui(UIC_HEADER ${UI_FILES})
        source_group("Resource Files" FILES ${UI_FILES})
        source_group("Generate Files" FILES ${UIC_HEADER})
    endif()

    if(QRC_SOURCE_FILES)
        # qt4_add_resources(QRC_FILES ${QRC_SOURCE_FILES})
        # qt4_wrap_cpp()
        source_group("Resource Files" FILES ${QRC_SOURCE_FILES})
    endif()
endmacro()

cmake_minimum_required(VERSION 3.12)

project(ThreadPool)

set(CMAKE_CXX_STANDARD 17)

message(STATUS "================ ${name} cpp_execute =================")
get_src_include()

include_directories(${H_FILE_I})

# 添加执行程序
add_executable(${PROJECT_NAME}
     ${SRC}
     ${H_FILE_I}
     ${UIC_HEADER}
     ${QRC_SOURCE_FILES}
     ${RC_FILE}
)


