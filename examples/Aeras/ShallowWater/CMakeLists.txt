 
add_subdirectory(Sphere10)
add_subdirectory(Sphere15)
add_subdirectory(Planar)
add_subdirectory(TC2)
add_subdirectory(TC5)
add_subdirectory(TC6)
add_subdirectory(TC3)
add_subdirectory(TCGalewsky)
add_subdirectory(TC4)
add_subdirectory(TC7)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/TestCase5.xml
               ${CMAKE_CURRENT_BINARY_DIR}/TestCase5.xml COPYONLY)

