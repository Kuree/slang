#include "Test.h"

#include "slang/compilation/Compilation.h"
#include "slang/syntax/SyntaxTree.h"

SVInt testParameter(const std::string& text, uint32_t index = 0) {
    const auto& fullText = "module Top; " + text + " endmodule";
    auto tree = SyntaxTree::fromText(string_view(fullText));

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    const auto& module = *compilation.getRoot().topInstances[0];
    if (!tree->diagnostics().empty())
        WARN(report(tree->diagnostics()));

    const ParameterSymbol& param = module.memberAt<ParameterSymbol>(index);
    return param.getValue().integer();
}

TEST_CASE("Bind parameter") {
    CHECK(testParameter("parameter foo = 4;") == 4);
    CHECK(testParameter("parameter foo = 4 + 5;") == 9);
    CHECK(testParameter("parameter bar = 9, foo = bar + 1;", 1) == 10);
    CHECK(testParameter("parameter logic [3:0] foo = 4;") == 4);
    CHECK(testParameter("parameter logic [3:0] foo = 4'b100;") == 4);
}

TEST_CASE("Evaluate assignment expression") {
    // Evaluate an assignment expression (has an LValue we can observe)
    auto syntax = SyntaxTree::fromText("i = i + 3");

    // Fabricate a symbol for the `i` variable
    Compilation compilation;
    auto& scope = compilation.createScriptScope();

    auto varToken = syntax->root().getFirstToken();
    VariableSymbol local{ varToken.valueText(), varToken.location(), VariableLifetime::Automatic };
    local.setType(compilation.getIntType());

    // Bind the expression tree to the symbol
    scope.addMember(local);
    auto& bound =
        Expression::bind(syntax->root().as<ExpressionSyntax>(),
                         BindContext(scope, LookupLocation::max), BindFlags::AssignmentAllowed);
    REQUIRE(syntax->diagnostics().empty());

    // Initialize `i` to 1.
    EvalContext context(compilation);
    auto i = context.createLocal(&local, SVInt(32, 1, true));

    // Evaluate the expression tree.
    bound.eval(context);
    CHECK(i->integer() == 4);

    // Run it again, results should be as you'd expect
    bound.eval(context);
    CHECK(i->integer() == 7);
    NO_COMPILATION_ERRORS;
}

TEST_CASE("Check type propagation") {
    // Assignment operator should increase RHS size to 20
    auto syntax = SyntaxTree::fromText("i = 5'b0101 + 4'b1100");

    // Fabricate a symbol for the `i` variable
    Compilation compilation;
    auto& scope = compilation.createScriptScope();

    auto varToken = syntax->root().getFirstToken();
    VariableSymbol local{ varToken.valueText(), varToken.location(), VariableLifetime::Automatic };
    local.setType(compilation.getType(20, IntegralFlags::Unsigned));

    // Bind the expression tree to the symbol
    scope.addMember(local);
    auto& bound =
        Expression::bind(syntax->root().as<ExpressionSyntax>(),
                         BindContext(scope, LookupLocation::max), BindFlags::AssignmentAllowed);

    REQUIRE(syntax->diagnostics().empty());

    CHECK(bound.type->getBitWidth() == 20);
    const Expression& rhs = bound.as<AssignmentExpression>().right();
    CHECK(rhs.type->getBitWidth() == 20);
    const Expression& op1 = rhs.as<BinaryExpression>().left();
    CHECK(op1.type->getBitWidth() == 20);
    const Expression& op2 = rhs.as<BinaryExpression>().right();
    CHECK(op2.type->getBitWidth() == 20);
    NO_COMPILATION_ERRORS;
}

