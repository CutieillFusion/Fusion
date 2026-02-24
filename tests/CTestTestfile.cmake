# CMake generated Testfile for 
# Source directory: /home/ad.msoe.edu/norquistd/SPRING26/Fusion/tests
# Build directory: /home/ad.msoe.edu/norquistd/SPRING26/Fusion/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(test_runner "/home/ad.msoe.edu/norquistd/SPRING26/Fusion/tests/test_runner")
set_tests_properties(test_runner PROPERTIES  REQUIRED_FILES "/home/ad.msoe.edu/norquistd/SPRING26/Fusion/tests/test_runner" _BACKTRACE_TRIPLES "/home/ad.msoe.edu/norquistd/SPRING26/Fusion/tests/CMakeLists.txt;3;add_test;/home/ad.msoe.edu/norquistd/SPRING26/Fusion/tests/CMakeLists.txt;0;")
add_test(fusion_tests "/home/ad.msoe.edu/norquistd/SPRING26/Fusion/tests/fusion_tests")
set_tests_properties(fusion_tests PROPERTIES  _BACKTRACE_TRIPLES "/home/ad.msoe.edu/norquistd/SPRING26/Fusion/tests/CMakeLists.txt;37;add_test;/home/ad.msoe.edu/norquistd/SPRING26/Fusion/tests/CMakeLists.txt;0;")
add_test(fusion_help "/home/ad.msoe.edu/norquistd/SPRING26/Fusion/compiler/fusion" "--help")
set_tests_properties(fusion_help PROPERTIES  REQUIRED_FILES "/home/ad.msoe.edu/norquistd/SPRING26/Fusion/compiler/fusion" _BACKTRACE_TRIPLES "/home/ad.msoe.edu/norquistd/SPRING26/Fusion/tests/CMakeLists.txt;39;add_test;/home/ad.msoe.edu/norquistd/SPRING26/Fusion/tests/CMakeLists.txt;0;")
add_test(fusion_run_print_1_2 "/home/ad.msoe.edu/norquistd/SPRING26/Fusion/compiler/fusion" "run" "/home/ad.msoe.edu/norquistd/SPRING26/Fusion/test.fusion")
set_tests_properties(fusion_run_print_1_2 PROPERTIES  ENVIRONMENT "LC_ALL=C" REQUIRED_FILES "/home/ad.msoe.edu/norquistd/SPRING26/Fusion/compiler/fusion" _BACKTRACE_TRIPLES "/home/ad.msoe.edu/norquistd/SPRING26/Fusion/tests/CMakeLists.txt;44;add_test;/home/ad.msoe.edu/norquistd/SPRING26/Fusion/tests/CMakeLists.txt;0;")
