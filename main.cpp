#include <cstdio>
#include <cstdlib>
#include <memory>
#include <cstdarg>

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/NoFolder.h"
#include "llvm/Support/raw_ostream.h"

using namespace std;
using namespace llvm;

#define NUMBER 256
#define IF 258
#define ELSE 259
#define WHILE 260

static unique_ptr<LLVMContext> TheContext;
static unique_ptr<IRBuilder<NoFolder>> Builder;
static unique_ptr<Module> TheModule;

static void InitializeModule()
{
    TheContext = std::make_unique<LLVMContext>();
    TheModule = std::make_unique<Module>("MyModule", *TheContext);
    Builder = std::make_unique<IRBuilder<NoFolder>>(*TheContext);
}

//===----------------------------------------------------------------------===//
// AST nodes
//===----------------------------------------------------------------------===//
class GenericASTNode
{
public:
    virtual ~GenericASTNode() = default;
    virtual void toString(){};
    virtual Value *codegen() = 0;
};

class StatementASTNode : public GenericASTNode {
    unique_ptr<GenericASTNode> node;
    unique_ptr<GenericASTNode> nextNode;

public:
    StatementASTNode(unique_ptr<GenericASTNode> node, unique_ptr<GenericASTNode> nextNode = nullptr)
        : node(std::move(node)), nextNode(std::move(nextNode)) {}

    void setNextNode(unique_ptr<GenericASTNode> next) {
        nextNode = std::move(next);
    }

    GenericASTNode* getNextNode() const {
        return nextNode.get();
    }

    void toString() override {
        printf("Statement:\n");
        node->toString();
        if (nextNode) {
            printf("\nNext Statement:\n");
            nextNode->toString();
        }
    }

    Value *codegen() override {
        if (!node->codegen()) return nullptr;
        if (nextNode) return nextNode->codegen();
        return ConstantInt::get(*TheContext, APInt(32, 0));
    }
};



class NumberASTNode : public GenericASTNode
{
    int Val;
 
public:
    NumberASTNode(int Val)
    {
        this->Val = Val;
    }
    void toString()
    {
        printf("Number: %d", this->Val);
    }
    Value *codegen()
    {
        return ConstantInt::get(*TheContext, APInt(32, this->Val, true));
    }
};

class VariableReadASTNode : public GenericASTNode {
    string name;

public:
    VariableReadASTNode(const string &name) : name(name) {}

    void toString() override {
        printf("Variable Read: %s", name.c_str());
    }

    Value *codegen() override {
        Value *V = TheModule->getNamedGlobal(name);
        if (!V) {
            fprintf(stderr, "Error: Unknown variable %s\n", name.c_str());
            return nullptr;
        }
        return Builder->CreateLoad(Type::getInt32Ty(*TheContext), V, name.c_str());
    }
};


class VariableDeclarationASTNode : public GenericASTNode {
    string name;

public:
    VariableDeclarationASTNode(const string &name) : name(name) {}

    void toString() override {
        printf("Variable Declaration: %s", name.c_str());
    }

    Value *codegen() override {
        Constant *InitVal = ConstantInt::get(Type::getInt32Ty(*TheContext), 0);
        GlobalVariable *GV = new GlobalVariable(
            *TheModule,                          
            Type::getInt32Ty(*TheContext),       
            false,                               
            GlobalValue::ExternalLinkage,        
            InitVal,                             
            "varName"                            
        );

        return GV;
    }
};


class VariableAssignASTNode : public GenericASTNode {
    string varName;
    unique_ptr<GenericASTNode> value;

public:
    VariableAssignASTNode(const string &varName, unique_ptr<GenericASTNode> value)
        : varName(varName), value(std::move(value)) {}

    void toString() override {
        printf("Variable Assign: %s = ", varName.c_str());
        value->toString();
    }

