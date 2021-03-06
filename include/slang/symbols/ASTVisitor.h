//------------------------------------------------------------------------------
//! @file ASTVisitor.h
//! @brief AST traversal
//
// File is under the MIT license; see LICENSE for details
//------------------------------------------------------------------------------
#pragma once

#include "slang/binding/AssignmentExpressions.h"
#include "slang/binding/LiteralExpressions.h"
#include "slang/binding/MiscExpressions.h"
#include "slang/binding/OperatorExpressions.h"
#include "slang/binding/PatternExpressions.h"
#include "slang/binding/Statements.h"
#include "slang/symbols/AllTypes.h"
#include "slang/symbols/AttributeSymbol.h"
#include "slang/symbols/BlockSymbols.h"
#include "slang/symbols/CompilationUnitSymbols.h"
#include "slang/symbols/InstanceSymbols.h"
#include "slang/symbols/MemberSymbols.h"
#include "slang/symbols/ParameterSymbols.h"
#include "slang/symbols/PortSymbols.h"
#include "slang/symbols/VariableSymbols.h"
#include "slang/util/TypeTraits.h"

namespace slang {

/// Use this type as a base class for AST visitors. It will default to
/// traversing all children of each node. Add implementations for any specific
/// node types you want to handle.
template<typename TDerived, bool VisitStatements, bool VisitExpressions>
struct ASTVisitor {
    template<typename T, typename Arg>
    using handle_t = decltype(std::declval<T>().handle(std::declval<Arg>()));

    template<typename T, typename Arg>
    using op_t = decltype(std::declval<T>()(std::declval<Arg>()));

    template<typename T>
    using getBody_t = decltype(std::declval<T>().getBody());

    template<typename T, typename Arg>
    using visitExprs_t = decltype(std::declval<T>().visitExprs(std::declval<Arg>()));

#define DERIVED *static_cast<TDerived*>(this)

public:
    template<typename T>
    void visit(const T& t) {
        if constexpr (is_detected_v<handle_t, TDerived, T>)
            static_cast<TDerived*>(this)->handle(t);
        else if constexpr (is_detected_v<op_t, TDerived, T>)
            (DERIVED)(t);
        else
            visitDefault(t);
    }

    template<typename T>
    void visitDefault(const T& t) {
        if constexpr (VisitExpressions && is_detected_v<visitExprs_t, T, TDerived>) {
            t.visitExprs(DERIVED);
        }

        if constexpr (VisitExpressions && std::is_base_of_v<Symbol, T>) {
            if (auto declaredType = t.getDeclaredType()) {
                if (auto init = declaredType->getInitializer())
                    init->visit(DERIVED);
            }
        }

        if constexpr (std::is_base_of_v<Scope, T>) {
            for (auto& member : t.members())
                member.visit(DERIVED);
        }

        if constexpr (is_detected_v<getBody_t, T>) {
            t.getBody().visit(DERIVED);
        }
    }

    void visitDefault(const Symbol&) {}
    void visitDefault(const Expression&) {}
    void visitDefault(const Statement&) {}

