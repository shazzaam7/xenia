/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_CONSOLE_SETTINGS_TABLES_H_
#define XENIA_APP_CONSOLE_SETTINGS_TABLES_H_

#include <map>
#include <string>

namespace xe {
namespace app {

// Display-name tables shared by the ImGui and wxWidgets console settings
// dialogs.
inline const std::map<uint32_t, std::string> kLanguageMap = {
    {1, "English"},    {2, "Japanese"},
    {3, "German"},     {4, "French"},
    {5, "Spanish"},    {6, "Italian"},
    {7, "Korean"},     {8, "Traditional Chinese"},
    {9, "Portuguese"}, {11, "Polish"},
    {12, "Russian"},   {13, "Swedish"},
    {14, "Turkish"},   {15, "Norwegian"},
    {16, "Dutch"},     {17, "Simplified Chinese"}};

inline const std::map<uint8_t, std::string> kCountryMap = {
    {1, "AE"},   {2, "AL"},   {3, "AM"},   {4, "AR"},   {5, "AT"},
    {6, "AU"},   {7, "AZ"},   {8, "BE"},   {9, "BG"},   {10, "BH"},
    {11, "BN"},  {12, "BO"},  {13, "BR"},  {14, "BY"},  {15, "BZ"},
    {16, "CA"},  {18, "CH"},  {19, "CL"},  {20, "CN"},  {21, "CO"},
    {22, "CR"},  {23, "CZ"},  {24, "DE"},  {25, "DK"},  {26, "DO"},
    {27, "DZ"},  {28, "EC"},  {29, "EE"},  {30, "EG"},  {31, "ES"},
    {32, "FI"},  {33, "FO"},  {34, "FR"},  {35, "GB"},  {36, "GE"},
    {37, "GR"},  {38, "GT"},  {39, "HK"},  {40, "HN"},  {41, "HR"},
    {42, "HU"},  {43, "ID"},  {44, "IE"},  {45, "IL"},  {46, "IN"},
    {47, "IQ"},  {48, "IR"},  {49, "IS"},  {50, "IT"},  {51, "JM"},
    {52, "JO"},  {53, "JP"},  {54, "KE"},  {55, "KG"},  {56, "KR"},
    {57, "KW"},  {58, "KZ"},  {59, "LB"},  {60, "LI"},  {61, "LT"},
    {62, "LU"},  {63, "LV"},  {64, "LY"},  {65, "MA"},  {66, "MC"},
    {67, "MK"},  {68, "MN"},  {69, "MO"},  {70, "MV"},  {71, "MX"},
    {72, "MY"},  {73, "NI"},  {74, "NL"},  {75, "NO"},  {76, "NZ"},
    {77, "OM"},  {78, "PA"},  {79, "PE"},  {80, "PH"},  {81, "PK"},
    {82, "PL"},  {83, "PR"},  {84, "PT"},  {85, "PY"},  {86, "QA"},
    {87, "RO"},  {88, "RU"},  {89, "SA"},  {90, "SE"},  {91, "SG"},
    {92, "SI"},  {93, "SK"},  {95, "SV"},  {96, "SY"},  {97, "TH"},
    {98, "TN"},  {99, "TR"},  {100, "TT"}, {101, "TW"}, {102, "UA"},
    {103, "US"}, {104, "UY"}, {105, "UZ"}, {106, "VE"}, {107, "VN"},
    {108, "YE"}, {109, "ZA"},
};

inline const std::map<uint32_t, std::string> kAVRegion = {
    {0x00400100, "NTSC"},
    {0x00400200, "NTSC-J"},
    {0x00400400, "PAL"},
    {0x00800300, "PAL 50Hz"}};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_CONSOLE_SETTINGS_TABLES_H_