    Value *codegen() override {
        Value *Val = value->codegen();
        if (!Val) return nullptr;

        Value *V = TheModule->getNamedGlobal(varName);
        if (!V) {
            fprintf(stderr, "Error: Unknown variable %s\n", varName.c_str());
            return nullptr;
        }

        return Builder->CreateStore(Val, V);
    }
};

class BinaryExprAST : public GenericASTNode
{
    char Op;
    unique_ptr<GenericASTNode> LHS, RHS;
 
public:
    BinaryExprAST(char Op, unique_ptr<GenericASTNode> LHS, unique_ptr<GenericASTNode> RHS)
    {
        this->Op = Op;
        this->LHS = std::move(LHS);
        this->RHS = std::move(RHS);
    }
 
    void toString() {
        printf("BinaryExpr: %c\n", this->Op);
        printf("LHS: ");
        this->LHS->toString();
        printf("\nRHS: ");
        this->RHS->toString();
        printf("\n");
    }
   
    Value* codegen() {
        Value *Left = LHS->codegen();
        Value *Right = RHS->codegen();
        if (!Left || !Right) {
            return nullptr;
        }
 
        switch (Op) {
            case '+':
                return Builder->CreateAdd(Left, Right, "addtmp");
            case '-':
                return Builder->CreateSub(Left, Right, "subtmp");
            case '*':
                return Builder->CreateMul(Left, Right, "multmp");
            case '/':
                return Builder->CreateSDiv(Left, Right, "divtmp");
            case '%':
                return Builder->CreateSRem(Left, Right, "modtmp");
            default:
                fprintf(stderr, "Invalid binary operator %c\n", Op);
                return nullptr;
        }
    }
};

class IfStatementAST : public GenericASTNode {
    unique_ptr<GenericASTNode> Cond, TrueExpr, FalseExpr;

public:
    IfStatementAST(
        unique_ptr<GenericASTNode> Cond,
        unique_ptr<GenericASTNode> TrueExpr,
        unique_ptr<GenericASTNode> FalseExpr
    )
    {
        this->Cond = std::move(Cond);
        this->TrueExpr = std::move(TrueExpr);
        this->FalseExpr = std::move(FalseExpr);
        
        if (this->FalseExpr == nullptr){
            this->FalseExpr = std::make_unique<NumberASTNode>(0);
        }
    }

    void toString() override {
        printf("If Statement:\n");
        printf("Condition: ");
        Cond->toString();
        printf("\nTrue Branch: ");
        TrueExpr->toString();
        if (FalseExpr) {
            printf("\nFalse Branch: ");
            FalseExpr->toString();
        }
    }

    Value *codegen() override {
        Value *CondV = Cond->codegen();
        if (!CondV) return nullptr;

        Function *TheFunction = Builder->GetInsertBlock()->getParent();

        BasicBlock *ThenBB = BasicBlock::Create(*TheContext, "then");
        BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else");
        BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "merge");

        CondV = Builder->CreateICmpNE(CondV, ConstantInt::get(*TheContext, APInt(32, 0, true)), "ifcond");
        Builder->CreateCondBr(CondV, ThenBB, ElseBB);

        TheFunction->insert(TheFunction->end(), ThenBB);
        Builder->SetInsertPoint(ThenBB);
        Value *ThenV = TrueExpr->codegen();
        if (!ThenV) return nullptr;
        Builder->CreateBr(MergeBB);
        ThenBB = Builder->GetInsertBlock();

        TheFunction->insert(TheFunction->end(), ElseBB);
        Builder->SetInsertPoint(ElseBB);
        Value *ElseV = FalseExpr ? FalseExpr->codegen() : nullptr;
        Builder->CreateBr(MergeBB);
        ElseBB = Builder->GetInsertBlock();

        TheFunction->insert(TheFunction->end(), MergeBB);
        Builder->SetInsertPoint(MergeBB);

