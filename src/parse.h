//
// Created by kaiser on 2019/10/31.
//

#ifndef KCC_SRC_PARSE_H_
#define KCC_SRC_PARSE_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <stack>
#include <string>
#include <utility>
#include <vector>

#include <llvm/IR/Constants.h>

#include "ast.h"
#include "location.h"
#include "scope.h"
#include "token.h"
#include "type.h"

namespace kcc {

class Parser {
 public:
  explicit Parser(std::vector<Token> tokens);
  TranslationUnit* ParseTranslationUnit();

 private:
  template <typename T, typename... Args>
  T* MakeAstNode(Args&&... args);

  bool HasNext();
  Token Peek();
  Token Next();
  void PutBack();
  bool Test(Tag tag);
  bool Try(Tag tag);
  Token Expect(Tag tag);
  void MarkLoc();

  void EnterBlock(Type* func_type = nullptr);
  void ExitBlock();
  void EnterFunc(IdentifierExpr* ident);
  void ExitFunc();
  void EnterProto();
  void ExitProto();
  bool IsTypeName(const Token& tok);
  bool IsDecl(const Token& tok);
  bool InGlobal() const;
  Declaration* MakeDeclaration(const std::string& name, QualType type,
                               std::uint32_t storage_class_spec,
                               std::uint32_t func_spec, std::int32_t align);

  /*
   * ExtDecl
   */
  ExtDecl* ParseExternalDecl();
  FuncDef* ParseFuncDef(const Declaration* decl);

  /*
   * Expr
   */
  Expr* ParseExpr();
  Expr* ParseAssignExpr();
  Expr* ParseConditionExpr();
  Expr* ParseLogicalOrExpr();
  Expr* ParseLogicalAndExpr();
  Expr* ParseInclusiveOrExpr();
  Expr* ParseExclusiveOrExpr();
  Expr* ParseAndExpr();
  Expr* ParseEqualityExpr();
  Expr* ParseRelationExpr();
  Expr* ParseShiftExpr();
  Expr* ParseAdditiveExpr();
  Expr* ParseMultiplicativeExpr();
  Expr* ParseCastExpr();
  Expr* ParseUnaryExpr();
  Expr* ParseSizeof();
  Expr* ParseAlignof();
  Expr* ParsePostfixExpr();
  Expr* TryParseCompoundLiteral();
  Expr* ParseCompoundLiteral(QualType type);
  Expr* ParsePostfixExprTail(Expr* expr);
  Expr* ParseIndexExpr(Expr* expr);
  Expr* ParseFuncCallExpr(Expr* expr);
  Expr* ParseMemberRefExpr(Expr* expr);
  Expr* ParsePrimaryExpr();
  Expr* ParseConstant();
  Expr* ParseCharacter();
  Expr* ParseInteger();
  Expr* ParseFloat();
  StringLiteralExpr* ParseStringLiteral();
  Expr* ParseGenericSelection();
  Expr* ParseConstantExpr();

  /*
   * Stmt
   */
  Stmt* ParseStmt();
  Stmt* ParseLabelStmt();
  Stmt* ParseCaseStmt();
  Stmt* ParseDefaultStmt();
  CompoundStmt* ParseCompoundStmt(Type* func_type = nullptr);
  Stmt* ParseExprStmt();
  Stmt* ParseIfStmt();
  Stmt* ParseSwitchStmt();
  Stmt* ParseWhileStmt();
  Stmt* ParseDoWhileStmt();
  Stmt* ParseForStmt();
  Stmt* ParseGotoStmt();
  Stmt* ParseContinueStmt();
  Stmt* ParseBreakStmt();
  Stmt* ParseReturnStmt();

  /*
   * Decl
   */
  CompoundStmt* ParseDecl(bool maybe_func_def = false);
  void ParseStaticAssertDecl();

  /*
   * Decl Spec
   */
  QualType ParseDeclSpec(std::uint32_t* storage_class_spec,
                         std::uint32_t* func_spec, std::int32_t* align);
  Type* ParseStructUnionSpec(bool is_struct);
  void ParseStructDeclList(StructType* type);
  Type* ParseEnumSpec();
  void ParseEnumerator();
  std::int32_t ParseAlignas();

  /*
   * Declarator
   */
  CompoundStmt* ParseInitDeclaratorList(QualType& base_type,
                                        std::uint32_t storage_class_spec,
                                        std::uint32_t func_spec,
                                        std::int32_t align);
  Declaration* ParseInitDeclarator(QualType& base_type,
                                   std::uint32_t storage_class_spec,
                                   std::uint32_t func_spec, std::int32_t align);
  void ParseDeclarator(Token& tok, QualType& base_type);
  void ParsePointer(QualType& type);
  std::uint32_t ParseTypeQualList();
  void ParseDirectDeclarator(Token& tok, QualType& base_type);
  void ParseDirectDeclaratorTail(QualType& base_type);
  std::size_t ParseArrayLength();
  std::pair<std::vector<ObjectExpr*>, bool> ParseParamTypeList();
  ObjectExpr* ParseParamDecl();

  /*
   * type name
   */
  QualType ParseTypeName();
  void ParseAbstractDeclarator(QualType& type);
  void ParseDirectAbstractDeclarator(QualType& type);

  /*
   * Init
   */
  void ParseInitDeclaratorSub(Declaration* decl);
  llvm::Constant* ParseInitializer(std::vector<Initializer>& inits,
                                   QualType type, bool designated,
                                   bool force_brace);
  void ParseArrayInitializer(std::vector<Initializer>& inits, Type* type,
                             std::int32_t designated);
  llvm::Constant* ParseLiteralInitializer(Type* type);
  void ParseStructInitializer(std::vector<Initializer>& inits, Type* type,
                              bool designated);
  static auto ParseStructDesignator(Type* type, const std::string& name)
      -> decltype(std::begin(type->StructGetMembers()));

  /*
   * ConstantInit
   */
  llvm::Constant* ParseConstantInitializer(QualType type, bool designated,
                                           bool force_brace);
  llvm::Constant* ParseConstantArrayInitializer(Type* type,
                                                std::int32_t designated);
  llvm::Constant* ParseConstantLiteralInitializer(Type* type);
  llvm::Constant* ParseConstantStructInitializer(Type* type, bool designated);

  /*
   * GNU 扩展
   */
  void TryParseAttributeSpec();
  void ParseAttributeList();
  void ParseAttribute();
  void ParseAttributeParamList();
  void ParseAttributeExprList();
  void TryParseAsm();
  QualType ParseTypeof();
  Expr* ParseStmtExpr();
  Expr* ParseTypeid();

  /*
   * built in
   */
  Expr* ParseOffsetof();
  void AddBuiltin();

  LabelStmt* FindLabel(const std::string& name) const;

  TranslationUnit* unit_;

  std::vector<Token> tokens_;
  decltype(tokens_)::size_type index_{};

  FuncDef* func_def_{};
  Scope* scope_{Scope::Get(nullptr, kFile)};

  std::unordered_map<std::string, LabelStmt*> labels_;
  std::vector<GotoStmt*> gotos_;

  // 用于将块作用与的复合字面量加入块中
  std::stack<CompoundStmt*> compound_stmt_;

  // 非常量初始化时记录索引
  std::list<std::pair<Type*, std::int32_t>> indexs_;

  Location loc_;
};

template <typename T, typename... Args>
T* Parser::MakeAstNode(Args&&... args) {
  auto t{T::Get(std::forward<Args>(args)...)};
  t->SetLoc(loc_);
  t->Check();
  return t;
}

}  // namespace kcc

#endif  // KCC_SRC_PARSE_H_