TEST_CASE("Check type propagation 2") {
    // Tests a number of rules of size propogation
    auto syntax = SyntaxTree::fromText("i = 2'b1 & (((17'b101 >> 1'b1) - 4'b1100) == 21'b1)");
    Compilation compilation;
    auto& scope = compilation.createScriptScope();

    // Fabricate a symbol for the `i` variable
    auto varToken = syntax->root().getFirstToken();
    VariableSymbol local{ varToken.valueText(), varToken.location(), VariableLifetime::Automatic };
    local.setType(compilation.getType(20, IntegralFlags::Unsigned));

    // Bind the expression tree to the symbol
    scope.addMember(local);
    auto& bound =
        Expression::bind(syntax->root().as<ExpressionSyntax>(),
                         BindContext(scope, LookupLocation::max), BindFlags::AssignmentAllowed);
    REQUIRE(syntax->diagnostics().empty());

    CHECK(bound.type->getBitWidth() == 20);
    const Expression& rhs = bound.as<AssignmentExpression>().right();
    CHECK(rhs.type->getBitWidth() == 20);

    const Expression& rrhs =
        rhs.as<BinaryExpression>().right().as<ConversionExpression>().operand();
    CHECK(rrhs.type->getBitWidth() == 1);

    const Expression& op1 = rrhs.as<BinaryExpression>().left();
    const Expression& shiftExpr = op1.as<BinaryExpression>().left();
    CHECK(shiftExpr.type->getBitWidth() == 21);
    CHECK(op1.type->getBitWidth() == 21);
    const Expression& op2 = rrhs.as<BinaryExpression>().right();
    CHECK(op2.type->getBitWidth() == 21);
    NO_COMPILATION_ERRORS;
}

TEST_CASE("Check type propagation real") {
    // Tests a number of rules of size propogation
    auto syntax = SyntaxTree::fromText("i = 2'b1 & (((17'b101 >> 1'b1) - 2.0) == 21'b1)");
    Compilation compilation;
    auto& scope = compilation.createScriptScope();

    // Fabricate a symbol for the `i` variable
    auto varToken = syntax->root().getFirstToken();
    VariableSymbol local{ varToken.valueText(), varToken.location(), VariableLifetime::Automatic };
    local.setType(compilation.getType(20, IntegralFlags::Unsigned));

    // Bind the expression tree to the symbol
    scope.addMember(local);
    auto& bound =
        Expression::bind(syntax->root().as<ExpressionSyntax>(),
                         BindContext(scope, LookupLocation::max), BindFlags::AssignmentAllowed);
    REQUIRE(syntax->diagnostics().empty());
    CHECK(bound.type->getBitWidth() == 20);

    const Expression& rhs = bound.as<AssignmentExpression>().right();
    CHECK(rhs.type->getBitWidth() == 20);

    const Expression& rrhs =
        rhs.as<BinaryExpression>().right().as<ConversionExpression>().operand();
    CHECK(rrhs.type->getBitWidth() == 1);

    const Expression& op1 = rrhs.as<BinaryExpression>().left();
    const ConversionExpression& convExpr =
        op1.as<BinaryExpression>().left().as<ConversionExpression>();
    CHECK(convExpr.type->getBitWidth() == 64);
    CHECK(convExpr.type->isFloating());

    const Expression& shiftExpr = convExpr.operand();
    CHECK(shiftExpr.type->getBitWidth() == 17);
    CHECK(shiftExpr.type->isIntegral());

    const Expression& rshiftOp = shiftExpr.as<BinaryExpression>().right();
    CHECK(rshiftOp.type->getBitWidth() == 1);

    const Expression& lshiftOp = shiftExpr.as<BinaryExpression>().left();
    CHECK(lshiftOp.type->getBitWidth() == 17);
    CHECK(op1.type->getBitWidth() == 64);
    CHECK(op1.type->isFloating());

    const Expression& op2 = rrhs.as<BinaryExpression>().right();
    CHECK(op2.type->getBitWidth() == 64);
    CHECK(op2.type->isFloating());
    NO_COMPILATION_ERRORS;
}

