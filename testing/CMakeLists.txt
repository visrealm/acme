cmake_minimum_required(VERSION 3.22)

enable_testing()

set(TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/)
set(TEST_RUNNER ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}acme)

# Test macro expressions
add_test(macro_math1 ${TEST_RUNNER} ${TESTS_DIR}math1.a)
add_test(macro_numberflags ${TEST_RUNNER} ${TESTS_DIR}numberflags.a)

# Test input files which should generate an error
set(TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/errors/)
file(GLOB ERROR_TESTS ${TESTS_DIR}/*.a)
foreach (testsourcefile ${ERROR_TESTS})
    get_filename_component(testname ${testsourcefile} NAME_WLE)	
	add_test(${testname} ${TEST_RUNNER} ${TESTS_DIR}${testsourcefile})
	set_tests_properties(${testname} PROPERTIES WILL_FAIL TRUE)
endforeach (testsourcefile ${ERROR_TESTS})

# Comparison tests for each CPU type. Compare output
set(TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cpus/)
set(CPUS 6502 65c02 r65c02 w65c02 nmos6502 c64dtv2 65ce02 4502 m65 65816)
foreach (cpu ${CPUS})
	add_test(test-${cpu} ${TEST_RUNNER} -v0 -I ${TESTS_DIR} ${TESTS_DIR}test-${cpu}.a)
	add_test(cmp-test-${cpu} ${CMAKE_COMMAND} -E compare_files out-${cpu}.o ${TESTS_DIR}expected-${cpu}.o
	)
endforeach (cpu ${CPUS})
