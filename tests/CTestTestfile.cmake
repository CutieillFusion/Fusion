# CMake generated Testfile for 
# Source directory: /home/ad.msoe.edu/norquistd/SPRING26/Fusion/tests
# Build directory: /home/ad.msoe.edu/norquistd/SPRING26/Fusion/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(all_tests "bash" "/home/ad.msoe.edu/norquistd/SPRING26/Fusion/tests/run_all_tests.sh" "/home/ad.msoe.edu/norquistd/SPRING26/Fusion" "/home/ad.msoe.edu/norquistd/SPRING26/Fusion")
set_tests_properties(all_tests PROPERTIES  $<TARGET_FILE:fusion_tests> "/home/ad.msoe.edu/norquistd/SPRING26/Fusion/compiler/fusion" REQUIRED_FILES "/home/ad.msoe.edu/norquistd/SPRING26/Fusion/tests/test_runner" WORKING_DIRECTORY "/home/ad.msoe.edu/norquistd/SPRING26/Fusion" _BACKTRACE_TRIPLES "/home/ad.msoe.edu/norquistd/SPRING26/Fusion/tests/CMakeLists.txt;19;add_test;/home/ad.msoe.edu/norquistd/SPRING26/Fusion/tests/CMakeLists.txt;0;")