        PHINode *PN = Builder->CreatePHI(Type::getInt32Ty(*TheContext), 2, "iftmp");
        PN->addIncoming(ThenV, ThenBB);
        if (FalseExpr) PN->addIncoming(ElseV, ElseBB);

        return PN;
    }
};


void CodeGenTopLevel(unique_ptr<GenericASTNode> AST_Root)
{
    vector<Type *> ArgumentsTypes(0);
    FunctionType *FT = FunctionType::get(Type::getInt32Ty(*TheContext), ArgumentsTypes, false);
    Function *F = Function::Create(FT, Function::ExternalLinkage, "main", TheModule.get());

    BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", F);
    Builder->SetInsertPoint(BB);

    if (Value *RetVal = AST_Root->codegen()) {
        Builder->CreateRet(RetVal);
    }

    auto Filename = "output.ll";
    std::error_code EC;
    raw_fd_ostream dest(Filename, EC);

    if (EC) {
        errs() << "Could not open file: " << EC.message();
        return;
    }

    F->print(errs());
    F->print(dest);
    F->eraseFromParent();
}

class WhileStatementAST : public GenericASTNode {
    unique_ptr<GenericASTNode> Cond;
    unique_ptr<GenericASTNode> Body;

public:
    WhileStatementAST(unique_ptr<GenericASTNode> Cond, unique_ptr<GenericASTNode> Body)
        : Cond(std::move(Cond)), Body(std::move(Body)) {}

    void toString() override {
        printf("While Statement:\n");
        printf("Condition: ");
        Cond->toString();
        printf("\nBody: ");
        Body->toString();
    }

    Value *codegen() override {
        Value *CondV = Cond->codegen();
        if (!CondV) return nullptr;

        CondV = Builder->CreateICmpNE(
            CondV,
            ConstantInt::get(*TheContext, APInt(32, 0, true)),
            "ifcond"
        );

        Function *TheFunction = Builder->GetInsertBlock()->getParent();
        BasicBlock *BodyBB = BasicBlock::Create(*TheContext, "while.body", TheFunction);
        BasicBlock *EndBB = BasicBlock::Create(*TheContext, "while.end", TheFunction);

        Builder->CreateCondBr(CondV, BodyBB, EndBB);

        Builder->SetInsertPoint(BodyBB);
        if (!Body->codegen()) return nullptr;
        Builder->CreateBr(EndBB);

        Builder->SetInsertPoint(EndBB);

        return ConstantInt::get(Type::getInt32Ty(*TheContext), 0);
    }

};


//===----------------------------------------------------------------------===//
// Parser
//===----------------------------------------------------------------------===//
int yylex();
int symbol;
extern int yylval;

unique_ptr<GenericASTNode> Z();
unique_ptr<GenericASTNode> E_AS();  
unique_ptr<GenericASTNode> E_MDR();
unique_ptr<GenericASTNode> E_IF();
unique_ptr<GenericASTNode> E_WHILE();
unique_ptr<GenericASTNode> T();
unique_ptr<GenericASTNode> VAR_DECL();
unique_ptr<GenericASTNode> VAR_ASSIGN();

unique_ptr<GenericASTNode> Statements();
unique_ptr<GenericASTNode> Statement();

void next_symbol()
{
    symbol = yylex();
}

void err_n_die(const char* const fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    exit(1);
}

unique_ptr<GenericASTNode> Z(){

    if(symbol == IF){
        return E_IF();
    }
    if(symbol == WHILE){
        return E_WHILE();
    }

    return E_AS();
}

