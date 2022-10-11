//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//

#include <cstdio>
#include <unistd.h>
using namespace std;

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

#include "Environment.h"

class InterpreterVisitor :
        public EvaluatedExprVisitor<InterpreterVisitor> {
public:
    explicit InterpreterVisitor(const ASTContext &context, Environment *env)
            : EvaluatedExprVisitor(context), mEnv(env) {}

    virtual ~InterpreterVisitor() {}

    virtual void VisitBinaryOperator(BinaryOperator *bop) {
        VisitStmt(bop);
        mEnv->binop(bop);
    }

    virtual void VisitDeclRefExpr(DeclRefExpr *expr) {
        VisitStmt(expr);
        mEnv->declref(expr);
    }

    virtual void VisitCastExpr(CastExpr *expr) {
        VisitStmt(expr);
        mEnv->cast(expr);
    }

    virtual void VisitCallExpr(CallExpr *call) {
        VisitStmt(call);
        mEnv->call(call);
    }

    virtual void VisitDeclStmt(DeclStmt *declstmt) {
        mEnv->decl(declstmt);
    }

private:
    Environment *mEnv;
};

class InterpreterConsumer : public ASTConsumer {
public:
    explicit InterpreterConsumer(const ASTContext &context) : mEnv(),
                                                              mVisitor(context, &mEnv) {
    }

    virtual ~InterpreterConsumer() {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
        TranslationUnitDecl *decl = Context.getTranslationUnitDecl();
        mEnv.init(decl);

        FunctionDecl *entry = mEnv.getEntry();
        mVisitor.VisitStmt(entry->getBody());
    }

private:
    Environment mEnv;
    InterpreterVisitor mVisitor;
};

class InterpreterClassAction : public ASTFrontendAction {
public:
    virtual std::unique_ptr <clang::ASTConsumer> CreateASTConsumer(
            clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
        return std::unique_ptr<clang::ASTConsumer>(
                new InterpreterConsumer(Compiler.getASTContext()));
    }
};

int main(int argc, char **argv) {
    if (argc > 1) {
#ifdef ASSIGNMENT_DEBUG
        printf("Warning: ASSIGNMENT_DEBUG_MODE_ON. It's intended for student debug purpose. "
               "If you are a teacher evaluating this assignment, please remove the macro ASSIGNMENT_DEBUG "
               "to restore program's original functionality.\n");
        FILE* fp = fopen(argv[1], "r");
        if (fp == NULL) {
            perror("Unable to open source");
            return 1;
        }
        fseek(fp, 0, SEEK_END);
        long fileSize = ftell(fp);
        char* source = static_cast<char *>(calloc(fileSize + 1, sizeof(char)));
        fseek(fp, 0, SEEK_SET);
        fread(source, sizeof(char), fileSize, fp);
        fclose(fp);
        clang::tooling::runToolOnCode(std::unique_ptr<clang::FrontendAction>(new InterpreterClassAction), source);
        free(source);
#else
        clang::tooling::runToolOnCode(std::unique_ptr<clang::FrontendAction>(new InterpreterClassAction), argv[1]);
#endif
    }

}
