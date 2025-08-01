//===- EnumsGen.cpp - MLIR enum utility generator -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// EnumsGen generates common utility functions for enums.
//
//===----------------------------------------------------------------------===//

#include "FormatGen.h"
#include "mlir/TableGen/Attribute.h"
#include "mlir/TableGen/EnumInfo.h"
#include "mlir/TableGen/Format.h"
#include "mlir/TableGen/GenInfo.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"

using llvm::formatv;
using llvm::isDigit;
using llvm::PrintFatalError;
using llvm::Record;
using llvm::RecordKeeper;
using namespace mlir;
using mlir::tblgen::Attribute;
using mlir::tblgen::EnumCase;
using mlir::tblgen::EnumInfo;
using mlir::tblgen::FmtContext;
using mlir::tblgen::tgfmt;

static std::string makeIdentifier(StringRef str) {
  if (!str.empty() && isDigit(static_cast<unsigned char>(str.front()))) {
    std::string newStr = std::string("_") + str.str();
    return newStr;
  }
  return str.str();
}

static void emitEnumClass(const Record &enumDef, StringRef enumName,
                          StringRef underlyingType, StringRef description,
                          const std::vector<EnumCase> &enumerants,
                          raw_ostream &os) {
  os << "// " << description << "\n";
  os << "enum class " << enumName;

  if (!underlyingType.empty())
    os << " : " << underlyingType;
  os << " {\n";

  for (const auto &enumerant : enumerants) {
    auto symbol = makeIdentifier(enumerant.getSymbol());
    auto value = enumerant.getValue();
    if (value >= 0) {
      os << formatv("  {0} = {1},\n", symbol, value);
    } else {
      os << formatv("  {0},\n", symbol);
    }
  }
  os << "};\n\n";
}

