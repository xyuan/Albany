
# Copy files used by multiple tests
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/materials.xml
               ${CMAKE_CURRENT_BINARY_DIR}/materials.xml COPYONLY)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/output)

# Get testRoot as QCAD_<the directory name>
get_filename_component(subRoot ${CMAKE_CURRENT_SOURCE_DIR} NAME)
set(testRoot "QCAD_${subRoot}")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/input_stk2D.xml
               ${CMAKE_CURRENT_BINARY_DIR}/input_stk2D.xml COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/input_pmoscap_1D.xml
               ${CMAKE_CURRENT_BINARY_DIR}/input_pmoscap_1D.xml COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/input_pmoscap.xml
               ${CMAKE_CURRENT_BINARY_DIR}/input_pmoscap.xml COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/input_pmoscap_polygate.xml
               ${CMAKE_CURRENT_BINARY_DIR}/input_pmoscap_polygate.xml COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/input_pndiode.xml
               ${CMAKE_CURRENT_BINARY_DIR}/input_pndiode.xml COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/input_mosdot_2D.xml
               ${CMAKE_CURRENT_BINARY_DIR}/input_mosdot_2D.xml COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/input_mosdot_2D_tri6.xml
               ${CMAKE_CURRENT_BINARY_DIR}/input_mosdot_2D_tri6.xml COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/input_mosdot_2D_qcad.xml
               ${CMAKE_CURRENT_BINARY_DIR}/input_mosdot_2D_qcad.xml COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/input_mosdot_3D.xml
               ${CMAKE_CURRENT_BINARY_DIR}/input_mosdot_3D.xml COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/input_mosdot_3D_tet10.xml
               ${CMAKE_CURRENT_BINARY_DIR}/input_mosdot_3D_tet10.xml COPYONLY)


# Create tests with this name and serial executable
IF(NOT ALBANY_PARALLEL_ONLY)
  add_test(${testRoot}_test2D_SERIAL ${SerialAlbany.exe} input_stk2D.xml)
  add_test(${testRoot}_pmoscap_1D_SERIAL ${SerialAlbany.exe} input_pmoscap_1D.xml)
ENDIF()

# Create tests with this name and standard executable
add_test(${testRoot}_pmoscap ${Albany.exe} input_pmoscap.xml)
add_test(${testRoot}_pmoscap_polygate ${Albany.exe} input_pmoscap_polygate.xml)
add_test(${testRoot}_pndiode ${Albany.exe} input_pndiode.xml)
add_test(${testRoot}_mosdot_2D ${Albany.exe} input_mosdot_2D.xml)
add_test(${testRoot}_mosdot_2D_tri6 ${Albany.exe} input_mosdot_2D_tri6.xml)
add_test(${testRoot}_mosdot_2D_qcad ${Albany.exe} input_mosdot_2D_qcad.xml)
add_test(${testRoot}_mosdot_3D ${Albany.exe} input_mosdot_3D.xml)
add_test(${testRoot}_mosdot_3D_tet10 ${Albany.exe} input_mosdot_3D_tet10.xml)