    void visitInvalid(const Expression&) {}
    void visitInvalid(const Statement&) {}

#undef DERIVED
};

template<typename... Functions>
auto makeVisitor(Functions... funcs) {
    struct Result : public Functions..., public ASTVisitor<Result, true, true> {
        Result(Functions... funcs) : Functions(std::move(funcs))... {}
        using Functions::operator()...;
    };
    return Result(std::move(funcs)...);
}

template<typename TVisitor, typename... Args>
decltype(auto) Symbol::visit(TVisitor&& visitor, Args&&... args) const {
    // clang-format off
#define SYMBOL(k) case SymbolKind::k: return visitor.visit(*static_cast<const k##Symbol*>(this), std::forward<Args>(args)...)
#define TYPE(k) case SymbolKind::k: return visitor.visit(*static_cast<const k*>(this), std::forward<Args>(args)...)
    switch (kind) {
        case SymbolKind::Unknown: return visitor.visit(*this, std::forward<Args>(args)...);
        case SymbolKind::DeferredMember: return visitor.visit(*this, std::forward<Args>(args)...);
        case SymbolKind::TypeAlias: return visitor.visit(*static_cast<const TypeAliasType*>(this), std::forward<Args>(args)...);
        SYMBOL(Root);
        SYMBOL(CompilationUnit);
        SYMBOL(Attribute);
        SYMBOL(TransparentMember);
        SYMBOL(EmptyMember);
        SYMBOL(EnumValue);
        SYMBOL(ForwardingTypedef);
        SYMBOL(Parameter);
        SYMBOL(TypeParameter);
        SYMBOL(Port);
        SYMBOL(InterfacePort);
        SYMBOL(ModuleInstance);
        SYMBOL(ProgramInstance);
        SYMBOL(InterfaceInstance);
        SYMBOL(InstanceArray);
        SYMBOL(Package);
        SYMBOL(ExplicitImport);
        SYMBOL(WildcardImport);
        SYMBOL(GenerateBlock);
        SYMBOL(GenerateBlockArray);
        SYMBOL(ProceduralBlock);
        SYMBOL(StatementBlock);
        SYMBOL(Net);
        SYMBOL(Variable);
        SYMBOL(FormalArgument);
        SYMBOL(Field);
        SYMBOL(Subroutine);
        SYMBOL(Modport);
        SYMBOL(ContinuousAssign);
        SYMBOL(Genvar);
        SYMBOL(Gate);
        SYMBOL(GateArray);
        TYPE(PredefinedIntegerType);
        TYPE(ScalarType);
        TYPE(FloatingType);
        TYPE(EnumType);
        TYPE(PackedArrayType);
        TYPE(UnpackedArrayType);
        TYPE(PackedStructType);
        TYPE(UnpackedStructType);
        TYPE(PackedUnionType);
        TYPE(UnpackedUnionType);
        TYPE(VoidType);
        TYPE(NullType);
        TYPE(CHandleType);
        TYPE(StringType);
        TYPE(EventType);
        TYPE(ErrorType);
        TYPE(NetType);

        case SymbolKind::ClassType: THROW_UNREACHABLE;
    }
#undef TYPE
#undef SYMBOL
    // clang-format on
    THROW_UNREACHABLE;
}

template<typename TVisitor, typename... Args>
decltype(auto) Statement::visit(TVisitor&& visitor, Args&&... args) const {
    // clang-format off
#define CASE(k, n) case StatementKind::k: return visitor.visit(*static_cast<const n*>(this), std::forward<Args>(args)...)
    switch (kind) {
        case StatementKind::Invalid: return visitor.visitInvalid(*this, std::forward<Args>(args)...);
        CASE(Empty, EmptyStatement);
        CASE(List, StatementList);
        CASE(Block, BlockStatement);
        CASE(ExpressionStatement, ExpressionStatement);
        CASE(VariableDeclaration, VariableDeclStatement);
        CASE(Return, ReturnStatement);
        CASE(Break, BreakStatement);
        CASE(Continue, ContinueStatement);
        CASE(Conditional, ConditionalStatement);
        CASE(Case, CaseStatement);
        CASE(ForLoop, ForLoopStatement);
        CASE(RepeatLoop, RepeatLoopStatement);
        CASE(ForeachLoop, ForeachLoopStatement);
        CASE(WhileLoop, WhileLoopStatement);
        CASE(DoWhileLoop, DoWhileLoopStatement);
        CASE(ForeverLoop, ForeverLoopStatement);
        CASE(Timed, TimedStatement);
        CASE(Assertion, AssertionStatement);
    }
#undef CASE
    // clang-format on
    THROW_UNREACHABLE;
}

template<typename TExpression, typename TVisitor, typename... Args>
decltype(auto) Expression::visitExpression(TExpression* expr, TVisitor&& visitor,
                                           Args&&... args) const {
    // clang-format off
#define CASE(k, n) case ExpressionKind::k: return visitor.visit(\
                        *static_cast<std::conditional_t<std::is_const_v<TExpression>, const n*, n*>>(expr),\
                            std::forward<Args>(args)...)

    switch (expr->kind) {
        case ExpressionKind::Invalid: return visitor.visitInvalid(*expr, std::forward<Args>(args)...);
        CASE(IntegerLiteral, IntegerLiteral);
        CASE(RealLiteral, RealLiteral);
        CASE(TimeLiteral, TimeLiteral);
        CASE(UnbasedUnsizedIntegerLiteral, UnbasedUnsizedIntegerLiteral);
        CASE(NullLiteral, NullLiteral);
        CASE(StringLiteral, StringLiteral);
        CASE(NamedValue, NamedValueExpression);
        CASE(UnaryOp, UnaryExpression);
        CASE(BinaryOp, BinaryExpression);
        CASE(ConditionalOp, ConditionalExpression);
        CASE(Inside, InsideExpression);
        CASE(Assignment, AssignmentExpression);
        CASE(Concatenation, ConcatenationExpression);
        CASE(Replication, ReplicationExpression);
        CASE(ElementSelect, ElementSelectExpression);
        CASE(RangeSelect, RangeSelectExpression);
        CASE(MemberAccess, MemberAccessExpression);
        CASE(Call, CallExpression);
        CASE(Conversion, ConversionExpression);
        CASE(DataType, DataTypeExpression);
        CASE(SimpleAssignmentPattern, SimpleAssignmentPatternExpression);
        CASE(StructuredAssignmentPattern, StructuredAssignmentPatternExpression);
        CASE(ReplicatedAssignmentPattern, ReplicatedAssignmentPatternExpression);
        CASE(EmptyArgument, EmptyArgumentExpression);
        CASE(OpenRange, OpenRangeExpression);
    }
#undef CASE
    // clang-format on
    THROW_UNREACHABLE;
}

template<typename TVisitor, typename... Args>
decltype(auto) Expression::visit(TVisitor&& visitor, Args&&... args) const {
    return visitExpression(this, visitor, std::forward<Args>(args)...);
}

template<typename TVisitor, typename... Args>
decltype(auto) Expression::visit(TVisitor&& visitor, Args&&... args) {
    return visitExpression(this, visitor, std::forward<Args>(args)...);
}

} // namespace slang