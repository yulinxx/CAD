cmake_minimum_required(VERSION 3.21)

project(SanYiClient__CLIENT_PASCAL__ LANGUAGES CXX)

if(NOT TARGET UICommon)
    message(FATAL_ERROR "UICommon target is required; add Clients/ after Main/ in the root CMakeLists.txt")
endif()

sanyi_add_client_plugin(SanYiClient__CLIENT_PASCAL__
    CLIENT_ID __CLIENT_ID__
    SOURCES
        src/ClientPlugin.cpp
)
