# 1. Recursively find all C source files down the directory tree
file(GLOB_RECURSE app_sources 
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/main/*.c"
)

# 2. Recursively find all directories containing header files (.h)
file(GLOB_RECURSE header_files "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
set(include_dirs ".")

foreach(header_path ${header_files})
    get_filename_component(header_dir ${header_path} DIRECTORY)
    list(APPEND include_dirs ${header_dir})
endforeach()

# Remove duplicate paths to keep the compiler flags clean
list(REMOVE_DUPLICATES include_dirs)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS ${include_dirs}
)