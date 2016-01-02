#
#  Compiling/running tests
#

if (use_context)
   add_definitions(-DUSE_CBOR_CONTEXT)
endif()

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${dist_dir}/test )

add_executable ( cose_test test.c json.c )

target_link_libraries (cose_test PRIVATE cose-c )

## OpenSSL

target_include_directories(cose_test PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries( cose_test PRIVATE ${OPENSSL_LIBRARIES} )
target_link_libraries( cose_test PRIVATE cn-cbor )
if ( MSVC )
    target_link_libraries( cose_test PRIVATE ws2_32 )
endif ()

target_include_directories ( cose_test PRIVATE ../include )
target_include_directories ( cose_test PRIVATE ../src )
target_include_directories ( cose_test PUBLIC ../../cose/cn-cbor/include )
target_include_directories ( cose_test PUBLIC ../../cn-cbor/implement/cn-cbor/include )

enable_testing()

add_test ( NAME cose_test COMMAND cose_test )
add_test ( NAME mac-04 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND cose_test Examples/spec-examples/Mac-04.json )

add_test ( NAME hmac-01 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND cose_test Examples/hmac-examples/HMac-01.json )
add_test ( NAME hmac-02 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND cose_test Examples/hmac-examples/HMac-02.json )
add_test ( NAME hmac-03 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND cose_test Examples/hmac-examples/HMac-03.json )
add_test ( NAME hmac-04 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND cose_test Examples/hmac-examples/HMac-04.json )
add_test ( NAME hmac-05 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND cose_test Examples/hmac-examples/HMac-05.json )

