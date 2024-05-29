#include <iostream>
#include <sstream>
#include <set>

#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Dump/RecordLayoutDump.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecordLayout.h"
#include "clang/tinyxml.h"

namespace clang {
const Type* GetDependantType(const Type *t) {
  /*if (t->isPointerType()) {
    return GetDependantType(t->getAs<PointerType>()->getPointeeType().getTypePtr());
  } else if (t->isReferenceType()) {
    return GetDependantType(t->getAs<ReferenceType>()->getPointeeType().getTypePtr());
  } else */if (t->isArrayType()) {
    return GetDependantType(t->getAsArrayTypeUnsafe()->getElementType().getTypePtr());
  }
  return t;
}

RecordDumpConsumer::RecordDumpConsumer(CompilerInstance &CI, StringRef InFile)
  :ci_(CI), source_file_(InFile) {
    ctx_ = NULL;
}

RecordDumpConsumer::~RecordDumpConsumer() {
}

void RecordDumpConsumer::Dump() {
  const SmallVectorImpl<Type *> &types = ctx_->getTypes();
  std::set<const Type*> record_types;
  TiXmlDocument doc;
  doc.LinkEndChild(new TiXmlDeclaration("1.0", "utf-8", "" ));

  TiXmlElement *tu_elem = new TiXmlElement("TranslationUnit");
  tu_elem->SetAttribute("SourceLocation", source_file_);

  for (SmallVectorImpl<Type *>::const_iterator it = types.begin(); it != types.end(); ++it) {
    const Type *t = *it;
    // 将全部的 ElaboratedType 全部加进来, 大约会提升80%的占用, 后续再看怎么优化
    bool isElaboratedType = false;
    if (t->isElaboratedTypeSpecifier()) {
      QualType qualType = dyn_cast<ElaboratedType>(t)->getNamedType();
      t =  qualType.getTypePtrOrNull();
      if (!t) {
        continue;
      }
      isElaboratedType = true;
    }
    const RecordType *record = NULL;
    if (!t->isRecordType() || ((record = dyn_cast<const RecordType>(t)) == NULL) ||
        t->isDependentType()) {
      continue;
    }
    RecordDecl *decl = record->getDecl();
    if (decl->isInAnonymousNamespace() || !decl->isDefinedOutsideFunctionOrMethod()) {
      continue;
    }
    if (record_types.find(record) != record_types.end()) {
      continue;
    }
    if (!decl->isReferenced() && !isElaboratedType) {
      // 解决 this 指针引用，不在类内定义的函数的类则加进来
      bool methodRef = false;
      auto cxxDecl = dyn_cast<CXXRecordDecl>(decl);
      if (cxxDecl) {
        for (auto iter = cxxDecl->method_begin(); iter != cxxDecl->method_end(); iter++) {
          auto method = *iter;
          if (method->hasTrivialBody() && method->isDefined() && !method->doesThisDeclarationHaveABody()) {
            methodRef = true;
            break;
          }
        }
      }
      if (!methodRef) {
        continue;
      }
    }
    record_types.insert(record);

    // std::cout << "Record: " << decl  << ", " << QualType(t, 0).getAsString() << std::endl;
    DumpRecordToXml(decl, tu_elem);
    // QualType tp(t, 0);
    // std::cout << tp.getAsString() << std::endl;
  }

  doc.LinkEndChild(tu_elem);
  doc.SaveFile(ci_.getFrontendOpts().OutputFile);
}

void RecordDumpConsumer::HandleTranslationUnit(ASTContext &ctx) {
  ctx_ = &ctx;
  Dump();
  return;

  TiXmlDocument doc;
  doc.LinkEndChild(new TiXmlDeclaration("1.0", "utf-8", "" ));

  TiXmlElement *tu_elem = new TiXmlElement("TranslationUnit");

  TranslationUnitDecl *tu = ctx.getTranslationUnitDecl();
  tu_elem->SetAttribute("SourceLocation", source_file_);

  for (DeclContext::decl_iterator first = tu->decls_begin();
       first != tu->decls_end(); ++first) {
    DumpDeclToXml(*first, tu_elem);
  }

  doc.LinkEndChild(tu_elem);

  doc.SaveFile(ci_.getFrontendOpts().OutputFile);
}

void RecordDumpConsumer::DumpDeclToXml(Decl *decl, TiXmlElement *e) {
  switch (decl->getKind()) {
    case Decl::Namespace:
      DumpNamespaceToXml(dyn_cast<NamespaceDecl>(decl), e);
      break;
    case Decl::Record:
    case Decl::CXXRecord:
      DumpRecordToXml(dyn_cast<RecordDecl>(decl), e);
      break;
    case Decl::Function:
      DumpFunctionToXml(dyn_cast<FunctionDecl>(decl), e);
      break;
    default: 
      // std::cout << decl->getDeclKindName() << std::endl;
      break;
  }
}

void RecordDumpConsumer::DumpNamespaceToXml(NamespaceDecl *decl,
                                            TiXmlElement *parent) {
  // TiXmlElement *e = new TiXmlElement("namespace");
  // std::string name = decl->getName();
  // e->SetAttribute("Name", name == "" ? "<anonymous>" : name);

  for (DeclContext::decl_iterator first = decl->decls_begin();
       first != decl->decls_end(); ++first) {
    DumpDeclToXml(*first, parent);
  }

  // parent->LinkEndChild(e);
}

static std::string GetCxxRecordString(QualType type, bool isCxxRecord) {
  if (isCxxRecord) {
    return type.getAsString();
  }
  
  return type.getAsCxxString();
}

void RecordDumpConsumer::DumpRecordToXml(RecordDecl *decl,
                                         TiXmlElement *parent) {
  if (record_decls_.find(decl) != record_decls_.end()) {
    return;
  }

  record_decls_.insert(decl);

  if (!decl->isCompleteDefinition()) {
    return;
  }

  // FIX: check before calling getASTRecordLayout to avoid assert-failure or segfault
  if (decl->hasExternalLexicalStorage() && !decl->getDefinition())
    ctx_->getExternalSource()->CompleteType(const_cast<RecordDecl*>(decl));
  if (decl->isInvalidDecl() || decl->getDefinition() == nullptr ||
      decl->getDefinition()->isInvalidDecl() ||
      !decl->getDefinition()->isCompleteDefinition()) {
    return;
  }

  // FIX: template specialization in a class cause segfault
  if (dyn_cast<CXXRecordDecl>(decl) != nullptr) {
    clang::CXXRecordDecl *Class = dyn_cast<CXXRecordDecl>(decl);
    for (const CXXBaseSpecifier &Base : Class->bases()) {
      if (Base.getType()->getAsCXXRecordDecl() == nullptr) {
        return;
      }
    }
  }
//   std::string q;
// 
//   for (DeclContext *pp = decl->getLexicalParent(); pp != NULL;
//        pp = pp->getLexicalParent()) {
//     if (pp->isTranslationUnit()) {
//       break;
//     }
//     TagDecl *d = dyn_cast<TagDecl>(pp);
//     
//     std::string s = d->getIdentifier()->getName();
//     q = s + std::string("::") + q;
//   }
//   
//   q += decl->getIdentifier()->getName();
//   std::cout << "the full qualified: " << q << std::endl;

  bool isCxxRecord = dyn_cast<CXXRecordDecl>(decl) != NULL;
  
  const ASTRecordLayout &layout = ctx_->getASTRecordLayout(decl);
  TiXmlElement *e = new TiXmlElement("Record");
  // e->SetAttribute("Name", decl->getName());
  e->SetAttribute("QualifiedName", GetCxxRecordString(ctx_->getRecordType(decl), isCxxRecord));
  e->SetAttribute("Size", static_cast<int>(layout.getSize().getQuantity()));
  e->SetAttribute("Alignment", static_cast<int>(layout.getAlignment().getQuantity()));
  e->SetAttribute("IsCxxClass", dyn_cast<CXXRecordDecl>(decl) ? "true" : "false");

  // Check if it is an anonymous record.
  bool isAnonymous = false;
  if (!decl->getIdentifier() && !decl->getTypedefNameForAnonDecl()) {
    isAnonymous = true;
  }
  e->SetAttribute("IsAnonymousRecord", isAnonymous);

  // e->SetAttribute("IsInAnonymousNamespace", decl->isInAnonymousNamespace() ? "true" : "false");

  // dump out the include hierachy.
  const SourceManager &src_mgr = ctx_->getSourceManager();
  SourceLocation loc = decl->getLocation();
  std::ostringstream sout;
  bool is_first = true;
  while (true) {
    PresumedLoc source_line = src_mgr.getPresumedLoc(loc);
    if (source_line.isInvalid()) {
      break;
    }
    if (!is_first) {
      sout << ":";
    }
    is_first = false;
    sout << source_line.getFilename() << ":" << source_line.getLine();
    loc = source_line.getIncludeLoc();
  }
  e->SetAttribute("IncludeDir", sout.str());

  const CXXRecordDecl *cxx_record = dyn_cast<CXXRecordDecl>(decl);
  if (cxx_record) {
    e->SetAttribute("NonVirtualSize", static_cast<int>(layout.getNonVirtualSize().getQuantity()));
    e->SetAttribute("NonVirtualAlignment", static_cast<int>(layout.getNonVirtualAlignment().getQuantity()));

    // Dump out all the bases
    for (CXXRecordDecl::base_class_const_iterator first = cxx_record->bases_begin();
         first != cxx_record->bases_end(); ++first) {
      const CXXBaseSpecifier *base = first;
      const CXXRecordDecl *base_type = base->getType()->getAsCXXRecordDecl();
      TiXmlElement *p = new TiXmlElement("base");
      p->SetAttribute("Name", base->getType().getCanonicalType().getAsString());
      p->SetAttribute("IsVirtual", base->isVirtual() ? "true" : "false");
      CharUnits size = base->isVirtual() ? layout.getVBaseClassOffset(base_type) :
          layout.getBaseClassOffset(base_type);
      p->SetAttribute("Offset", static_cast<int>(size.getQuantity()));
      e->LinkEndChild(p);
    }
  }

  // Dump out all the vtable.

  // Dump out all the records declared in the record.
  typedef CXXRecordDecl::specific_decl_iterator<RecordDecl> SubRecordIter;

  SubRecordIter first(decl->decls_begin()), last(decl->decls_end());

  for (; first != last; ++first) {
    DumpRecordToXml(*first, parent);
  }

  // Dump out all the fields in bits.
  for (RecordDecl::field_iterator first = decl->field_begin();
       first != decl->field_end(); ++first) {
    FieldDecl *field = *first;
    TiXmlElement *p = new TiXmlElement("field");
    p->SetAttribute("Name", field->getNameAsString());
    QualType type = field->getType().getCanonicalType();
    p->SetAttribute("Type", GetCxxRecordString(type, isCxxRecord));
    const Type *dependant_type = GetDependantType(type.getTypePtr());
    if (dependant_type->isRecordType()) {
      p->SetAttribute("DependantType", GetCxxRecordString(QualType(dependant_type, 0), isCxxRecord));
    }
    p->SetAttribute("OffsetInBits", static_cast<int>(layout.getFieldOffset(field->getFieldIndex())));

    clang::TypeInfo field_info = ctx_->getTypeInfo(field->getType());
    uint64_t type_size = field_info.Width;

    if (field->isBitField()) {
      type_size = field->getBitWidthValue(*ctx_);
    }
    p->SetAttribute("SizeInBits", type_size);
    e->LinkEndChild(p);
  }

  parent->LinkEndChild(e);
}

void RecordDumpConsumer::DumpFunctionToXml(FunctionDecl *decl,
                                           TiXmlElement *parent) {
}

RecordDumpAction::RecordDumpAction() {
}

RecordDumpAction::~RecordDumpAction() {
}

std::unique_ptr<ASTConsumer> RecordDumpAction::CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) {
  return std::unique_ptr<ASTConsumer>(new RecordDumpConsumer(CI, InFile));
}
}

