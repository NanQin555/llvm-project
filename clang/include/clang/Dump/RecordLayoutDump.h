#pragma once

#include <fstream>
#include <string>
#include <unordered_set>

#include "llvm/ADT/StringRef.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"

class TiXmlElement;
namespace clang {
  class RecordDumpAction;
  class RecordDumpConsumer : public ASTConsumer {
  public:
    RecordDumpConsumer(CompilerInstance &CI, StringRef InFile);
    virtual ~RecordDumpConsumer();

    virtual void HandleTranslationUnit(ASTContext &Ctx);
  private:
    void Dump();
    void DumpDeclToXml(Decl *decl, TiXmlElement *parent);
    void DumpNamespaceToXml(NamespaceDecl *decl, TiXmlElement *parent);
    void DumpRecordToXml(RecordDecl *decl, TiXmlElement *parent);
    void DumpFunctionToXml(FunctionDecl *decl, TiXmlElement *parent);
  private:
    ASTContext *ctx_;
    CompilerInstance &ci_;
    std::string source_file_;
    std::unordered_set<RecordDecl *> record_decls_;
  };

  class RecordDumpAction : public ASTFrontendAction {
  public:
    RecordDumpAction();
    virtual ~RecordDumpAction();

    virtual std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                         StringRef InFile);
  };
}

