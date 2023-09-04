cmake_minimum_required(VERSION 3.22)

add_executable(acme)

target_sources(acme PRIVATE
	acme.c
	cliargs.c
	alu.c
	cpu.c
	dynabuf.c
	encoding.c
	flow.c
	global.c
	input.c
	macro.c
	mnemo.c
	output.c
	platform.c
	pseudoopcodes.c
	section.c
	symbol.c
	tree.c
	typesystem.c)
	
target_sources(acme PUBLIC
	acme.h
	alu.h
	cliargs.h
	config.h
	cpu.h
	dynabuf.h
	encoding.h
	flow.h
	global.h
	input.h
	macro.h
	mnemo.h
	output.h
	platform.h
	pseudoopcodes.h
	section.h
	symbol.h
	tree.h
	typesystem.h
	version.h)
	
list(APPEND CMAKE_REQUIRED_LIBRARIES m)