TEST_CASE("Expression types") {
    Compilation compilation;
    auto& scope = compilation.createScriptScope();

    auto declare = [&](const std::string& source) {
        auto tree = SyntaxTree::fromText(string_view(source));
        scope.getCompilation().addSyntaxTree(tree);
        scope.addMembers(tree->root());
    };

    auto typeof = [&](const std::string& source) {
        auto tree = SyntaxTree::fromText(string_view(source));
        BindContext context(scope, LookupLocation::max);
        return Expression::bind(tree->root().as<ExpressionSyntax>(), context).type->toString();
    };

    declare("logic [7:0] l;");
    declare("logic signed [7:0] sl;");
    declare("logic [7:0][3:2] pa;");
    declare("bit [2:10] b1;");
    declare("int i;");
    declare("integer ig4;");
    declare("real r;");
    declare("shortreal sr;");
    declare("struct packed { logic a; bit b; } sp;");
    declare("union packed { logic [1:0] a; bit [0:1] b; } up;");
    declare("struct { logic a; bit b; } su;");
    declare("struct { bit a; bit b; } su2;");
    declare("reg reg1, reg2;");
    declare("enum {EVAL1, EVAL2} e1;");

    // Literals / misc
    CHECK(typeof("\"asdfg\"") == "bit[39:0]");
    CHECK(typeof("reg1 + reg2") == "reg");
    CHECK(typeof("e1") == "enum{EVAL1=32'sd0,EVAL2=32'sd1}e$1");
    CHECK(typeof("10.234ns") == "realtime");

    // Unary operators
    CHECK(typeof("+i") == "int");
    CHECK(typeof("-sp") == "struct packed{logic a;bit b;}s$1");
    CHECK(typeof("!r") == "bit");
    CHECK(typeof("~l") == "logic[7:0]");
    CHECK(typeof("~r") == "<error>");
    CHECK(typeof("&l") == "logic");
    CHECK(typeof("~^b1") == "bit");

    // Binary operators
    CHECK(typeof("l + pa") == "logic[15:0]");
    CHECK(typeof("sl - pa") == "logic[15:0]");
    CHECK(typeof("sl * 16'sd5") == "logic signed[15:0]"); // both signed, result is signed
    CHECK(typeof("b1 * i") == "bit[31:0]");               // 2 state result
    CHECK(typeof("b1 / i") == "logic[31:0]");             // divide always produces 4 state
    CHECK(typeof("b1 % i") == "logic[31:0]");             // mod always produces 4 state
    CHECK(typeof("b1 ** (9234'd234)") == "logic[8:0]");   // self determined from lhs
    CHECK(typeof("r + sr") == "real");
    CHECK(typeof("sr + sr") == "shortreal");
    CHECK(typeof("l + r") == "real");
    CHECK(typeof("l + sr") == "shortreal");
    CHECK(typeof("sp < r") == "logic");
    CHECK(typeof("su < r") == "<error>");
    CHECK(typeof("pa <<< b1") == "logic[7:0][3:2]");
    CHECK(typeof("b1 >> b1") == "bit[2:10]");
    CHECK(typeof("b1 >> sl") == "logic[8:0]");
    CHECK(typeof("sp == l") == "logic");
    CHECK(typeof("b1 == b1") == "bit");
    CHECK(typeof("b1 != l") == "logic");
    CHECK(typeof("b1 === b1") == "bit");
    CHECK(typeof("b1 !== l") == "bit");
    CHECK(typeof("r == b1") == "bit");
    CHECK(typeof("b1 == r") == "bit");
    CHECK(typeof("l == r") == "logic");
    CHECK(typeof("su == su") == "logic");
    CHECK(typeof("su2 == su2") == "bit");
    CHECK(typeof("EVAL1 + 5") == "int");
    CHECK(typeof("up + 5") == "logic[31:0]");
    CHECK(typeof("up + up") == "logic[1:0]");

    // Unpacked arrays
    declare("bit [7:0] arr1 [2];");
    declare("bit [7:0] arr2 [2:0];");
    declare("bit [7:0] arr3 [3];");
    CHECK(typeof("arr1 == arr2") == "<error>");
    CHECK(typeof("arr2 == arr3") == "bit");
    CHECK(typeof("arr1 == arr3[0:1]") == "bit");

    // Conditional operator
    CHECK(typeof("i ? l : pa") == "logic[15:0]");
    CHECK(typeof("r ? b1 : i") == "bit[31:0]");
    CHECK(typeof("i ? arr2 : arr3") == "bit[7:0]$[2:0]");
    CHECK(typeof("i ? arr1: arr2") == "<error>");
    CHECK(typeof("arr2 ? 1 : 0") == "<error>");
    CHECK(typeof("i ? EVAL1 : EVAL2") == "enum{EVAL1=32'sd0,EVAL2=32'sd1}e$1");
    CHECK(typeof("b1 ? e1 : e1") == "enum{EVAL1=32'sd0,EVAL2=32'sd1}e$1");
    CHECK(typeof("ig4 ? e1 : EVAL1") == "enum{EVAL1=32'sd0,EVAL2=32'sd1}e$1");

    // Member access
    declare("struct packed { logic [13:0] a; bit b; } foo;");
    declare("struct packed { logic [13:0] a; bit b; } [3:0] spPackedArray;");
    declare("union { logic [13:0] a; int b; } upUnion;");
    CHECK(typeof("foo.a") == "logic[13:0]");
    CHECK(typeof("spPackedArray") == "struct packed{logic[13:0] a;bit b;}s$5[3:0]");
    CHECK(typeof("spPackedArray[0].a") == "logic[13:0]");
    CHECK(typeof("upUnion.a") == "logic[13:0]");
    CHECK(typeof("upUnion.b") == "int");

    // Casts
    declare("parameter int FOO = 1;");
    CHECK(typeof("(FOO + 2)'(b1)") == "bit[2:0]");
    CHECK(typeof("int'(b1)") == "int");
    CHECK(typeof("5'(sp)") == "logic[4:0]");
    CHECK(typeof("signed'(b1)") == "bit signed[8:0]");
    CHECK(typeof("unsigned'(b1)") == "bit[8:0]");
    CHECK(typeof("signed'(sl)") == "logic signed[7:0]");
    CHECK(typeof("unsigned'(sl)") == "logic[7:0]");
    CHECK(typeof("const'(sp)") == "struct packed{logic a;bit b;}s$1");
    CHECK(typeof("const'(FOO)") == "int");
    CHECK(typeof("const'(r)") == "real");

    // Strings
    declare("string s1 = \"asdf\";");
    declare("string s2 = \"asdf\" | 1;");
    declare("string s3 = 1 ? \"asdf\" : \"bar\";");
    declare("string s4 = {\"asdf\", 8'd42};");

    // Inside expressions
    CHECK(typeof("i inside { 4, arr3, pa, sp }") == "logic");

    auto& diags = compilation.getAllDiagnostics();
    REQUIRE(diags.size() == 5);
    CHECK(diags[0].code == diag::BadUnaryExpression);
    CHECK(diags[1].code == diag::BadBinaryExpression);
    CHECK(diags[2].code == diag::BadBinaryExpression);
    CHECK(diags[3].code == diag::BadConditionalExpression);
    CHECK(diags[4].code == diag::NotBooleanConvertible);
}

