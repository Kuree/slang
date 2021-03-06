//------------------------------------------------------------------------------
// ASTSerializer.cpp
// Support for serializing an AST
//
// File is under the MIT license; see LICENSE for details
//------------------------------------------------------------------------------
#include "slang/symbols/ASTSerializer.h"

#include "slang/compilation/Compilation.h"
#include "slang/symbols/ASTVisitor.h"

namespace slang {

ASTSerializer::ASTSerializer(Compilation& compilation, JsonWriter& writer) :
    compilation(compilation), writer(writer) {
}

void ASTSerializer::serialize(const Symbol& symbol) {
    symbol.visit(*this);
}

void ASTSerializer::serialize(const Expression& expr) {
    expr.visit(*this);
}

void ASTSerializer::serialize(const Statement& statement) {
    statement.visit(*this);
}

void ASTSerializer::serialize(string_view value) {
    writer.writeValue(value);
}

void ASTSerializer::write(string_view name, string_view value) {
    writer.writeProperty(name);
    writer.writeValue(value);
}

void ASTSerializer::write(string_view name, int64_t value) {
    writer.writeProperty(name);
    writer.writeValue(value);
}

void ASTSerializer::write(string_view name, uint64_t value) {
    writer.writeProperty(name);
    writer.writeValue(value);
}

void ASTSerializer::write(string_view name, double value) {
    writer.writeProperty(name);
    writer.writeValue(value);
}

void ASTSerializer::write(string_view name, bool value) {
    writer.writeProperty(name);
    writer.writeValue(value);
}

void ASTSerializer::write(string_view name, const std::string& value) {
    writer.writeProperty(name);
    writer.writeValue(value);
}

void ASTSerializer::write(string_view name, const Symbol& value) {
    writer.writeProperty(name);
    serialize(value);
}

void ASTSerializer::write(string_view name, const ConstantValue& value) {
    writer.writeProperty(name);
    writer.writeValue(value.toString());
}

void ASTSerializer::write(string_view name, const Expression& value) {
    writer.writeProperty(name);
    serialize(value);
}

void ASTSerializer::write(string_view name, const Statement& value) {
    writer.writeProperty(name);
    serialize(value);
}

void ASTSerializer::writeLink(string_view name, const Symbol& value) {
    writer.writeProperty(name);
    writer.writeValue(std::to_string(uintptr_t(&value)) + " " +
                      (value.isType() ? value.as<Type>().toString() : std::string(value.name)));
}

void ASTSerializer::startArray(string_view name) {
    writer.writeProperty(name);
    writer.startArray();
}

void ASTSerializer::endArray() {
    writer.endArray();
}

void ASTSerializer::startObject() {
    writer.startObject();
}

void ASTSerializer::endObject() {
    writer.endObject();
}

template<typename T>
void ASTSerializer::visit(const T& elem) {
    if constexpr (std::is_base_of_v<Expression, T>) {
        writer.startObject();
        write("kind", toString(elem.kind));
        write("type", *elem.type);

        if constexpr (!std::is_same_v<Expression, T>) {
            elem.serializeTo(*this);
        }

        // This isn't the most efficient thing in the world; it could be improved
        // with some kind of caching of values for individual expressions.
        EvalContext ctx(compilation);
        ConstantValue constant = elem.eval(ctx);
        if (constant)
            write("constant", constant);

        writer.endObject();
    }
    else if constexpr (std::is_base_of_v<Statement, T>) {
        writer.startObject();
        write("kind", toString(elem.kind));

        if constexpr (!std::is_same_v<Statement, T>) {
            elem.serializeTo(*this);
        }

        writer.endObject();
    }
    else if constexpr (std::is_base_of_v<Type, T> && !std::is_same_v<TypeAliasType, T>) {
        writer.writeValue(elem.toString());
    }
    else {
        writer.startObject();
        write("name", elem.name);
        write("kind", toString(elem.kind));
        write("addr", uintptr_t(&elem));

        auto scope = elem.getParentScope();
        if (scope) {
            auto attributes = scope->getCompilation().getAttributes(elem);
            if (!attributes.empty()) {
                startArray("attributes");
                for (auto attr : attributes)
                    serialize(*attr);
                endArray();
            }
        }

        if constexpr (std::is_base_of_v<ValueSymbol, T>) {
            write("type", elem.getType());

            if (auto init = elem.getInitializer())
                write("initializer", *init);
        }

        if constexpr (std::is_base_of_v<Scope, T>) {
            if (!elem.empty()) {
                startArray("members");
                for (auto& member : elem.members())
                    serialize(member);
                endArray();
            }
        }

        if constexpr (!std::is_same_v<Symbol, T>) {
            elem.serializeTo(*this);
        }

        writer.endObject();
    }
}

void ASTSerializer::visitInvalid(const Expression& expr) {
    visit(expr.as<InvalidExpression>());
}

void ASTSerializer::visitInvalid(const slang::Statement& statement) {
    visit(statement.as<InvalidStatement>());
}

} // namespace slang