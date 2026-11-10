add_executable(AprLicenseExample
    main.cpp
)

target_compile_features(AprLicenseExample PRIVATE cxx_std_17)

target_link_libraries(AprLicenseExample PRIVATE
    License
)

if(MSVC)
    target_compile_options(AprLicenseExample PRIVATE /utf-8)
endif()

set_target_properties(AprLicenseExample PROPERTIES FOLDER "Examples")

message(STATUS "  Example: AprLicenseExample")