TEST_CASE("Expression - bad name references") {
    auto tree = SyntaxTree::fromText(R"(
module m1;

    typedef struct { logic f; } T;

    int i = T + 2;      // not a value
    int j = (3 + 4)(2); // not callable
    int k = i(2);       // not a task or function

endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diags = compilation.getAllDiagnostics();
    REQUIRE(diags.size() == 3);
    CHECK(diags[0].code == diag::NotAValue);
    CHECK(diags[1].code == diag::ExpressionNotCallable);
    CHECK(diags[2].code == diag::ExpressionNotCallable);
}

TEST_CASE("Expression - bad use of data type") {
    auto tree = SyntaxTree::fromText(R"(
module m1;

    typedef int blah;

    int i = int;
    int j = -(int + 1);
    int k = (blah * 2);
    int l = $bits(blah & 2);

endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diags = compilation.getAllDiagnostics();
    REQUIRE(diags.size() == 4);
    CHECK(diags[0].code == diag::ExpectedExpression);
    CHECK(diags[1].code == diag::ExpectedExpression);
    CHECK(diags[2].code == diag::NotAValue);
    CHECK(diags[3].code == diag::NotAValue);
}

TEST_CASE("Expression - allowed data type") {
    auto tree = SyntaxTree::fromText(R"(
module m1;

    typedef int blah;

    int i = $bits(blah);
    int j = $bits(logic[3:0]);
    string s = $typename(blah);
    string t = $typename(logic[3:0]);

endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    NO_COMPILATION_ERRORS;
}

TEST_CASE("$bits / typename - hierarchical allowed in non-const") {
    auto tree = SyntaxTree::fromText(R"(
module m1;
    int i = $bits(n.asdf);
    string s = $typename(n.asdf);
endmodule

module n;
    logic [5:1] asdf;
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    NO_COMPILATION_ERRORS;
}

TEST_CASE("Checking for required constant subexpressions") {
    auto tree = SyntaxTree::fromText(R"(
module m1;

    int a;
    function int foo;
        return a;
    endfunction

    logic [3:0] asdf;
    always_comb asdf = asdf[foo:0];
    always_comb asdf = asdf[0+:foo];
    always_comb asdf = {foo {1}};
    always_comb asdf = foo'(1);

endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diags = compilation.getAllDiagnostics();
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].code == diag::ConstEvalFunctionIdentifiersMustBeLocal);
}

TEST_CASE("Invalid string conversions") {
    auto tree = SyntaxTree::fromText(R"(
module m1;

    string s;

    typedef logic[15:0] r_t;
    r_t r;

    always_comb begin
        s = r;
        r = s;
        r = r_t'(s);    // ok
        s = string'(r); // ok
    end

endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diags = compilation.getAllDiagnostics();
    REQUIRE(diags.size() == 2);
    CHECK(diags[0].code == diag::NoImplicitConversion);
    CHECK(diags[1].code == diag::NoImplicitConversion);
}

TEST_CASE("Integer literal corner cases") {
    auto tree = SyntaxTree::fromText(R"(
`define FOO aa_ff
`define BAR 'h

module m1;

    int i = 35'd123498234978234;
    int j = 0'd234;
    int k = 16777216'd1;
    int l = 16   `BAR `FOO;
    integer m = 'b ??0101?1;
    int n = 999999999999;
    int o = 'b _?1;
    int p = 'b3;
    int q = 'ox789;
    int r = 'd?;
    int s = 'd  z_;
    int t = 'd x1;
    int u = 'd a;
    int v = 'h g;
    int w = 3'h f;
    int x = 'd;

endmodule
)",
                                     "source");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diagnostics = compilation.getAllDiagnostics();
    std::string result = "\n" + report(diagnostics);
    CHECK(result == R"(
source:7:17: warning: vector literal too large for the given number of bits [-Wliteral-overflow]
    int i = 35'd123498234978234;
                ^
source:8:13: error: size of vector literal cannot be zero
    int j = 0'd234;
            ^
source:9:13: error: size of vector literal is too large (> 16777215 bits)
    int k = 16777216'd1;
            ^
source:12:13: error: signed integer overflows 32 bits
    int n = 999999999999;
            ^
source:13:16: error: numeric literals must not start with a leading underscore
    int o = 'b _?1;
               ^
source:14:15: error: expected binary digit
    int p = 'b3;
              ^
source:15:17: error: expected octal digit
    int q = 'ox789;
                ^
source:18:17: error: decimal literals cannot have multiple digits if at least one of them is X or Z
    int t = 'd x1;
                ^
source:19:16: error: expected decimal digit
    int u = 'd a;
               ^
source:20:16: error: expected hexadecimal digit
    int v = 'h g;
               ^
source:21:17: warning: vector literal too large for the given number of bits [-Wliteral-overflow]
    int w = 3'h f;
                ^
source:22:15: error: expected vector literal digits
    int x = 'd;
              ^
)");
}

TEST_CASE("Real literal corner cases") {
    auto tree = SyntaxTree::fromText(R"(
module m1;
    real a = 9999e99999;
    real b = 9999e-99999;
endmodule
)",
                                     "source");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diagnostics = compilation.getAllDiagnostics();
    std::string result = "\n" + report(diagnostics);
    CHECK(result == R"(
source:3:14: warning: value of real literal is too large; maximum is 1.79769e+308 [-Wreal-overflow]
    real a = 9999e99999;
             ^
source:4:14: warning: value of real literal is too small; minimum is 4.94066e-324 [-Wreal-underflow]
    real b = 9999e-99999;
             ^
)");
}

TEST_CASE("Crazy long hex literal") {
    std::string str = "int i = 'h";
    str += std::string(4194304, 'f');
    str += ';';

    auto tree = SyntaxTree::fromText(str);

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diags = compilation.getAllDiagnostics();
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].code == diag::LiteralSizeTooLarge);
}

