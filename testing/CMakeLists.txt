cmake_minimum_required(VERSION 3.22)

enable_testing()

set(TEST_RUNNER ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}acme)

# Upstream self-tests and bugfix regression tests. These use !assert internally,
# so assembling without error is the pass condition.
foreach (suite auto bugfixes)
	set(TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${suite}/)
	file(GLOB SUITE_TESTS ${TESTS_DIR}*.a)
	foreach (testsourcefile ${SUITE_TESTS})
		get_filename_component(testname ${testsourcefile} NAME_WLE)
		# ticket25 does "!bin \"/dev/null\", 32000", which has no Windows equivalent
		if (WIN32 AND testname STREQUAL "ticket25")
			continue()
		endif()
		add_test(${suite}-${testname} ${TEST_RUNNER} -v0 -I ${TESTS_DIR} ${testsourcefile})
	endforeach (testsourcefile ${SUITE_TESTS})
endforeach (suite)

# Test input files which should generate an error
set(TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/errors/)
file(GLOB ERROR_TESTS ${TESTS_DIR}/*.a)
foreach (testsourcefile ${ERROR_TESTS})
    get_filename_component(testname ${testsourcefile} NAME_WLE)
	add_test(${testname} ${TEST_RUNNER} ${TESTS_DIR}${testsourcefile})
	set_tests_properties(${testname} PROPERTIES WILL_FAIL TRUE)
endforeach (testsourcefile ${ERROR_TESTS})

set(CPUS 6502 65c02 r65c02 w65c02 nmos6502 c64dtv2 65ce02 4502 m65 65816)

# Comparison tests for each CPU type. Compare output against the expected
# binaries shipped upstream (out-<cpu>.exp).
set(TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cpus/)
foreach (cpu ${CPUS})
	add_test(test-${cpu} ${TEST_RUNNER} -v0 -I ${TESTS_DIR} ${TESTS_DIR}test-${cpu}.a)
	add_test(cmp-test-${cpu} ${CMAKE_COMMAND} -E compare_files out-${cpu}.o ${TESTS_DIR}out-${cpu}.exp
	)
endforeach (cpu ${CPUS})

# Comparison tests for each CPU type Intel HEX output. Compare output.
# test-hex-<cpu>.a is generated from test-<cpu>.a by swapping the "!to" line,
# so the two stay in sync when upstream changes a CPU test.
foreach (cpu ${CPUS})
	add_test(test-hex-${cpu} ${TEST_RUNNER} -v0 -I ${TESTS_DIR} ${TESTS_DIR}test-hex-${cpu}.a)
	add_test(cmp-test-hex-${cpu} ${CMAKE_COMMAND} -E compare_files --ignore-eol out-${cpu}.hex ${TESTS_DIR}expected-${cpu}.hex
	)
endforeach (cpu ${CPUS})
