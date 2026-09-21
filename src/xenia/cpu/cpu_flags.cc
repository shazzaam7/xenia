/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/cpu_flags.h"

DEFINE_string_choices(cpu, "any", "Does nothing. CPU backend [any, x64].",
                      "CPU", "CPU backend", XE_CVAR_CHOICE("Any", "any"),
                      XE_CVAR_CHOICE("x64", "x64"));

DEFINE_string(
    load_module_map, "",
    "Loads a .map for symbol names and to diff with the generated symbol "
    "database.",
    "CPU");
DEFINE_CVar_DisplayName(load_module_map, "Module map file");

DEFINE_bool(disassemble_functions, false,
            "Disassemble functions during generation.", "CPU");
DEFINE_CVar_DisplayName(disassemble_functions, "Disassemble functions");

DEFINE_bool(trace_functions, false, "Generate tracing for function statistics.",
            "CPU");
DEFINE_bool(trace_function_coverage, false,
            "Generate tracing for function instruction coverage statistics.",
            "CPU");
DEFINE_bool(trace_function_references, false,
            "Generate tracing for function address references.", "CPU");
DEFINE_bool(trace_function_data, false,
            "Generate tracing for function result data.", "CPU");

DEFINE_bool(validate_hir, false,
            "Perform validation checks on the HIR during compilation.", "CPU");

// https://github.com/bitsh1ft3r/Xenon/blob/091e8cd4dc4a7c697b4979eb200be7c9dee3590b/Xenon/Core/XCPU/PPU/PowerPC.h#L370
// The values are the decimal spellings of the PVRs the editor offers - the
// dropdown writes what it stores, and the config file holds decimals.
DEFINE_uint64_choices(
    pvr, 0x710700,
    "Known PVR's.\n"
    " 0x710200 = Used by Zephyr \n"
    " 0x710300 = Used by Zephyr\n"
    " 0x710500 = Used by Jasper\n"
    " 0x710700 = Default\n"
    " 0x710800 = Used by Corona V1 & V2\n"
    "Processor version and revision number.\nBits 0 to 15 are the version "
    "number.\nBits 16 to 31 are the revision number.\nNote: Some XEXs (such as "
    "mfgbootlauncher.xex) may check for a value that's less than 0x710700.",
    "CPU", "Processor version (PVR)",
    XE_CVAR_CHOICE("0x710200 - Zephyr", "7406080"),
    XE_CVAR_CHOICE("0x710300 - Zephyr", "7406336"),
    XE_CVAR_CHOICE("0x710500 - Jasper", "7406848"),
    XE_CVAR_CHOICE("0x710700 - Default", "7407360"),
    XE_CVAR_CHOICE("0x710800 - Corona V1/V2", "7407616"));

// Breakpoints:
DEFINE_uint64(break_on_instruction, 0,
              "int3 before the given guest address is executed.", "CPU");
DEFINE_CVar_DisplayName(break_on_instruction, "Break on instruction");
DEFINE_int32(break_condition_gpr, -1, "GPR compared to", "CPU");
DEFINE_CVar_DisplayName(break_condition_gpr, "Breakpoint GPR");
DEFINE_uint64(break_condition_value, 0, "value compared against", "CPU");
DEFINE_CVar_DisplayName(break_condition_value, "Breakpoint value");
DEFINE_string_choices(break_condition_op, "eq", "comparison operator", "CPU",
                      "Breakpoint comparison",
                      XE_CVAR_CHOICE("Equal (eq)", "eq"),
                      XE_CVAR_CHOICE("Not equal (ne)", "ne"),
                      XE_CVAR_CHOICE("Signed less than (slt)", "slt"),
                      XE_CVAR_CHOICE("Signed less or equal (sle)", "sle"),
                      XE_CVAR_CHOICE("Signed greater than (sgt)", "sgt"),
                      XE_CVAR_CHOICE("Signed greater or equal (sge)", "sge"),
                      XE_CVAR_CHOICE("Unsigned less than (ult)", "ult"),
                      XE_CVAR_CHOICE("Unsigned less or equal (ule)", "ule"),
                      XE_CVAR_CHOICE("Unsigned greater than (ugt)", "ugt"),
                      XE_CVAR_CHOICE("Unsigned greater or equal (uge)", "uge"));
DEFINE_bool(break_condition_truncate, true, "truncate value to 32-bits", "CPU");
DEFINE_CVar_DisplayName(break_condition_truncate, "Truncate breakpoint value");

DEFINE_bool(break_on_debugbreak, true, "int3 on JITed __debugbreak requests.",
            "CPU");
DEFINE_CVar_DisplayName(break_on_debugbreak, "Break on debugbreak");
