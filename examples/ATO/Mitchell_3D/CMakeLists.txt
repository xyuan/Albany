# 
# 1. Copy input file from source to binary dir
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/element_oc.xml ${CMAKE_CURRENT_BINARY_DIR}/element_oc.xml COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodal_oc.xml ${CMAKE_CURRENT_BINARY_DIR}/nodal_oc.xml COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/element_nlopt.xml ${CMAKE_CURRENT_BINARY_DIR}/element_nlopt.xml COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodal_nlopt.xml ${CMAKE_CURRENT_BINARY_DIR}/nodal_nlopt.xml COPYONLY)
# 2. Copy mesh files from source to binary dir
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/mitchell3D.gen     ${CMAKE_CURRENT_BINARY_DIR}/mitchell3D.gen COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/mitchell3D.gen.4.0 ${CMAKE_CURRENT_BINARY_DIR}/mitchell3D.gen.4.0 COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/mitchell3D.gen.4.1 ${CMAKE_CURRENT_BINARY_DIR}/mitchell3D.gen.4.1 COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/mitchell3D.gen.4.2 ${CMAKE_CURRENT_BINARY_DIR}/mitchell3D.gen.4.2 COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/mitchell3D.gen.4.3 ${CMAKE_CURRENT_BINARY_DIR}/mitchell3D.gen.4.3 COPYONLY)

# 3. Copy runtest.cmake from source to binary dir
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/runtest.cmake ${CMAKE_CURRENT_BINARY_DIR}/runtest.cmake COPYONLY)

# 5. Name the test with the directory name
get_filename_component(testName ${CMAKE_CURRENT_SOURCE_DIR} NAME)

# 6. Run the "runtest.cmake" file in the CWD
add_test(NAME ${testName} COMMAND ${CMAKE_COMMAND} "-DTEST_PROG=${Albany.exe}"
  -DTEST_NAME=${testName} -P runtest.cmake)