// TODO: optimize and re-enable
// TEST_CASE("Crazy long decimal literal") {
//    std::string str = "int i = 'd";
//    str += std::string(5050446, '9');
//    str += ';';
//
//    auto tree = SyntaxTree::fromText(str);
//
//    Compilation compilation;
//    compilation.addSyntaxTree(tree);
//
//    auto& diags = compilation.getAllDiagnostics();
//    REQUIRE(diags.size() == 1);
//    CHECK(diags[0].code == diag::LiteralSizeTooLarge);
//}

TEST_CASE("Simple assignment patterns") {
    auto tree = SyntaxTree::fromText(R"(
module n(input int frob[3]);
endmodule

module m;

    parameter int foo[2] = '{42, -39};
    parameter struct { int a; logic [1:0] b; } asdf = '{999, '{1, 0}};

    typedef struct { int a; int b; int c; } type_t;
    parameter bar = type_t '{1, 2, 3};

    type_t baz;
    initial baz = '{1, 2, 3};

    n n1('{1, 2, 3});

endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    NO_COMPILATION_ERRORS;

    auto& foo = compilation.getRoot().lookupName<ParameterSymbol>("m.foo");
    auto elems = foo.getValue().elements();
    REQUIRE(elems.size() == 2);
    CHECK(elems[0].integer() == 42);
    CHECK(elems[1].integer() == -39);

    auto& asdf = compilation.getRoot().lookupName<ParameterSymbol>("m.asdf");
    elems = asdf.getValue().elements();
    REQUIRE(elems.size() == 2);
    CHECK(elems[0].integer() == 999);
    CHECK(elems[1].integer() == 2);

    auto& bar = compilation.getRoot().lookupName<ParameterSymbol>("m.bar");
    elems = bar.getValue().elements();
    REQUIRE(elems.size() == 3);
    CHECK(elems[0].integer() == 1);
    CHECK(elems[1].integer() == 2);
    CHECK(elems[2].integer() == 3);
}

TEST_CASE("Replicated assignment patterns") {
    auto tree = SyntaxTree::fromText(R"(
module n(input int frob[3]);
endmodule

module m;

    parameter int foo[2] = '{2 {42}};
    parameter struct { int a; logic [1:0] b; } asdf = '{2 {2}};

    typedef struct { int a; shortint b; integer c; longint d; } type_t;
    parameter bar = type_t '{2 {1, 2}};

    type_t baz;
    initial baz = '{2 {1, 2}};

    n n1('{3 {2}});

endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    NO_COMPILATION_ERRORS;

    auto& foo = compilation.getRoot().lookupName<ParameterSymbol>("m.foo");
    auto elems = foo.getValue().elements();
    REQUIRE(elems.size() == 2);
    CHECK(elems[0].integer() == 42);
    CHECK(elems[1].integer() == 42);

    auto& asdf = compilation.getRoot().lookupName<ParameterSymbol>("m.asdf");
    elems = asdf.getValue().elements();
    REQUIRE(elems.size() == 2);
    CHECK(elems[0].integer() == 2);
    CHECK(elems[1].integer() == 2);

    auto& bar = compilation.getRoot().lookupName<ParameterSymbol>("m.bar");
    elems = bar.getValue().elements();
    REQUIRE(elems.size() == 4);
    CHECK(elems[0].integer() == 1);
    CHECK(elems[1].integer() == 2);
    CHECK(elems[2].integer() == 1);
    CHECK(elems[3].integer() == 2);
}

TEST_CASE("Structured assignment patterns") {
    auto tree = SyntaxTree::fromText(R"(
module n(input int frob[3]);
endmodule

module m;

    typedef struct { int a; shortint b; integer c; longint d; logic [1:0] e; } type_t;
    parameter type_t bar = '{ c:9, default:2, int:42, int:37, d:-1 };

    parameter int index = 1 * 2 - 1;
    parameter int foo[3] = '{ default:0, int:1, index - 1 + 1:-42 };

    type_t baz;
    initial baz = '{ c:9, default:2, int:42, int:37, d:-1 };

    n n1('{ default:0, int:1, index - 1 + 1:-42 });

endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    NO_COMPILATION_ERRORS;

    auto& bar = compilation.getRoot().lookupName<ParameterSymbol>("m.bar");
    auto elems = bar.getValue().elements();
    REQUIRE(elems.size() == 5);
    CHECK(elems[0].integer() == 37);
    CHECK(elems[1].integer() == 2);
    CHECK(elems[2].integer() == 9);
    CHECK(elems[3].integer() == -1);
    CHECK(elems[4].integer() == 2);

    auto& foo = compilation.getRoot().lookupName<ParameterSymbol>("m.foo");
    elems = foo.getValue().elements();
    REQUIRE(elems.size() == 3);
    CHECK(elems[0].integer() == 1);
    CHECK(elems[1].integer() == -42);
    CHECK(elems[2].integer() == 1);
}

TEST_CASE("Array select out of bounds - valid") {
    auto tree = SyntaxTree::fromText(R"(
module m;
    localparam logic[3:0][31:0] foo = '{default:0};
    localparam int n = -1;

    localparam int j = n >= 0 ? foo[n] : -4;
    int k = n >= 0 ? foo[n] : -4;

    localparam logic[1:0][31:0] l = n >= 0 ? foo[1:n] : '0;
    logic[1:0][31:0] o = n >= 0 ? foo[1:n] : '0;

    localparam logic[1:0][31:0] p = n >= 0 ? foo[n+:2] : '0;
    logic[1:0][31:0] q = n >= 0 ? foo[n+:2] : '0;
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    NO_COMPILATION_ERRORS;
}

TEST_CASE("Array select out of bounds - invalid") {
    auto tree = SyntaxTree::fromText(R"(
module m;
    localparam logic[3:0][31:0] foo = '{default:0};
    localparam int n = -1;

    localparam int j = n >= -2 ? foo[n] : -4;
    int k = n >= -2 ? foo[n] : -4;

    localparam logic[1:0][31:0] l = n >= -2 ? foo[1:n] : '0;
    logic[1:0][31:0] o = n >= -2 ? foo[1:n] : '0;

    localparam logic[1:0][31:0] p = n >= -2 ? foo[n+:2] : '0;
    logic[1:0][31:0] q = n >= -2 ? foo[n+:2] : '0;
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diags = compilation.getAllDiagnostics();
    REQUIRE(diags.size() == 6);
    CHECK(diags[0].code == diag::IndexValueInvalid);
    CHECK(diags[1].code == diag::IndexValueInvalid);
    CHECK(diags[2].code == diag::BadRangeExpression);
    CHECK(diags[3].code == diag::BadRangeExpression);
    CHECK(diags[4].code == diag::BadRangeExpression);
    CHECK(diags[5].code == diag::BadRangeExpression);
}

TEST_CASE("Consteval - infinite recursion checking") {
    auto tree = SyntaxTree::fromText(R"(
function int foo;
    return bar() + 1;
endfunction

function int bar;
    return foo;
endfunction

module m;
    localparam int i = foo();
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diags = compilation.getAllDiagnostics();
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].code == diag::ConstEvalExceededMaxCallDepth);
}

TEST_CASE("Consteval - infinite loop checking") {
    auto tree = SyntaxTree::fromText(R"(
function int foo;
    for (int i = 0; i < 10000; i++) begin end
endfunction

module m;
    localparam int i = foo();
endmodule
)");

    // Reduce this a bit just to make the tests faster.
    CompilationOptions co;
    co.maxConstexprSteps = 8192;

    Bag options;
    options.add(co);

    Compilation compilation(options);
    compilation.addSyntaxTree(tree);

    auto& diags = compilation.getAllDiagnostics();
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].code == diag::ConstEvalExceededMaxSteps);
}

TEST_CASE("Consteval - enum used in constant function") {
    auto tree = SyntaxTree::fromText(R"(
typedef enum { A, B } e_t;

function int foo;
    return A;
endfunction

module m;
    localparam int i = foo();
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    NO_COMPILATION_ERRORS;
}

TEST_CASE("Disallowed assignment contexts") {
    auto tree = SyntaxTree::fromText(R"(
module m;
    int i;
    int j;
    logic [(j = 2) : 0] asdf;
    assign i = 1 + (j = 1);

    initial i = {j = 1};
    initial if (i = 1) begin end

    assign i = j++;
    assign i = ++j;

    // This is ok
    initial i = 1 + (j = 1);

    // Initialization in a procedural context is also ok
    initial begin
        automatic int k = 1;
        automatic int l = k++;
        static int m = 2;
        static int n = m++;

        static int foo = k; // disallowed
    end
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diags = compilation.getAllDiagnostics();
    REQUIRE(diags.size() == 7);
    CHECK(diags[0].code == diag::AssignmentNotAllowed);
    CHECK(diags[1].code == diag::AssignmentNotAllowed);
    CHECK(diags[2].code == diag::AssignmentRequiresParens);
    CHECK(diags[3].code == diag::AssignmentRequiresParens);
    CHECK(diags[4].code == diag::IncDecNotAllowed);
    CHECK(diags[5].code == diag::IncDecNotAllowed);
    CHECK(diags[6].code == diag::AutoFromStaticInit);
}

TEST_CASE("Assignment error checking") {
    auto tree = SyntaxTree::fromText(R"(
module m;
    enum { ASD = 2 } asdf;
    localparam int i[3] = '{1, 0, ASD};
    const struct { int j = i[0] + 2; } foo;

    initial begin
        ASD = 3;
        i[0] = 4;
        foo.j = 5;
    end

    always begin
        automatic int i;
        i <= 1;
    end
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diags = compilation.getAllDiagnostics();
    REQUIRE(diags.size() == 4);
    CHECK(diags[0].code == diag::ExpressionNotAssignable);
    CHECK(diags[1].code == diag::ExpressionNotAssignable);
    CHECK(diags[2].code == diag::AssignmentToConst);
    CHECK(diags[3].code == diag::NonblockingAssignmentToAuto);
}

TEST_CASE("Subroutine calls with out params from various contexts") {
    auto tree = SyntaxTree::fromText(R"(
module m;
    // These are all fine
    initial begin
        automatic int i = 1;
        automatic int j = mutate(i);
        static int k = 2;
        static int l = mutate(k);
        void'(mutate(j));
        void'(mutate(k));
    end

    int i;
    int j = mutate(i);

    function int mutate(output int f);
        f++;
        return f - 1;
    endfunction
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    NO_COMPILATION_ERRORS;
}

TEST_CASE("Type operator") {
    auto tree = SyntaxTree::fromText(R"(
module m;
    logic [3:0] a;
    logic [4:0] b;
    var type(a + b) foo = a + b;
    int i = type(int)'(a);
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    NO_COMPILATION_ERRORS;
}

TEST_CASE("Time literal + units / precision") {
    auto tree = SyntaxTree::fromText(R"(
module m;
    timeunit 1ns / 1ps;
    localparam real r = 234.0567891ns;
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    NO_COMPILATION_ERRORS;

    auto& r = compilation.getRoot().lookupName<ParameterSymbol>("m.r");
    CHECK(r.getValue().real() == 234.057);
}