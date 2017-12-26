//------------------------------------------------------------------------------
// HierarchySymbols.cpp
// Contains hierarchy-related symbol definitions.
//
// File is under the MIT license; see LICENSE for details.
//------------------------------------------------------------------------------
#include "Symbol.h"

#include "binding/Binder.h"
#include "compilation/Compilation.h"
#include "util/HashMap.h"

namespace slang {

PackageSymbol& PackageSymbol::fromSyntax(Compilation& compilation, const ModuleDeclarationSyntax& syntax) {
    auto result = compilation.emplace<PackageSymbol>(compilation, syntax.header.name.valueText(),
                                                     syntax.header.name.location());
    for (auto member : syntax.members)
        result->addMembers(*member);
    return *result;
}

void InstanceSymbol::fromSyntax(Compilation& compilation, const HierarchyInstantiationSyntax& syntax,
                                const Scope& scope, SmallVector<const Symbol*>& results) {
    
    auto definition = compilation.getDefinition(syntax.type.valueText(), scope);
    if (!definition) {
        // TODO: error
        return;
    }

    SmallHashMap<string_view, const ExpressionSyntax*, 8> paramOverrides;
    if (syntax.parameters) {
        // Build up data structures to easily index the parameter assignments. We need to handle
        // both ordered assignment as well as named assignment, though a specific instance can only
        // use one method or the other.
        bool hasParamAssignments = false;
        bool orderedAssignments = true;
        SmallVectorSized<const OrderedArgumentSyntax*, 8> orderedParams;
        SmallHashMap<string_view, std::pair<const NamedArgumentSyntax*, bool>, 8> namedParams;

        // TODO: syntax node names here are dumb
        for (auto paramBase : syntax.parameters->parameters.parameters) {
            bool isOrdered = paramBase->kind == SyntaxKind::OrderedArgument;
            if (!hasParamAssignments) {
                hasParamAssignments = true;
                orderedAssignments = isOrdered;
            }
            else if (isOrdered != orderedAssignments) {
                compilation.addError(DiagCode::MixingOrderedAndNamedParams, paramBase->getFirstToken().location());
                break;
            }

            if (isOrdered)
                orderedParams.append(&paramBase->as<OrderedArgumentSyntax>());
            else {
                const NamedArgumentSyntax& nas = paramBase->as<NamedArgumentSyntax>();
                auto pair = namedParams.emplace(nas.name.valueText(), std::make_pair(&nas, false));
                if (!pair.second) {
                    compilation.addError(DiagCode::DuplicateParamAssignment, nas.name.location()) << nas.name.valueText();
                    compilation.addError(DiagCode::NotePreviousUsage, pair.first->second.first->name.location());
                }
            }
        }

        // For each parameter assignment we have, match it up to a real parameter
        if (orderedAssignments) {
            uint32_t orderedIndex = 0;
            for (auto param : definition->parameters) {
                if (orderedIndex >= orderedParams.size())
                    break;

                if (param.isLocal)
                    continue;

                paramOverrides.emplace(param.name, &orderedParams[orderedIndex++]->expr);
            }

            // Make sure there aren't extra param assignments for non-existent params.
            if (orderedIndex < orderedParams.size()) {
                auto location = orderedParams[orderedIndex]->getFirstToken().location();
                auto& diag = compilation.addError(DiagCode::TooManyParamAssignments, location);
                diag << definition->name;
                diag << orderedParams.size();
                diag << orderedIndex;
            }
        }
        else {
            // Otherwise handle named assignments.
            for (auto param : definition->parameters) {
                auto it = namedParams.find(param.name);
                if (it == namedParams.end())
                    continue;

                const NamedArgumentSyntax* arg = it->second.first;
                it->second.second = true;
                if (param.isLocal) {
                    // Can't assign to localparams, so this is an error.
                    DiagCode code = param.isPort ? DiagCode::AssignedToLocalPortParam : DiagCode::AssignedToLocalBodyParam;
                    compilation.addError(code, arg->name.location());
                    compilation.addError(DiagCode::NoteDeclarationHere, param.location) << string_view("parameter");
                    continue;
                }

                // It's allowed to have no initializer in the assignment; it means to just use the default
                if (!arg->expr)
                    continue;

                paramOverrides.emplace(param.name, arg->expr);
            }

            for (const auto& pair : namedParams) {
                // We marked all the args that we used, so anything left over is a param assignment
                // for a non-existent parameter.
                if (!pair.second.second) {
                    auto& diag = compilation.addError(DiagCode::ParameterDoesNotExist, pair.second.first->name.location());
                    diag << pair.second.first->name.valueText();
                    diag << definition->name;
                }
            }
        }
    }

    for (auto instanceSyntax : syntax.instances) {
        // TODO: other things besides modules
        // TODO: instance arrays
        const auto& instance = ModuleInstanceSymbol::instantiate(compilation, instanceSyntax->name.valueText(),
                                                                 instanceSyntax->name.location(), *definition,
                                                                 paramOverrides);
        results.append(&instance);
    }
}

ModuleInstanceSymbol& ModuleInstanceSymbol::instantiate(Compilation& compilation, string_view name,
                                                        SourceLocation loc, const Definition& definition) {
    SmallHashMap<string_view, const ExpressionSyntax*, 2> paramOverrides;
    return instantiate(compilation, name, loc, definition, paramOverrides);
}

ModuleInstanceSymbol& ModuleInstanceSymbol::instantiate(
    Compilation& compilation, string_view name, SourceLocation loc, const Definition& definition,
    HashMapRef<string_view, const ExpressionSyntax*>& paramOverrides) {

    auto instance = compilation.emplace<ModuleInstanceSymbol>(compilation, name, loc);

    auto parameterPorts = definition.syntax.header.parameters;
    if (parameterPorts) {
        for (auto decl : parameterPorts->declarations) {
            SmallVectorSized<ParameterSymbol*, 16> params;
            ParameterSymbol::fromSyntax(compilation, *decl, params);
            for (auto param : params) {
                // TODO: does this actually need to be lazy?
                instance->addMember(*param);
                if (auto it = paramOverrides.find(param->name); it != paramOverrides.end())
                    param->value = LazyConstant(static_cast<const Scope*>(instance), *it->second);

                // TODO: handle missing value / no default
            }
        }
    }

    for (auto member : definition.syntax.members)
        instance->addMembers(*member);

    return *instance;
}

GenerateBlockSymbol* GenerateBlockSymbol::fromSyntax(Compilation& compilation, const IfGenerateSyntax& syntax,
                                                     const Scope& parent) {
    // TODO: better error checking
    const auto& cv = parent.evaluateConstant(syntax.condition);
    if (!cv)
        return nullptr;

    // TODO: handle named block, location
    const SVInt& value = cv.integer();
    if ((logic_t)value) {
        auto block = compilation.emplace<GenerateBlockSymbol>(compilation, "", SourceLocation());
        block->addMembers(syntax.block);
        return block;
    }
    else if (syntax.elseClause) {
        auto block = compilation.emplace<GenerateBlockSymbol>(compilation, "", SourceLocation());
        block->addMembers(syntax.elseClause->clause);
        return block;
    }
    return nullptr;
}

GenerateBlockArraySymbol& GenerateBlockArraySymbol::fromSyntax(Compilation& compilation,
                                                               const LoopGenerateSyntax& syntax,
                                                               const Scope& parent) {
    // If the loop initializer has a genvar keyword, we can use it directly. Otherwise
    // we need to do a lookup to make sure we have the actual genvar.
    // TODO: do the actual lookup

    // Initialize the genvar
    auto result = compilation.emplace<GenerateBlockArraySymbol>(compilation, "", SourceLocation());
    const auto& initial = parent.evaluateConstant(syntax.initialExpr);
    if (!initial)
        return *result;

    // Fabricate a local variable that will serve as the loop iteration variable.
    SequentialBlockSymbol iterScope(compilation, SourceLocation());
    VariableSymbol local { syntax.identifier.valueText(), syntax.identifier.location() };
    local.type = compilation.getIntType();
    iterScope.addMember(local);

    // Bind the stop and iteration expressions so we can reuse them on each iteration.
    Binder binder(iterScope);
    const auto& stopExpr = binder.bindConstantExpression(syntax.stopExpr);
    const auto& iterExpr = binder.bindConstantExpression(syntax.iterationExpr);

    // Create storage for the iteration variable.
    EvalContext context;
    auto genvar = context.createLocal(&local, initial);

    // Generate blocks!
    SmallVectorSized<const Symbol*, 16> arrayEntries;
    for (; stopExpr.evalBool(context); iterExpr.eval(context)) {
        // Spec: each generate block gets their own scope, with an implicit
        // localparam of the same name as the genvar.
        // TODO: scope name
        auto block = compilation.emplace<GenerateBlockSymbol>(compilation, "", SourceLocation());
        auto implicitParam = compilation.emplace<ParameterSymbol>(syntax.identifier.valueText(),
                                                                  syntax.identifier.location());
        block->addMember(*implicitParam);
        block->addMembers(syntax.block);
        result->addMember(*block);

        implicitParam->value = *genvar;
    }
    return *result;
}

}