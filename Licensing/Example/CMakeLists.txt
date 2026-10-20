add_executable(SyLicenseExample
    main.cpp
)

target_compile_features(SyLicenseExample PRIVATE cxx_std_17)

target_link_libraries(SyLicenseExample PRIVATE
    License
)

if(MSVC)
    target_compile_options(SyLicenseExample PRIVATE /utf-8)
endif()

set_target_properties(SyLicenseExample PROPERTIES FOLDER "Examples")

message(STATUS "  Example: SyLicenseExample")
