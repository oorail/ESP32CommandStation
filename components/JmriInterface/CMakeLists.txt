idf_component_register(
    SRCS JmriInterface.cpp
    INCLUDE_DIRS include
    PRIV_REQUIRES OpenMRNLite Esp32HttpServer DCCppProtocol
)

set_source_files_properties(JmriInterface.cpp PROPERTIES COMPILE_FLAGS "-Wno-implicit-fallthrough -Wno-ignored-qualifiers")