static void emitParserPrinter(const EnumInfo &enumInfo, StringRef qualName,
                              StringRef cppNamespace, raw_ostream &os) {
  std::optional<Attribute> enumAttrInfo = enumInfo.asEnumAttr();
  if (enumInfo.getUnderlyingType().empty() ||
      (enumAttrInfo && enumAttrInfo->getConstBuilderTemplate().empty()))
    return;
  auto cases = enumInfo.getAllCases();

  // Check which cases shouldn't be printed using a keyword.
  llvm::BitVector nonKeywordCases(cases.size());
  std::string casesList;
  llvm::raw_string_ostream caseListOs(casesList);
  caseListOs << "[";
  llvm::interleaveComma(llvm::enumerate(cases), caseListOs,
                        [&](auto enumerant) {
                          StringRef name = enumerant.value().getStr();
                          if (!mlir::tblgen::canFormatStringAsKeyword(name)) {
                            nonKeywordCases.set(enumerant.index());
                            caseListOs << "\\\"" << name << "\\\"";
                          }
                          caseListOs << name;
                        });
  caseListOs << "]";

  // Generate the parser and the start of the printer for the enum, excluding
  // non-quoted bit enums.
  const char *parsedAndPrinterStart = R"(
namespace mlir {
template <typename T, typename>
struct FieldParser;

template<>
struct FieldParser<{0}, {0}> {{
  template <typename ParserT>
  static FailureOr<{0}> parse(ParserT &parser) {{
    // Parse the keyword/string containing the enum.
    std::string enumKeyword;
    auto loc = parser.getCurrentLocation();
    if (failed(parser.parseOptionalKeywordOrString(&enumKeyword)))
      return parser.emitError(loc, "expected keyword for {2}");

    // Symbolize the keyword.
    if (::std::optional<{0}> attr = {1}::symbolizeEnum<{0}>(enumKeyword))
      return *attr;
    return parser.emitError(loc, "expected one of {3} for {2}, got: ") << enumKeyword;
  }
};

/// Support for std::optional, useful in attribute/type definition where the enum is
/// used as:
///
///    let parameters = (ins OptionalParameter<"std::optional<TheEnumName>">:$value);
template<>
struct FieldParser<std::optional<{0}>, std::optional<{0}>> {{
  template <typename ParserT>
  static FailureOr<std::optional<{0}>> parse(ParserT &parser) {{
    // Parse the keyword/string containing the enum.
    std::string enumKeyword;
    auto loc = parser.getCurrentLocation();
    if (failed(parser.parseOptionalKeywordOrString(&enumKeyword)))
      return std::optional<{0}>{{};

    // Symbolize the keyword.
    if (::std::optional<{0}> attr = {1}::symbolizeEnum<{0}>(enumKeyword))
      return attr;
    return parser.emitError(loc, "expected one of {3} for {2}, got: ") << enumKeyword;
  }
};
} // namespace mlir

namespace llvm {
inline ::llvm::raw_ostream &operator<<(::llvm::raw_ostream &p, {0} value) {{
  auto valueStr = stringifyEnum(value);
)";

  const char *parsedAndPrinterStartUnquotedBitEnum = R"(
  namespace mlir {
  template <typename T, typename>
  struct FieldParser;

  template<>
  struct FieldParser<{0}, {0}> {{
    template <typename ParserT>
    static FailureOr<{0}> parse(ParserT &parser) {{
      {0} flags = {{};
      do {{
        // Parse the keyword containing a part of the enum.
        ::llvm::StringRef enumKeyword;
        auto loc = parser.getCurrentLocation();
        if (failed(parser.parseOptionalKeyword(&enumKeyword))) {{
          return parser.emitError(loc, "expected keyword for {2}");
        }

        // Symbolize the keyword.
        if (::std::optional<{0}> flag = {1}::symbolizeEnum<{0}>(enumKeyword)) {{
          flags = flags | *flag;
        } else {{
          return parser.emitError(loc, "expected one of {3} for {2}, got: ") << enumKeyword;
        }
      } while (::mlir::succeeded(parser.{5}()));
      return flags;
    }
  };

  /// Support for std::optional, useful in attribute/type definition where the enum is
  /// used as:
  ///
  ///    let parameters = (ins OptionalParameter<"std::optional<TheEnumName>">:$value);
  template<>
  struct FieldParser<std::optional<{0}>, std::optional<{0}>> {{
    template <typename ParserT>
    static FailureOr<std::optional<{0}>> parse(ParserT &parser) {{
      {0} flags = {{};
      bool firstIter = true;
      do {{
        // Parse the keyword containing a part of the enum.
        ::llvm::StringRef enumKeyword;
        auto loc = parser.getCurrentLocation();
        if (failed(parser.parseOptionalKeyword(&enumKeyword))) {{
          if (firstIter)
            return std::optional<{0}>{{};
          return parser.emitError(loc, "expected keyword for {2} after '{4}'");
        }
        firstIter = false;

        // Symbolize the keyword.
        if (::std::optional<{0}> flag = {1}::symbolizeEnum<{0}>(enumKeyword)) {{
          flags = flags | *flag;
        } else {{
          return parser.emitError(loc, "expected one of {3} for {2}, got: ") << enumKeyword;
        }
      } while(::mlir::succeeded(parser.{5}()));
      return std::optional<{0}>{{flags};
    }
  };
  } // namespace mlir

  namespace llvm {
  inline ::llvm::raw_ostream &operator<<(::llvm::raw_ostream &p, {0} value) {{
    auto valueStr = stringifyEnum(value);
  )";

  bool isNewStyleBitEnum =
      enumInfo.isBitEnum() && !enumInfo.printBitEnumQuoted();

  if (isNewStyleBitEnum) {
    if (nonKeywordCases.any())
      return PrintFatalError(
          "bit enum " + qualName +
          " cannot be printed unquoted with cases that cannot be keywords");
    StringRef separator = enumInfo.getDef().getValueAsString("separator");
    StringRef parseSeparatorFn =
        llvm::StringSwitch<StringRef>(separator.trim())
            .Case("|", "parseOptionalVerticalBar")
            .Case(",", "parseOptionalComma")
            .Default("error, enum seperator must be '|' or ','");
    os << formatv(parsedAndPrinterStartUnquotedBitEnum, qualName, cppNamespace,
                  enumInfo.getSummary(), casesList, separator,
                  parseSeparatorFn);
  } else {
    os << formatv(parsedAndPrinterStart, qualName, cppNamespace,
                  enumInfo.getSummary(), casesList);
  }

  // If all cases require a string, always wrap.
  if (nonKeywordCases.all()) {
    os << "  return p << '\"' << valueStr << '\"';\n"
          "}\n"
          "} // namespace llvm\n";
    return;
  }

  // If there are any cases that can't be used with a keyword, switch on the
  // case value to determine when to print in the string form.
  if (nonKeywordCases.any()) {
    os << "  switch (value) {\n";
    for (auto it : llvm::enumerate(cases)) {
      if (nonKeywordCases.test(it.index()))
        continue;
      StringRef symbol = it.value().getSymbol();
      os << llvm::formatv("  case {0}::{1}:\n", qualName,
                          makeIdentifier(symbol));
    }
    os << "    break;\n"
          "  default:\n"
          "    return p << '\"' << valueStr << '\"';\n"
          "  }\n";

    // If this is a bit enum, conservatively print the string form if the value
    // is not a power of two (i.e. not a single bit case) and not a known case.
    // Only do this if we're using the old-style parser that parses the enum as
    // one keyword, as opposed to the new form, where we can print the value
    // as-is.
  } else if (enumInfo.isBitEnum() && !isNewStyleBitEnum) {
    // Process the known multi-bit cases that use valid keywords.
    SmallVector<EnumCase *> validMultiBitCases;
    for (auto [index, caseVal] : llvm::enumerate(cases)) {
      uint64_t value = caseVal.getValue();
      if (value && !llvm::has_single_bit(value) && !nonKeywordCases.test(index))
        validMultiBitCases.push_back(&caseVal);
    }
    if (!validMultiBitCases.empty()) {
      os << "  switch (value) {\n";
      for (EnumCase *caseVal : validMultiBitCases) {
        StringRef symbol = caseVal->getSymbol();
        os << llvm::formatv("  case {0}::{1}:\n", qualName,
                            llvm::isDigit(symbol.front()) ? ("_" + symbol)
                                                          : symbol);
      }
      os << "    return p << valueStr;\n"
            "  default:\n"
            "    break;\n"
            "  }\n";
    }

    // All other multi-bit cases should be printed as strings.
    os << formatv("  auto underlyingValue = "
                  "static_cast<std::make_unsigned_t<{0}>>(value);\n",
                  qualName);
    os << "  if (underlyingValue && !llvm::has_single_bit(underlyingValue))\n"
          "    return p << '\"' << valueStr << '\"';\n";
  }
  os << "  return p << valueStr;\n"
        "}\n"
        "} // namespace llvm\n";
}

static void emitDenseMapInfo(StringRef qualName, std::string underlyingType,
                             StringRef cppNamespace, raw_ostream &os) {
  if (underlyingType.empty())
    underlyingType =
        std::string(formatv("std::underlying_type_t<{0}>", qualName));

  const char *const mapInfo = R"(
namespace llvm {
template<> struct DenseMapInfo<{0}> {{
  using StorageInfo = ::llvm::DenseMapInfo<{1}>;

  static inline {0} getEmptyKey() {{
    return static_cast<{0}>(StorageInfo::getEmptyKey());
  }

  static inline {0} getTombstoneKey() {{
    return static_cast<{0}>(StorageInfo::getTombstoneKey());
  }

  static unsigned getHashValue(const {0} &val) {{
    return StorageInfo::getHashValue(static_cast<{1}>(val));
  }

  static bool isEqual(const {0} &lhs, const {0} &rhs) {{
    return lhs == rhs;
  }
};
})";
  os << formatv(mapInfo, qualName, underlyingType);
  os << "\n\n";
}

static void emitMaxValueFn(const Record &enumDef, raw_ostream &os) {
  EnumInfo enumInfo(enumDef);
  StringRef maxEnumValFnName = enumInfo.getMaxEnumValFnName();
  auto enumerants = enumInfo.getAllCases();

  unsigned maxEnumVal = 0;
  for (const auto &enumerant : enumerants) {
    int64_t value = enumerant.getValue();
    // Avoid generating the max value function if there is an enumerant without
    // explicit value.
    if (value < 0)
      return;

    maxEnumVal = std::max(maxEnumVal, static_cast<unsigned>(value));
  }

  // Emit the function to return the max enum value
  os << formatv("inline constexpr unsigned {0}() {{\n", maxEnumValFnName);
  os << formatv("  return {0};\n", maxEnumVal);
  os << "}\n\n";
}

// Returns the EnumCase whose value is zero if exists; returns std::nullopt
// otherwise.
static std::optional<EnumCase>
getAllBitsUnsetCase(llvm::ArrayRef<EnumCase> cases) {
  for (auto attrCase : cases) {
    if (attrCase.getValue() == 0)
      return attrCase;
  }
  return std::nullopt;
}

// Emits the following inline function for bit enums:
//
// inline constexpr <enum-type> operator|(<enum-type> a, <enum-type> b);
// inline constexpr <enum-type> operator&(<enum-type> a, <enum-type> b);
// inline constexpr <enum-type> operator^(<enum-type> a, <enum-type> b);
// inline constexpr <enum-type> operator~(<enum-type> bits);
// inline constexpr bool bitEnumContainsAll(<enum-type> bits, <enum-type> bit);
// inline constexpr bool bitEnumContainsAny(<enum-type> bits, <enum-type> bit);
// inline constexpr <enum-type> bitEnumClear(<enum-type> bits, <enum-type> bit);
// inline constexpr <enum-type> bitEnumSet(<enum-type> bits, <enum-type> bit,
// bool value=true);
static void emitOperators(const Record &enumDef, raw_ostream &os) {
  EnumInfo enumInfo(enumDef);
  StringRef enumName = enumInfo.getEnumClassName();
  std::string underlyingType = std::string(enumInfo.getUnderlyingType());
  int64_t validBits = enumDef.getValueAsInt("validBits");
  const char *const operators = R"(
inline constexpr {0} operator|({0} a, {0} b) {{
  return static_cast<{0}>(static_cast<{1}>(a) | static_cast<{1}>(b));
}
inline constexpr {0} operator&({0} a, {0} b) {{
  return static_cast<{0}>(static_cast<{1}>(a) & static_cast<{1}>(b));
}
inline constexpr {0} operator^({0} a, {0} b) {{
  return static_cast<{0}>(static_cast<{1}>(a) ^ static_cast<{1}>(b));
}
inline constexpr {0} operator~({0} bits) {{
  // Ensure only bits that can be present in the enum are set
  return static_cast<{0}>(~static_cast<{1}>(bits) & static_cast<{1}>({2}u));
}
inline constexpr bool bitEnumContainsAll({0} bits, {0} bit) {{
  return (bits & bit) == bit;
}
inline constexpr bool bitEnumContainsAny({0} bits, {0} bit) {{
  return (static_cast<{1}>(bits) & static_cast<{1}>(bit)) != 0;
}
inline constexpr {0} bitEnumClear({0} bits, {0} bit) {{
  return bits & ~bit;
}
inline constexpr {0} bitEnumSet({0} bits, {0} bit, /*optional*/bool value=true) {{
  return value ? (bits | bit) : bitEnumClear(bits, bit);
}
  )";
  os << formatv(operators, enumName, underlyingType, validBits);
}

static void emitSymToStrFnForIntEnum(const Record &enumDef, raw_ostream &os) {
  EnumInfo enumInfo(enumDef);
  StringRef enumName = enumInfo.getEnumClassName();
  StringRef symToStrFnName = enumInfo.getSymbolToStringFnName();
  StringRef symToStrFnRetType = enumInfo.getSymbolToStringFnRetType();
  auto enumerants = enumInfo.getAllCases();

  os << formatv("{2} {1}({0} val) {{\n", enumName, symToStrFnName,
                symToStrFnRetType);
  os << "  switch (val) {\n";
  for (const auto &enumerant : enumerants) {
    auto symbol = enumerant.getSymbol();
    auto str = enumerant.getStr();
    os << formatv("    case {0}::{1}: return \"{2}\";\n", enumName,
                  makeIdentifier(symbol), str);
  }
  os << "  }\n";
  os << "  return \"\";\n";
  os << "}\n\n";
}

static void emitSymToStrFnForBitEnum(const Record &enumDef, raw_ostream &os) {
  EnumInfo enumInfo(enumDef);
  StringRef enumName = enumInfo.getEnumClassName();
  StringRef symToStrFnName = enumInfo.getSymbolToStringFnName();
  StringRef symToStrFnRetType = enumInfo.getSymbolToStringFnRetType();
  StringRef separator = enumDef.getValueAsString("separator");
  auto enumerants = enumInfo.getAllCases();
  auto allBitsUnsetCase = getAllBitsUnsetCase(enumerants);

  os << formatv("{2} {1}({0} symbol) {{\n", enumName, symToStrFnName,
                symToStrFnRetType);

  os << formatv("  auto val = static_cast<{0}>(symbol);\n",
                enumInfo.getUnderlyingType());
  // If we have unknown bit set, return an empty string to signal errors.
  int64_t validBits = enumDef.getValueAsInt("validBits");
  os << formatv("  assert({0}u == ({0}u | val) && \"invalid bits set in bit "
                "enum\");\n",
                validBits);
  if (allBitsUnsetCase) {
    os << "  // Special case for all bits unset.\n";
    os << formatv("  if (val == 0) return \"{0}\";\n\n",
                  allBitsUnsetCase->getStr());
  }
  os << "  ::llvm::SmallVector<::llvm::StringRef, 2> strs;\n";

  // Add case string if the value has all case bits, and remove them to avoid
  // printing again. Used only for groups, when printBitEnumPrimaryGroups is 1.
  const char *const formatCompareRemove = R"(
  if ({0}u == ({0}u & val)) {{
    strs.push_back("{1}");
    val &= ~static_cast<{2}>({0});
  }
)";
  // Add case string if the value has all case bits. Used for individual bit
  // cases, and for groups when printBitEnumPrimaryGroups is 0.
  const char *const formatCompare = R"(
  if ({0}u == ({0}u & val))
    strs.push_back("{1}");
)";
  // Optionally elide bits that are members of groups that will also be printed
  // for more concise output.
  if (enumInfo.printBitEnumPrimaryGroups()) {
    os << "  // Print bit enum groups before individual bits\n";
    // Emit comparisons for group bit cases in reverse tablegen declaration
    // order, removing bits for groups with all bits present.
    for (const auto &enumerant : llvm::reverse(enumerants)) {
      if ((enumerant.getValue() != 0) &&
          (enumerant.getDef().isSubClassOf("BitEnumCaseGroup") ||
           enumerant.getDef().isSubClassOf("BitEnumAttrCaseGroup"))) {
        os << formatv(formatCompareRemove, enumerant.getValue(),
                      enumerant.getStr(), enumInfo.getUnderlyingType());
      }
    }
    // Emit comparisons for individual bit cases in tablegen declaration order.
    for (const auto &enumerant : enumerants) {
      if ((enumerant.getValue() != 0) &&
          (enumerant.getDef().isSubClassOf("BitEnumCaseBit") ||
           enumerant.getDef().isSubClassOf("BitEnumAttrCaseBit")))
        os << formatv(formatCompare, enumerant.getValue(), enumerant.getStr());
    }
  } else {
    // Emit comparisons for ALL nonzero cases (individual bits and groups) in
    // tablegen declaration order.
    for (const auto &enumerant : enumerants) {
      if (enumerant.getValue() != 0)
        os << formatv(formatCompare, enumerant.getValue(), enumerant.getStr());
    }
  }
  os << formatv("  return ::llvm::join(strs, \"{0}\");\n", separator);

  os << "}\n\n";
}

static void emitStrToSymFnForIntEnum(const Record &enumDef, raw_ostream &os) {
  EnumInfo enumInfo(enumDef);
  StringRef enumName = enumInfo.getEnumClassName();
  StringRef strToSymFnName = enumInfo.getStringToSymbolFnName();
  auto enumerants = enumInfo.getAllCases();

  os << formatv("::std::optional<{0}> {1}(::llvm::StringRef str) {{\n",
                enumName, strToSymFnName);
  os << formatv("  return ::llvm::StringSwitch<::std::optional<{0}>>(str)\n",
                enumName);
  for (const auto &enumerant : enumerants) {
    auto symbol = enumerant.getSymbol();
    auto str = enumerant.getStr();
    os << formatv("      .Case(\"{1}\", {0}::{2})\n", enumName, str,
                  makeIdentifier(symbol));
  }
  os << "      .Default(::std::nullopt);\n";
  os << "}\n";
}

static void emitStrToSymFnForBitEnum(const Record &enumDef, raw_ostream &os) {
  EnumInfo enumInfo(enumDef);
  StringRef enumName = enumInfo.getEnumClassName();
  std::string underlyingType = std::string(enumInfo.getUnderlyingType());
  StringRef strToSymFnName = enumInfo.getStringToSymbolFnName();
  StringRef separator = enumDef.getValueAsString("separator");
  StringRef separatorTrimmed = separator.trim();
  auto enumerants = enumInfo.getAllCases();
  auto allBitsUnsetCase = getAllBitsUnsetCase(enumerants);

  os << formatv("::std::optional<{0}> {1}(::llvm::StringRef str) {{\n",
                enumName, strToSymFnName);

  if (allBitsUnsetCase) {
    os << "  // Special case for all bits unset.\n";
    StringRef caseSymbol = allBitsUnsetCase->getSymbol();
    os << formatv("  if (str == \"{1}\") return {0}::{2};\n\n", enumName,
                  allBitsUnsetCase->getStr(), makeIdentifier(caseSymbol));
  }

  // Split the string to get symbols for all the bits.
  os << "  ::llvm::SmallVector<::llvm::StringRef, 2> symbols;\n";
  // Remove whitespace from the separator string when parsing.
  os << formatv("  str.split(symbols, \"{0}\");\n\n", separatorTrimmed);

  os << formatv("  {0} val = 0;\n", underlyingType);
  os << "  for (auto symbol : symbols) {\n";

  // Convert each symbol to the bit ordinal and set the corresponding bit.
  os << formatv("    auto bit = "
                "llvm::StringSwitch<::std::optional<{0}>>(symbol.trim())\n",
                underlyingType);
  for (const auto &enumerant : enumerants) {
    // Skip the special enumerant for None.
    if (auto val = enumerant.getValue())
      os.indent(6) << formatv(".Case(\"{0}\", {1})\n", enumerant.getStr(), val);
  }
  os.indent(6) << ".Default(::std::nullopt);\n";

  os << "    if (bit) { val |= *bit; } else { return ::std::nullopt; }\n";
  os << "  }\n";

  os << formatv("  return static_cast<{0}>(val);\n", enumName);
  os << "}\n\n";
}

static void emitUnderlyingToSymFnForIntEnum(const Record &enumDef,
                                            raw_ostream &os) {
  EnumInfo enumInfo(enumDef);
  StringRef enumName = enumInfo.getEnumClassName();
  std::string underlyingType = std::string(enumInfo.getUnderlyingType());
  StringRef underlyingToSymFnName = enumInfo.getUnderlyingToSymbolFnName();
  auto enumerants = enumInfo.getAllCases();

  // Avoid generating the underlying value to symbol conversion function if
  // there is an enumerant without explicit value.
  if (llvm::any_of(enumerants,
                   [](EnumCase enumerant) { return enumerant.getValue() < 0; }))
    return;

  os << formatv("::std::optional<{0}> {1}({2} value) {{\n", enumName,
                underlyingToSymFnName,
                underlyingType.empty() ? std::string("unsigned")
                                       : underlyingType)
     << "  switch (value) {\n";
  for (const auto &enumerant : enumerants) {
    auto symbol = enumerant.getSymbol();
    auto value = enumerant.getValue();
    os << formatv("  case {0}: return {1}::{2};\n", value, enumName,
                  makeIdentifier(symbol));
  }
  os << "  default: return ::std::nullopt;\n"
     << "  }\n"
     << "}\n\n";
}

static void emitSpecializedAttrDef(const Record &enumDef, raw_ostream &os) {
  EnumInfo enumInfo(enumDef);
  StringRef enumName = enumInfo.getEnumClassName();
  StringRef attrClassName = enumInfo.getSpecializedAttrClassName();
  const Record *baseAttrDef = enumInfo.getBaseAttrClass();
  Attribute baseAttr(baseAttrDef);

  // Emit classof method

  os << formatv("bool {0}::classof(::mlir::Attribute attr) {{\n",
                attrClassName);

  mlir::tblgen::Pred baseAttrPred = baseAttr.getPredicate();
  if (baseAttrPred.isNull())
    PrintFatalError("ERROR: baseAttrClass for EnumAttr has no Predicate\n");

  std::string condition = baseAttrPred.getCondition();
  FmtContext verifyCtx;
  verifyCtx.withSelf("attr");
  os << tgfmt("  return $0;\n", /*ctx=*/nullptr, tgfmt(condition, &verifyCtx));

  os << "}\n";

  // Emit get method

  os << formatv("{0} {0}::get(::mlir::MLIRContext *context, {1} val) {{\n",
                attrClassName, enumName);

  StringRef underlyingType = enumInfo.getUnderlyingType();

  // Assuming that it is IntegerAttr constraint
  int64_t bitwidth = 64;
  if (baseAttrDef->getValue("valueType")) {
    auto *valueTypeDef = baseAttrDef->getValueAsDef("valueType");
    if (valueTypeDef->getValue("bitwidth"))
      bitwidth = valueTypeDef->getValueAsInt("bitwidth");
  }

  os << formatv("  ::mlir::IntegerType intType = "
                "::mlir::IntegerType::get(context, {0});\n",
                bitwidth);
  os << formatv("  ::mlir::IntegerAttr baseAttr = "
                "::mlir::IntegerAttr::get(intType, static_cast<{0}>(val));\n",
                underlyingType);
  os << formatv("  return ::llvm::cast<{0}>(baseAttr);\n", attrClassName);

  os << "}\n";

  // Emit getValue method

  os << formatv("{0} {1}::getValue() const {{\n", enumName, attrClassName);

  os << formatv(
      "  return "
      "static_cast<{0}>(::mlir::IntegerAttr::getValue().getZExtValue());\n",
      enumName);

  os << "}\n";
}

static void emitUnderlyingToSymFnForBitEnum(const Record &enumDef,
                                            raw_ostream &os) {
  EnumInfo enumInfo(enumDef);
  StringRef enumName = enumInfo.getEnumClassName();
  std::string underlyingType = std::string(enumInfo.getUnderlyingType());
  StringRef underlyingToSymFnName = enumInfo.getUnderlyingToSymbolFnName();
  auto enumerants = enumInfo.getAllCases();
  auto allBitsUnsetCase = getAllBitsUnsetCase(enumerants);

  os << formatv("::std::optional<{0}> {1}({2} value) {{\n", enumName,
                underlyingToSymFnName, underlyingType);
  if (allBitsUnsetCase) {
    os << "  // Special case for all bits unset.\n";
    os << formatv("  if (value == 0) return {0}::{1};\n\n", enumName,
                  makeIdentifier(allBitsUnsetCase->getSymbol()));
  }
  int64_t validBits = enumDef.getValueAsInt("validBits");
  os << formatv("  if (value & ~static_cast<{0}>({1}u)) return std::nullopt;\n",
                underlyingType, validBits);
  os << formatv("  return static_cast<{0}>(value);\n", enumName);
  os << "}\n";
}

static void emitEnumDecl(const Record &enumDef, raw_ostream &os) {
  EnumInfo enumInfo(enumDef);
  StringRef enumName = enumInfo.getEnumClassName();
  StringRef cppNamespace = enumInfo.getCppNamespace();
  std::string underlyingType = std::string(enumInfo.getUnderlyingType());
  StringRef description = enumInfo.getSummary();
  StringRef strToSymFnName = enumInfo.getStringToSymbolFnName();
  StringRef symToStrFnName = enumInfo.getSymbolToStringFnName();
  StringRef symToStrFnRetType = enumInfo.getSymbolToStringFnRetType();
  StringRef underlyingToSymFnName = enumInfo.getUnderlyingToSymbolFnName();
  auto enumerants = enumInfo.getAllCases();

  SmallVector<StringRef, 2> namespaces;
  llvm::SplitString(cppNamespace, namespaces, "::");

  for (auto ns : namespaces)
    os << "namespace " << ns << " {\n";

  // Emit the enum class definition
  emitEnumClass(enumDef, enumName, underlyingType, description, enumerants, os);

  // Emit conversion function declarations
  if (llvm::all_of(enumerants, [](EnumCase enumerant) {
        return enumerant.getValue() >= 0;
      })) {
    os << formatv(
        "::std::optional<{0}> {1}({2});\n", enumName, underlyingToSymFnName,
        underlyingType.empty() ? std::string("unsigned") : underlyingType);
  }
  os << formatv("{2} {1}({0});\n", enumName, symToStrFnName, symToStrFnRetType);
  os << formatv("::std::optional<{0}> {1}(::llvm::StringRef);\n", enumName,
                strToSymFnName);

  if (enumInfo.isBitEnum()) {
    emitOperators(enumDef, os);
  } else {
    emitMaxValueFn(enumDef, os);
  }

  // Generate a generic `stringifyEnum` function that forwards to the method
  // specified by the user.
  const char *const stringifyEnumStr = R"(
inline {0} stringifyEnum({1} enumValue) {{
  return {2}(enumValue);
}
)";
  os << formatv(stringifyEnumStr, symToStrFnRetType, enumName, symToStrFnName);

  // Generate a generic `symbolizeEnum` function that forwards to the method
  // specified by the user.
  const char *const symbolizeEnumStr = R"(
template <typename EnumType>
::std::optional<EnumType> symbolizeEnum(::llvm::StringRef);

template <>
inline ::std::optional<{0}> symbolizeEnum<{0}>(::llvm::StringRef str) {
  return {1}(str);
}
)";
  os << formatv(symbolizeEnumStr, enumName, strToSymFnName);

  const char *const attrClassDecl = R"(
class {1} : public ::mlir::{2} {
public:
  using ValueType = {0};
  using ::mlir::{2}::{2};
  static bool classof(::mlir::Attribute attr);
  static {1} get(::mlir::MLIRContext *context, {0} val);
  {0} getValue() const;
};
)";
  if (enumInfo.genSpecializedAttr()) {
    StringRef attrClassName = enumInfo.getSpecializedAttrClassName();
    StringRef baseAttrClassName = "IntegerAttr";
    os << formatv(attrClassDecl, enumName, attrClassName, baseAttrClassName);
  }

  for (auto ns : llvm::reverse(namespaces))
    os << "} // namespace " << ns << "\n";

  // Generate a generic parser and printer for the enum.
  std::string qualName =
      std::string(formatv("{0}::{1}", cppNamespace, enumName));
  emitParserPrinter(enumInfo, qualName, cppNamespace, os);

  // Emit DenseMapInfo for this enum class
  emitDenseMapInfo(qualName, underlyingType, cppNamespace, os);
}

static bool emitEnumDecls(const RecordKeeper &records, raw_ostream &os) {
  llvm::emitSourceFileHeader("Enum Utility Declarations", os, records);

  for (const Record *def :
       records.getAllDerivedDefinitionsIfDefined("EnumInfo"))
    emitEnumDecl(*def, os);

  return false;
}

static void emitEnumDef(const Record &enumDef, raw_ostream &os) {
  EnumInfo enumInfo(enumDef);
  StringRef cppNamespace = enumInfo.getCppNamespace();

  SmallVector<StringRef, 2> namespaces;
  llvm::SplitString(cppNamespace, namespaces, "::");

  for (auto ns : namespaces)
    os << "namespace " << ns << " {\n";

  if (enumInfo.isBitEnum()) {
    emitSymToStrFnForBitEnum(enumDef, os);
    emitStrToSymFnForBitEnum(enumDef, os);
    emitUnderlyingToSymFnForBitEnum(enumDef, os);
  } else {
    emitSymToStrFnForIntEnum(enumDef, os);
    emitStrToSymFnForIntEnum(enumDef, os);
    emitUnderlyingToSymFnForIntEnum(enumDef, os);
  }

  if (enumInfo.genSpecializedAttr())
    emitSpecializedAttrDef(enumDef, os);

  for (auto ns : llvm::reverse(namespaces))
    os << "} // namespace " << ns << "\n";
  os << "\n";
}

static bool emitEnumDefs(const RecordKeeper &records, raw_ostream &os) {
  llvm::emitSourceFileHeader("Enum Utility Definitions", os, records);

  for (const Record *def :
       records.getAllDerivedDefinitionsIfDefined("EnumInfo"))
    emitEnumDef(*def, os);

  return false;
}

// Registers the enum utility generator to mlir-tblgen.
static mlir::GenRegistration
    genEnumDecls("gen-enum-decls", "Generate enum utility declarations",
                 [](const RecordKeeper &records, raw_ostream &os) {
                   return emitEnumDecls(records, os);
                 });

// Registers the enum utility generator to mlir-tblgen.
static mlir::GenRegistration
    genEnumDefs("gen-enum-defs", "Generate enum utility definitions",
                [](const RecordKeeper &records, raw_ostream &os) {
                  return emitEnumDefs(records, os);
                });
