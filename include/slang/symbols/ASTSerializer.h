//------------------------------------------------------------------------------
//! @file ASTSerializer.h
//! @brief Support for serializing an AST
//
// File is under the MIT license; see LICENSE for details
//------------------------------------------------------------------------------
#pragma once

#include "slang/text/Json.h"
#include "slang/util/Util.h"

namespace slang {

class AttributeSymbol;
class Compilation;
class ConstantValue;
class Expression;
class Statement;
class Symbol;
class Type;

class ASTSerializer {
public:
    ASTSerializer(Compilation& compilation, JsonWriter& writer);

    void serialize(const Symbol& symbol);
    void serialize(const Expression& expr);
    void serialize(const Statement& statement);
    void serialize(std::string_view value);

    void startArray(string_view name);
    void endArray();
    void startObject();
    void endObject();

    void write(string_view name, string_view value);
    void write(string_view name, int64_t value);
    void write(string_view name, uint64_t value);
    void write(string_view name, double value);
    void write(string_view name, bool value);
    void write(string_view name, const std::string& value);
    void write(string_view name, const Symbol& value);
    void write(string_view name, const ConstantValue& value);
    void write(string_view name, const Expression& value);
    void write(string_view name, const Statement& value);

    void writeLink(string_view name, const Symbol& value);

    template<typename T, typename = std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T>>>
    void write(string_view name, T value) {
        write(name, int64_t(value));
    }

    template<typename T, typename = void,
             typename = std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T>>>
    void write(string_view name, T value) {
        write(name, uint64_t(value));
    }

private:
    friend Symbol;
    friend Expression;
    friend Statement;

    template<typename T>
    void visit(const T& symbol);

    void visitInvalid(const Expression& expr);
    void visitInvalid(const Statement& statement);

    Compilation& compilation;
    JsonWriter& writer;
};

} // namespace slang