unique_ptr<GenericASTNode> E_IF() {
    if (symbol != IF) err_n_die("Error: Expected 'if'.\n");
    next_symbol();

    if (symbol != '(') err_n_die("Error: Expected '('.\n");
    next_symbol();
    auto Cond = E_AS();
    if (symbol != ')') err_n_die("Error: Expected ')'.\n");
    next_symbol();

    if (symbol != '{') err_n_die("Error: Expected '{' for true branch.\n");
    next_symbol();
    auto TrueExpr = E_AS();
    if (symbol != '}') err_n_die("Error: Expected '}' for true branch.\n");
    next_symbol();

    unique_ptr<GenericASTNode> FalseExpr = nullptr;
    if (symbol == ELSE) {
        next_symbol();
        if (symbol != '{') err_n_die("Error: Expected '{' for false branch.\n");
        next_symbol();
        FalseExpr = E_AS();
        if (symbol != '}') err_n_die("Error: Expected '}' for false branch.\n");
        next_symbol();
    }

    return make_unique<IfStatementAST>(std::move(Cond), std::move(TrueExpr), std::move(FalseExpr));
}


unique_ptr<GenericASTNode> E_AS() {
    auto acc = E_MDR();
    while (symbol == '+' || symbol == '-') {
        char op = symbol;
        next_symbol();
        auto acc1 = E_MDR();
        acc = make_unique<BinaryExprAST>(op, std::move(acc), std::move(acc1));
    }

    return acc;
}

unique_ptr<GenericASTNode> E_MDR() {
    auto acc = T();
    while (symbol == '*' || symbol == '/' || symbol == '%') {
        char op = symbol;
        next_symbol();
        auto rhs = T();
        acc = make_unique<BinaryExprAST>(op, std::move(acc), std::move(rhs));
    }
    return acc;
}

unique_ptr<GenericASTNode> T() {
    if (symbol == '(') {
        next_symbol();
        auto acc = E_AS();
        if (symbol == ')') {
            next_symbol();
            return acc;
        } else {
            err_n_die("Error: Expected closing parenthesis\n");
        }
    } else if (symbol == NUMBER) {
        auto numNode = make_unique<NumberASTNode>(yylval);
        next_symbol();
        return numNode;
    } else {
        err_n_die("Error: Unexpected token\n");
    }
    return nullptr;
}

unique_ptr<GenericASTNode> Statement() {
    unique_ptr<GenericASTNode> node;
    if (symbol == NUMBER) {
        node = E_AS();
    } else if (symbol == IF) {
        node = E_IF();
    } else if (symbol == WHILE) {
        node = E_WHILE();
    } else {
        err_n_die("%d %c Error: Unexpected token in statement\n", symbol, symbol);
    }
    return make_unique<StatementASTNode>(std::move(node));
}


unique_ptr<GenericASTNode> Statements() {
    unique_ptr<GenericASTNode> head = Statement();
    GenericASTNode* current = head.get();

    while (symbol == ';') {
        next_symbol();
        auto newNode = Statement();
        newNode->toString();
        auto stmtNode = dynamic_cast<StatementASTNode*>(current);
        if (!stmtNode) {
            err_n_die("Invalid cast to StatementASTNode.\n");
        }
        stmtNode->setNextNode(std::move(newNode));
        current = stmtNode->getNextNode();
    }

    return head ? std::move(head) : std::make_unique<NumberASTNode>(0);
}


unique_ptr<GenericASTNode> E_WHILE() {
    if (symbol != WHILE) err_n_die("Error: Expected 'while'.\n");
    next_symbol();

    if (symbol != '(') err_n_die("Error: Expected '('.\n");
    next_symbol();
    auto Cond = E_AS();
    if (symbol != ')') err_n_die("Error: Expected ')'.\n");
    next_symbol();

    if (symbol != '{') err_n_die("Error: Expected '{' for while body.\n");
    next_symbol();
    auto Body = Statements(); 
    if (symbol != '}') err_n_die("Error: Expected '}' for while body.\n");
    next_symbol();

    return make_unique<WhileStatementAST>(std::move(Cond), std::move(Body));
}


//===----------------------------------------------------------------------===//
// main function
//===----------------------------------------------------------------------===//
int main()
{
    InitializeModule();

    next_symbol();
    CodeGenTopLevel(Z());

    return 0;
}