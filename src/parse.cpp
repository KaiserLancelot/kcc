//
// Created by kaiser on 2019/10/31.
//

#include "parse.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "calc.h"
#include "encoding.h"
#include "error.h"
#include "lex.h"
#include "llvm_common.h"

namespace kcc {

Parser::Parser(std::vector<Token> tokens) : tokens_{std::move(tokens)} {
  unit_ = MakeAstNode<TranslationUnit>(Peek());
  AddBuiltin();
}

TranslationUnit* Parser::ParseTranslationUnit() {
  while (HasNext()) {
    unit_->AddExtDecl(ParseExternalDecl());
  }

  return unit_;
}

bool Parser::HasNext() { return !Peek().TagIs(Tag::kEof); }

const Token& Parser::Peek() { return tokens_[index_]; }

const Token& Parser::Next() { return tokens_[index_++]; }

void Parser::PutBack() {
  assert(index_ > 0);
  --index_;
}

bool Parser::Test(Tag tag) { return Peek().TagIs(tag); }

bool Parser::Try(Tag tag) {
  if (Test(tag)) {
    Next();
    return true;
  } else {
    return false;
  }
}

const Token& Parser::Expect(Tag tag) {
  if (!Test(tag)) {
    Error(tag, Peek());
  } else {
    return Next();
  }
}

void Parser::EnterBlock(Type* func_type) {
  scope_ = Scope::Get(scope_, kBlock);

  if (func_type) {
    for (const auto& param : func_type->FuncGetParams()) {
      scope_->InsertUsual(param);
    }
  }
}

void Parser::ExitBlock() { scope_ = scope_->GetParent(); }

void Parser::EnterFunc(IdentifierExpr* ident) {
  func_def_ = MakeAstNode<FuncDef>(ident->GetLoc(), ident);
}

void Parser::ExitFunc() { func_def_ = nullptr; }

void Parser::EnterProto() { scope_ = Scope::Get(scope_, kFuncProto); }

void Parser::ExitProto() { scope_ = scope_->GetParent(); }

bool Parser::IsTypeName(const Token& tok) {
  if (tok.IsTypeSpecQual()) {
    return true;
  } else if (tok.IsIdentifier()) {
    auto ident{scope_->FindUsual(tok)};
    if (ident && ident->IsTypeName()) {
      return true;
    }
  }

  return false;
}

bool Parser::IsDecl(const Token& tok) {
  if (tok.IsDeclSpec()) {
    return true;
  } else if (tok.IsIdentifier()) {
    auto ident{scope_->FindUsual(tok)};
    if (ident && ident->IsTypeName()) {
      return true;
    }
  }

  return false;
}

std::int64_t Parser::ParseInt64Constant() {
  auto expr{ParseExpr()};
  if (!expr->GetType()->IsIntegerTy()) {
    Error(expr, "expect integer");
  }

  auto val{CalcConstantExpr{}.CalcInteger(expr)};

  return val;
}

LabelStmt* Parser::FindLabel(const std::string& name) const {
  if (auto iter{labels_.find(name)}; iter != std::end(labels_)) {
    return iter->second;
  } else {
    return nullptr;
  }
}

auto Parser::GetStructDesignator(Type* type, const std::string& name)
    -> decltype(std::begin(type->StructGetMembers())) {
  auto iter{std::begin(type->StructGetMembers())};

  for (; iter != std::end(type->StructGetMembers()); ++iter) {
    if ((*iter)->IsAnonymous()) {
      auto anonymous_type{(*iter)->GetType()};
      if (anonymous_type->StructGetMember(name)) {
        return iter;
      }
    } else if ((*iter)->GetName() == name) {
      return iter;
    }
  }

  assert(false);
  return iter;
}

Declaration* Parser::MakeDeclaration(const Token& token, QualType type,
                                     std::uint32_t storage_class_spec,
                                     std::uint32_t func_spec,
                                     std::int32_t align) {
  auto name{token.GetIdentifier()};

  if (storage_class_spec & kTypedef) {
    if (align > 0) {
      Error(token, "'_Alignas' attribute applies to typedef");
    }

    auto ident{scope_->FindUsualInCurrScope(name)};
    if (ident) {
      // 如果两次定义的类型兼容是可以的
      if (!type->Compatible(ident->GetType())) {
        Error(token, "typedef redefinition with different types '{}' vs '{}'",
              type->ToString(), ident->GetType()->ToString());
      } else {
        Warning(token, "Typedef redefinition");
        return nullptr;
      }
    } else {
      scope_->InsertUsual(
          name, MakeAstNode<IdentifierExpr>(token, name, type, kNone, true));

      // 如果没有名字, 将 typedef 的名字给该 struct / union
      if (type->IsStructOrUnionTy() && !type->StructHasName()) {
        type->StructSetName(name);
      }

      return nullptr;
    }
  } else if (storage_class_spec & kRegister) {
    if (align > 0) {
      Error(token, "'_Alignas' attribute applies to register");
    }
  }

  if (type->IsVoidTy()) {
    Error(token, "variable or field '{}' declared void", name);
  } else if (type->IsFunctionTy() && !scope_->IsFileScope()) {
    Error(token, "function declaration is not allowed here");
  }

  Linkage linkage;
  if (scope_->IsFileScope()) {
    if (storage_class_spec & kStatic) {
      linkage = kInternal;
    } else {
      linkage = kExternal;
    }
  } else {
    linkage = kNone;
  }

  auto ident{scope_->FindUsualInCurrScope(name)};
  //有链接对象(外部或内部)的声明可以重复
  if (ident) {
    if (!type->Compatible(ident->GetType())) {
      Error(token, "conflicting types '{}' vs '{}'", type->ToString(),
            ident->GetType()->ToString());
    }

    if (linkage == kNone) {
      Error(token, "redefinition of '{}'", name);
    } else if (linkage == kExternal) {
      // static int a = 1;
      // extern int a;
      // 这种情况是可以的
      if (ident->GetLinkage() == kNone) {
        Error(token, "conflicting linkage '{}'", name);
      } else {
        linkage = ident->GetLinkage();
      }
    } else {
      if (ident->GetLinkage() != kInternal) {
        Error(token, "conflicting linkage '{}'", name);
      }
    }

    // extern int a;
    // int a = 1;
    if (auto obj{ident->ToObjectExpr()}) {
      if (!(storage_class_spec & kExtern)) {
        obj->SetStorageClassSpec(obj->GetStorageClassSpec() & ~kExtern);
      }

      auto decl{ident->ToObjectExpr()->GetDecl()};
      assert(decl != nullptr);
      return decl;
    }
  }

  // int a;
  // { extern int a;}
  if (storage_class_spec & kExtern) {
    ident = scope_->FindUsual(name);
    if (ident) {
      if (!type->Compatible(ident->GetType())) {
        Error(token, "conflicting types '{}' vs '{}'", type->ToString(),
              ident->GetType()->ToString());
      }

      if (ident->IsObject()) {
        auto decl{ident->ToObjectExpr()->GetDecl()};
        assert(decl != nullptr);
        return decl;
      }
    }
  }

  if (type->IsFunctionTy()) {
    if (align > 0) {
      Error(token, "'_Alignas' attribute applies to func");
    }

    type->FuncSetFuncSpec(func_spec);
    type->FuncSetName(name);

    ident = MakeAstNode<IdentifierExpr>(token, name, type, linkage, false);
    scope_->InsertUsual(name, ident);

    return MakeAstNode<Declaration>(token, ident);
  } else {
    auto obj{MakeAstNode<ObjectExpr>(token, name, type, storage_class_spec,
                                     linkage, false)};
    if (align > 0) {
      if (align < type->GetWidth()) {
        Error(token,
              "requested alignment is less than minimum alignment of {} for "
              "type '{}'",
              type->GetWidth(), type.ToString());
      }
      obj->SetAlign(align);
    }

    scope_->InsertUsual(obj);
    auto decl{MakeAstNode<Declaration>(token, obj)};
    obj->SetDecl(decl);

    if (obj->GetType()->IsComplete()) {
      // 全局变量和局部静态变量提前生成
      if (scope_->IsFileScope()) {
        CreateGlobalVar(obj);
      } else if (scope_->IsBlockScope() && storage_class_spec & kStatic) {
        obj->SetName(func_def_->GetName() + "." + obj->GetName());
        obj->SetGlobalPtr(
            CreateLocalStaticVar(obj->GetQualType(), obj->GetName()));
      }
    }

    return decl;
  }
}

/*
 * ExtDecl
 */
ExtDecl* Parser::ParseExternalDecl() {
  auto ext_decl{ParseDecl(true)};

  // _Static_assert / e.g. int;
  if (ext_decl == nullptr) {
    return nullptr;
  }

  TryParseAsm();
  TryParseAttributeSpec();

  if (Test(Tag::kLeftBrace)) {
    auto stmt{ext_decl->GetStmts()};
    if (std::size(stmt) != 1) {
      Error(Peek(), "unexpect left braces");
    }

    return ParseFuncDef(dynamic_cast<Declaration*>(stmt.front()));
  } else {
    Expect(Tag::kSemicolon);
    return ext_decl;
  }
}

FuncDef* Parser::ParseFuncDef(const Declaration* decl) {
  auto ident{decl->GetIdent()};
  if (!ident->GetType()->IsFunctionTy()) {
    Error(decl->GetLoc(), "func def need func type");
  }

  EnterFunc(ident);
  func_def_->SetBody(ParseCompoundStmt(ident->GetType()));
  auto ret{func_def_};
  ExitFunc();

  // label 具有函数作用域
  for (auto&& item : gotos_) {
    auto label{FindLabel(item->GetName())};
    if (label) {
      item->SetLabel(label);
    } else {
      Error(item->GetLoc(), "unknown label: {}", item->GetName());
    }
  }
  gotos_.clear();
  labels_.clear();

  return ret;
}

/*
 * Expr
 */
Expr* Parser::ParseExpr() {
  // GCC 扩展, 当使用 -ansi 时避免警告
  Try(Tag::kExtension);

  // GCC 扩展, 语句表达式
  if (auto expr{TryParseStmtExpr()}) {
    return expr;
  }

  auto lhs{ParseAssignExpr()};

  auto token{Peek()};
  while (Try(Tag::kComma)) {
    auto rhs{ParseAssignExpr()};
    lhs = MakeAstNode<BinaryOpExpr>(token, Tag::kComma, lhs, rhs);
    token = Peek();
  }

  return lhs;
}

Expr* Parser::ParseAssignExpr() {
  Try(Tag::kExtension);

  // 因为有很多是直接调用该函数而不是 ParseExpr
  // 所以需要再做一遍
  if (auto expr{TryParseStmtExpr()}) {
    return expr;
  }

  auto lhs{ParseConditionExpr()};
  Expr* rhs;

  auto token{Next()};
  switch (token.GetTag()) {
    case Tag::kEqual:
      rhs = ParseAssignExpr();
      break;
    case Tag::kStarEqual:
      rhs = ParseAssignExpr();
      rhs = MakeAstNode<BinaryOpExpr>(token, Tag::kStar, lhs, rhs);
      break;
    case Tag::kSlashEqual:
      rhs = ParseAssignExpr();
      rhs = MakeAstNode<BinaryOpExpr>(token, Tag::kSlash, lhs, rhs);
      break;
    case Tag::kPercentEqual:
      rhs = ParseAssignExpr();
      rhs = MakeAstNode<BinaryOpExpr>(token, Tag::kPercent, lhs, rhs);
      break;
    case Tag::kPlusEqual:
      rhs = ParseAssignExpr();
      rhs = MakeAstNode<BinaryOpExpr>(token, Tag::kPlus, lhs, rhs);
      break;
    case Tag::kMinusEqual:
      rhs = ParseAssignExpr();
      rhs = MakeAstNode<BinaryOpExpr>(token, Tag::kMinus, lhs, rhs);
      break;
    case Tag::kLessLessEqual:
      rhs = ParseAssignExpr();
      rhs = MakeAstNode<BinaryOpExpr>(token, Tag::kLessLess, lhs, rhs);
      break;
    case Tag::kGreaterGreaterEqual:
      rhs = ParseAssignExpr();
      rhs = MakeAstNode<BinaryOpExpr>(token, Tag::kGreaterGreater, lhs, rhs);
      break;
    case Tag::kAmpEqual:
      rhs = ParseAssignExpr();
      rhs = MakeAstNode<BinaryOpExpr>(token, Tag::kAmp, lhs, rhs);
      break;
    case Tag::kCaretEqual:
      rhs = ParseAssignExpr();
      rhs = MakeAstNode<BinaryOpExpr>(token, Tag::kCaret, lhs, rhs);
      break;
    case Tag::kPipeEqual:
      rhs = ParseAssignExpr();
      rhs = MakeAstNode<BinaryOpExpr>(token, Tag::kPipe, lhs, rhs);
      break;
    default: {
      PutBack();
      return lhs;
    }
  }

  return MakeAstNode<BinaryOpExpr>(token, Tag::kEqual, lhs, rhs);
}

Expr* Parser::ParseConditionExpr() {
  auto cond{ParseLogicalOrExpr()};

  auto token{Peek()};
  if (Try(Tag::kQuestion)) {
    // GCC 扩展
    // a ?: b 相当于 a ? a: c
    auto lhs{Test(Tag::kColon) ? cond : ParseExpr()};
    Expect(Tag::kColon);
    auto rhs{ParseConditionExpr()};

    return MakeAstNode<ConditionOpExpr>(token, cond, lhs, rhs);
  }

  return cond;
}

Expr* Parser::ParseLogicalOrExpr() {
  auto lhs{ParseLogicalAndExpr()};

  auto token{Peek()};
  while (Try(Tag::kPipePipe)) {
    auto rhs{ParseLogicalAndExpr()};
    lhs = MakeAstNode<BinaryOpExpr>(token, Tag::kPipePipe, lhs, rhs);
    token = Peek();
  }

  return lhs;
}

Expr* Parser::ParseLogicalAndExpr() {
  auto lhs{ParseInclusiveOrExpr()};

  auto token{Peek()};
  while (Try(Tag::kAmpAmp)) {
    auto rhs{ParseInclusiveOrExpr()};
    lhs = MakeAstNode<BinaryOpExpr>(token, Tag::kAmpAmp, lhs, rhs);
    token = Peek();
  }

  return lhs;
}

Expr* Parser::ParseInclusiveOrExpr() {
  auto lhs{ParseExclusiveOrExpr()};

  auto token{Peek()};
  while (Try(Tag::kPipe)) {
    auto rhs{ParseExclusiveOrExpr()};
    lhs = MakeAstNode<BinaryOpExpr>(token, Tag::kPipe, lhs, rhs);
    token = Peek();
  }

  return lhs;
}

Expr* Parser::ParseExclusiveOrExpr() {
  auto lhs{ParseAndExpr()};

  auto token{Peek()};
  while (Try(Tag::kCaret)) {
    auto rhs{ParseAndExpr()};
    lhs = MakeAstNode<BinaryOpExpr>(token, Tag::kCaret, lhs, rhs);
    token = Peek();
  }

  return lhs;
}

Expr* Parser::ParseAndExpr() {
  auto lhs{ParseEqualityExpr()};

  auto token{Peek()};
  while (Try(Tag::kAmp)) {
    auto rhs{ParseEqualityExpr()};
    lhs = MakeAstNode<BinaryOpExpr>(token, Tag::kAmp, lhs, rhs);
    token = Peek();
  }

  return lhs;
}

Expr* Parser::ParseEqualityExpr() {
  auto lhs{ParseRelationExpr()};

  auto token{Peek()};
  while (true) {
    if (Try(Tag::kEqualEqual)) {
      auto rhs{ParseRelationExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(token, Tag::kEqualEqual, lhs, rhs);
    } else if (Try(Tag::kExclaimEqual)) {
      auto rhs{ParseRelationExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(token, Tag::kExclaimEqual, lhs, rhs);
    } else {
      break;
    }
    token = Peek();
  }

  return lhs;
}

Expr* Parser::ParseRelationExpr() {
  auto lhs{ParseShiftExpr()};

  auto token{Peek()};
  while (true) {
    if (Try(Tag::kLess)) {
      auto rhs{ParseShiftExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(token, Tag::kLess, lhs, rhs);
    } else if (Try(Tag::kGreater)) {
      auto rhs{ParseShiftExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(token, Tag::kGreater, lhs, rhs);
    } else if (Try(Tag::kLessEqual)) {
      auto rhs{ParseShiftExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(token, Tag::kLessEqual, lhs, rhs);
    } else if (Try(Tag::kGreaterEqual)) {
      auto rhs{ParseShiftExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(token, Tag::kGreaterEqual, lhs, rhs);
    } else {
      break;
    }
    token = Peek();
  }

  return lhs;
}

Expr* Parser::ParseShiftExpr() {
  auto lhs{ParseAdditiveExpr()};

  auto token{Peek()};
  while (true) {
    if (Try(Tag::kLessLess)) {
      auto rhs{ParseAdditiveExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(token, Tag::kLessLess, lhs, rhs);
    } else if (Try(Tag::kGreaterGreater)) {
      auto rhs{ParseAdditiveExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(token, Tag::kGreaterGreater, lhs, rhs);
    } else {
      break;
    }
    token = Peek();
  }

  return lhs;
}

Expr* Parser::ParseAdditiveExpr() {
  auto lhs{ParseMultiplicativeExpr()};

  auto token{Peek()};
  while (true) {
    if (Try(Tag::kPlus)) {
      auto rhs{ParseMultiplicativeExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(token, Tag::kPlus, lhs, rhs);
    } else if (Try(Tag::kMinus)) {
      auto rhs{ParseMultiplicativeExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(token, Tag::kMinus, lhs, rhs);
    } else {
      break;
    }
    token = Peek();
  }

  return lhs;
}

Expr* Parser::ParseMultiplicativeExpr() {
  auto lhs{ParseCastExpr()};

  auto token{Peek()};
  while (true) {
    if (Try(Tag::kStar)) {
      auto rhs{ParseCastExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(token, Tag::kStar, lhs, rhs);
    } else if (Try(Tag::kSlash)) {
      auto rhs{ParseCastExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(token, Tag::kSlash, lhs, rhs);
    } else if (Try(Tag::kPercent)) {
      auto rhs{ParseCastExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(token, Tag::kPercent, lhs, rhs);
    } else {
      break;
    }
    token = Peek();
  }

  return lhs;
}

Expr* Parser::ParseCastExpr() {
  if (Try(Tag::kLeftParen)) {
    if (IsTypeName(Peek())) {
      auto type{ParseTypeName()};
      Expect(Tag::kRightParen);

      // 复合字面量
      if (Test(Tag::kLeftBrace)) {
        return ParsePostfixExprTail(ParseCompoundLiteral(type));
      } else {
        return MakeAstNode<TypeCastExpr>(Peek(), ParseCastExpr(), type);
      }
    } else {
      PutBack();
      return ParseUnaryExpr();
    }
  } else {
    return ParseUnaryExpr();
  }
}

Expr* Parser::ParseUnaryExpr() {
  auto token{Next()};
  switch (token.GetTag()) {
    // 默认为前缀
    case Tag::kPlusPlus:
      return MakeAstNode<UnaryOpExpr>(token, Tag::kPlusPlus, ParseUnaryExpr());
    case Tag::kMinusMinus:
      return MakeAstNode<UnaryOpExpr>(token, Tag::kMinusMinus,
                                      ParseUnaryExpr());
    case Tag::kAmp:
      return MakeAstNode<UnaryOpExpr>(token, Tag::kAmp, ParseCastExpr());
    case Tag::kStar:
      return MakeAstNode<UnaryOpExpr>(token, Tag::kStar, ParseCastExpr());
    case Tag::kPlus:
      return MakeAstNode<UnaryOpExpr>(token, Tag::kPlus, ParseCastExpr());
    case Tag::kMinus:
      return MakeAstNode<UnaryOpExpr>(token, Tag::kMinus, ParseCastExpr());
    case Tag::kTilde:
      return MakeAstNode<UnaryOpExpr>(token, Tag::kTilde, ParseCastExpr());
    case Tag::kExclaim:
      return MakeAstNode<UnaryOpExpr>(token, Tag::kExclaim, ParseCastExpr());
    case Tag::kSizeof:
      return ParseSizeof();
    case Tag::kAlignof:
      return ParseAlignof();
    case Tag::kOffsetof:
      return ParseOffsetof();
    case Tag::kTypeid:
      return ParseTypeid();
    default:
      PutBack();
      return ParsePostfixExpr();
  }
}

Expr* Parser::ParseSizeof() {
  QualType type;

  auto token{Peek()};
  if (Try(Tag::kLeftParen)) {
    if (!IsTypeName(Peek())) {
      auto expr{ParseExpr()};
      type = expr->GetType();
    } else {
      type = ParseTypeName();
    }
    Expect(Tag::kRightParen);
  } else {
    auto expr{ParseUnaryExpr()};
    type = expr->GetType();
  }

  if (!type->IsComplete() && !type->IsVoidTy() && !type->IsFunctionTy()) {
    Error(token, "sizeof(incomplete type)");
  }

  return MakeAstNode<ConstantExpr>(
      token, ArithmeticType::Get(kLong | kUnsigned),
      static_cast<std::uint64_t>(type->GetWidth()));
}

Expr* Parser::ParseAlignof() {
  QualType type;

  Expect(Tag::kLeftParen);

  auto token{Peek()};
  if (!IsTypeName(token)) {
    Error(token, "expect type name");
  }

  type = ParseTypeName();
  Expect(Tag::kRightParen);

  return MakeAstNode<ConstantExpr>(
      token, ArithmeticType::Get(kLong | kUnsigned),
      static_cast<std::uint64_t>(type->GetAlign()));
}

Expr* Parser::ParsePostfixExpr() {
  auto expr{TryParseCompoundLiteral()};

  if (expr) {
    return ParsePostfixExprTail(expr);
  } else {
    return ParsePostfixExprTail(ParsePrimaryExpr());
  }
}

Expr* Parser::TryParseCompoundLiteral() {
  auto begin{index_};

  if (Try(Tag::kLeftParen) && IsTypeName(Peek())) {
    auto type{ParseTypeName()};

    if (Try(Tag::kRightParen) && Test(Tag::kLeftBrace)) {
      return ParseCompoundLiteral(type);
    }
  }

  index_ = begin;
  return nullptr;
}

Expr* Parser::ParseCompoundLiteral(QualType type) {
  if (scope_->IsFileScope()) {
    auto obj{MakeAstNode<ObjectExpr>(Peek(), "", type, 0, kInternal, true)};
    auto decl{MakeAstNode<Declaration>(Peek(), obj)};

    decl->SetConstant(
        ParseConstantInitializer(decl->GetIdent()->GetType(), false, true));
    assert(decl->GetConstant() != nullptr);

    obj->SetGlobalPtr(CreateGlobalCompoundLiteral(type, decl->GetConstant()));

    return obj;
  } else {
    auto obj{MakeAstNode<ObjectExpr>(Peek(), "", type, 0, kNone, true)};
    auto decl{MakeAstNode<Declaration>(Peek(), obj)};

    ParseInitDeclaratorSub(decl);
    compound_stmt_.top()->AddStmt(decl);

    return obj;
  }
}

Expr* Parser::ParsePostfixExprTail(Expr* expr) {
  auto token{Peek()};
  while (true) {
    switch (Next().GetTag()) {
      case Tag::kLeftSquare:
        expr = ParseIndexExpr(expr);
        break;
      case Tag::kLeftParen:
        expr = ParseFuncCallExpr(expr);
        break;
      case Tag::kArrow:
        expr = MakeAstNode<UnaryOpExpr>(token, Tag::kStar, expr);
        expr = ParseMemberRefExpr(expr);
        break;
      case Tag::kPeriod:
        expr = ParseMemberRefExpr(expr);
        break;
      case Tag::kPlusPlus:
        expr = MakeAstNode<UnaryOpExpr>(token, Tag::kPostfixPlusPlus, expr);
        break;
      case Tag::kMinusMinus:
        expr = MakeAstNode<UnaryOpExpr>(token, Tag::kPostfixMinusMinus, expr);
        break;
      default:
        PutBack();
        return expr;
    }
    token = Peek();
  }
}

Expr* Parser::ParseIndexExpr(Expr* expr) {
  auto token{Peek()};
  auto rhs{ParseExpr()};
  Expect(Tag::kRightSquare);

  return MakeAstNode<UnaryOpExpr>(
      token, Tag::kStar,
      MakeAstNode<BinaryOpExpr>(token, Tag::kPlus, expr, rhs));
}

Expr* Parser::ParseFuncCallExpr(Expr* expr) {
  std::vector<Expr*> args;

  if (expr->GetType()->IsFunctionTy() &&
      expr->GetType()->FuncGetName() == "__builtin_va_arg_sub") {
    args.push_back(ParseAssignExpr());
    Expect(Tag::kComma);
    auto type{ParseTypeName()};
    Expect(Tag::kRightParen);
    auto ret{MakeAstNode<FuncCallExpr>(expr->GetLoc(), expr, args)};
    ret->SetVaArgType(type.GetType());
    return ret;
  }

  while (!Try(Tag::kRightParen)) {
    args.push_back(ParseAssignExpr());

    if (!Test(Tag::kRightParen)) {
      Expect(Tag::kComma);
    }
  }

  return MakeAstNode<FuncCallExpr>(expr->GetLoc(), expr, args);
}

Expr* Parser::ParseMemberRefExpr(Expr* expr) {
  auto token{Peek()};

  auto member{Expect(Tag::kIdentifier)};
  auto member_name{member.GetIdentifier()};

  auto type{expr->GetQualType()};
  if (!type->IsStructOrUnionTy()) {
    Error(expr, "an struct/union expected: '{}'", type.ToString());
  }

  auto rhs{type->StructGetMember(member_name)};
  if (!rhs) {
    Error(member, "'{}' is not a member of '{}'", member_name,
          type->StructGetName());
  }

  return MakeAstNode<BinaryOpExpr>(token, Tag::kPeriod, expr, rhs);
}

Expr* Parser::ParsePrimaryExpr() {
  auto token{Peek()};

  if (Peek().IsIdentifier()) {
    auto name{Next().GetIdentifier()};
    auto ident{scope_->FindUsual(name)};

    if (ident) {
      return ident;
    } else {
      Error(token, "undefined symbol: {}", name);
    }
  } else if (Peek().IsConstant()) {
    return ParseConstant();
  } else if (Peek().IsStringLiteral()) {
    return ParseStringLiteral();
  } else if (Try(Tag::kLeftParen)) {
    auto expr{ParseExpr()};
    Expect(Tag::kRightParen);
    return expr;
  } else if (Try(Tag::kGeneric)) {
    return ParseGenericSelection();
  } else if (Try(Tag::kFuncName)) {
    if (func_def_ == nullptr) {
      Error(token, "Not allowed to use __func__ or __FUNCTION__ here");
    }
    return MakeAstNode<StringLiteralExpr>(token, func_def_->GetName());
  } else if (Try(Tag::kFuncSignature)) {
    if (func_def_ == nullptr) {
      Error(token, "Not allowed to use __PRETTY_FUNCTION__ here");
    }
    return MakeAstNode<StringLiteralExpr>(
        token, func_def_->GetFuncType()->ToString() + ": " +
                   func_def_->GetFuncType()->FuncGetName());
  } else if (Try(Tag::kHugeVal)) {
    return ParseHugeVal();
  } else if (Try(Tag::kInff)) {
    return ParseInff();
  } else {
    Error(token, "'{}' unexpected", token.GetStr());
  }
}

Expr* Parser::ParseConstant() {
  if (Peek().IsCharacter()) {
    return ParseCharacter();
  } else if (Peek().IsInteger()) {
    return ParseInteger();
  } else if (Peek().IsFloatPoint()) {
    return ParseFloat();
  } else {
    assert(false);
    return nullptr;
  }
}

Expr* Parser::ParseCharacter() {
  auto token{Next()};
  Scanner scanner{token.GetStr(), token.GetLoc()};
  auto [val, encoding]{scanner.HandleCharacter()};

  std::uint32_t type_spec{};
  switch (encoding) {
    case Encoding::kNone:
      val = static_cast<char>(val);
      type_spec = kInt;
      break;
    case Encoding::kChar16:
      val = static_cast<char16_t>(val);
      type_spec = kShort | kUnsigned;
      break;
    case Encoding::kChar32:
      val = static_cast<char32_t>(val);
      type_spec = kInt | kUnsigned;
      break;
    case Encoding::kWchar:
      val = static_cast<wchar_t>(val);
      type_spec = kInt | kUnsigned;
      break;
    case Encoding::kUtf8:
      Error(token, "Can't use u8 here");
    default:
      assert(false);
  }

  return MakeAstNode<ConstantExpr>(token, ArithmeticType::Get(type_spec),
                                   static_cast<std::uint64_t>(val));
}

Expr* Parser::ParseInteger() {
  auto token{Next()};
  auto str{token.GetStr()};
  std::uint64_t val;
  std::size_t end;

  try {
    // GNU 扩展, 也可以有后缀
    if (std::size(str) >= 3 &&
        (str.substr(0, 2) == "0b" || str.substr(0, 2) == "0B")) {
      val = std::stoull(str.substr(2), &end, 2);
      end += 2;
    } else {
      // 当 base 为 0 时，自动检测进制
      val = std::stoull(str, &end, 0);
    }
  } catch (const std::out_of_range& error) {
    Error(token, "integer out of range");
  }

  auto backup{end};
  std::uint32_t type_spec{};
  for (auto ch{str[end]}; ch != '\0'; ch = str[++end]) {
    if (ch == 'u' || ch == 'U') {
      if (type_spec & kUnsigned) {
        Error(token, "invalid suffix: {}", str.substr(backup));
      }
      type_spec |= kUnsigned;
    } else if (ch == 'l' || ch == 'L') {
      if ((type_spec & kLong) || (type_spec & kLongLong)) {
        Error(token, "invalid suffix: {}", str.substr(backup));
      }

      if (str[end + 1] == 'l' || str[end + 1] == 'L') {
        type_spec |= kLongLong;
        ++end;
      } else {
        type_spec |= kLong;
      }
    } else {
      Error(token, "invalid suffix: {}", str.substr(backup));
    }
  }

  // 十进制
  bool decimal{'1' <= str.front() && str.front() <= '9'};

  if (decimal) {
    switch (type_spec) {
      case 0:
        if ((val > static_cast<std::uint64_t>(
                       std::numeric_limits<std::int32_t>::max()))) {
          type_spec |= kLong;
        } else {
          type_spec |= kInt;
        }
        break;
      case kUnsigned:
        if (val > std::numeric_limits<std::uint32_t>::max()) {
          type_spec |= (kLong | kUnsigned);
        } else {
          type_spec |= (kInt | kUnsigned);
        }
        break;
      default:
        break;
    }
  } else {
    switch (type_spec) {
      case 0:
        if (val > static_cast<std::uint64_t>(
                      std::numeric_limits<std::int64_t>::max())) {
          type_spec |= (kLong | kUnsigned);
        } else if (val > std::numeric_limits<std::uint32_t>::max()) {
          type_spec |= kLong;
        } else if (val > static_cast<std::uint64_t>(
                             std::numeric_limits<std::int32_t>::max())) {
          type_spec |= (kInt | kUnsigned);
        } else {
          type_spec |= kInt;
        }
        break;
      case kUnsigned:
        if (val > std::numeric_limits<std::uint32_t>::max()) {
          type_spec |= (kLong | kUnsigned);
        } else {
          type_spec |= (kInt | kUnsigned);
        }
        break;
      case kLong:
        if (val > static_cast<std::uint64_t>(
                      std::numeric_limits<std::int64_t>::max())) {
          type_spec |= (kLong | kUnsigned);
        } else {
          type_spec |= kLong;
        }
        break;
      case kLongLong:
        if (val > static_cast<std::uint64_t>(
                      std::numeric_limits<std::int64_t>::max())) {
          type_spec |= (kLongLong | kUnsigned);
        } else {
          type_spec |= kLongLong;
        }
        break;
      default:
        break;
    }
  }

  return MakeAstNode<ConstantExpr>(token, ArithmeticType::Get(type_spec), val);
}

Expr* Parser::ParseFloat() {
  auto tok{Next()};
  auto str{tok.GetStr()};
  long double val;
  std::size_t end;

  try {
    val = std::stold(str, &end);
  } catch (const std::out_of_range& err) {
    std::istringstream iss{str};
    iss >> val;
    end = iss.tellg();

    // 当值过小时是可以的
    if (std::abs(val) > 1.0) {
      Error(tok, "float point out of range");
    }
  }

  auto backup{end};
  std::uint32_t type_spec{kDouble};
  if (str[end] == 'f' || str[end] == 'F') {
    type_spec = kFloat;
    ++end;
  } else if (str[end] == 'l' || str[end] == 'L') {
    type_spec = kLong | kDouble;
    ++end;
  }

  if (str[end] != '\0') {
    Error(tok, "invalid suffix:{}", str.substr(backup));
  }

  return MakeAstNode<ConstantExpr>(tok, ArithmeticType::Get(type_spec),
                                   str.substr(0, backup));
}

StringLiteralExpr* Parser::ParseStringLiteral(bool handle_escape) {
  auto loc{Peek().GetLoc()};
  // 如果一个没有指定编码而另一个指定了那么可以连接
  // 两个都指定了不能连接
  auto tok{Expect(Tag::kStringLiteral)};
  auto [str, encoding]{
      Scanner{tok.GetStr(), tok.GetLoc()}.HandleStringLiteral(handle_escape)};
  ConvertString(str, encoding);

  while (Test(Tag::kStringLiteral)) {
    tok = Next();
    auto [next_str, next_encoding]{
        Scanner{tok.GetStr()}.HandleStringLiteral(handle_escape)};
    ConvertString(next_str, next_encoding);

    if (encoding == Encoding::kNone && next_encoding != Encoding::kNone) {
      ConvertString(str, next_encoding);
      encoding = next_encoding;
    } else if (encoding != Encoding::kNone &&
               next_encoding == Encoding::kNone) {
      ConvertString(next_str, encoding);
      next_encoding = encoding;
    }

    if (encoding != next_encoding) {
      Error(loc, "cannot concat literal with different encodings");
    }

    str += next_str;
  }

  std::uint32_t type_spec{};

  switch (encoding) {
    case Encoding::kUtf8:
    case Encoding::kNone:
      type_spec = kChar;
      break;
    case Encoding::kChar16:
      type_spec = kShort | kUnsigned;
      break;
    case Encoding::kChar32:
    case Encoding::kWchar:
      type_spec = kInt | kUnsigned;
      break;
    default:
      assert(false);
  }

  return MakeAstNode<StringLiteralExpr>(loc, ArithmeticType::Get(type_spec),
                                        str);
}

Expr* Parser::ParseGenericSelection() {
  Expect(Tag::kLeftParen);
  auto control_expr{ParseAssignExpr()};
  control_expr = Expr::MayCast(control_expr);
  Expect(Tag::kComma);

  Expr* ret{nullptr};
  Expr* default_expr{nullptr};

  auto token{Peek()};
  while (true) {
    if (Try(Tag::kDefault)) {
      if (default_expr) {
        Error(token, "duplicate default generic association");
      }

      Expect(Tag::kColon);
      default_expr = ParseAssignExpr();
    } else {
      auto type{ParseTypeName()};

      if (type->Compatible(control_expr->GetType())) {
        if (ret) {
          Error(token,
                "more than one generic association are compatible with control "
                "expression");
        }

        Expect(Tag::kColon);
        ret = ParseAssignExpr();
      } else {
        Expect(Tag::kColon);
        ParseAssignExpr();
      }
    }

    if (!Try(Tag::kComma)) {
      Expect(Tag::kRightParen);
      break;
    }
  }

  if (!ret && !default_expr) {
    Error(Peek(), "no compatible generic association");
  }

  return ret ? ret : default_expr;
}

Expr* Parser::ParseConstantExpr() { return ParseConditionExpr(); }

/*
 * Stmt
 */
Stmt* Parser::ParseStmt() {
  TryParseAttributeSpec();

  switch (Peek().GetTag()) {
    case Tag::kIdentifier: {
      Next();
      if (Peek().TagIs(Tag::kColon)) {
        PutBack();
        return ParseLabelStmt();
      } else {
        PutBack();
        return ParseExprStmt();
      }
    }
    case Tag::kCase:
      return ParseCaseStmt();
    case Tag::kDefault:
      return ParseDefaultStmt();
    case Tag::kLeftBrace:
      return ParseCompoundStmt();
    case Tag::kIf:
      return ParseIfStmt();
    case Tag::kSwitch:
      return ParseSwitchStmt();
    case Tag::kWhile:
      return ParseWhileStmt();
    case Tag::kDo:
      return ParseDoWhileStmt();
    case Tag::kFor:
      return ParseForStmt();
    case Tag::kGoto:
      return ParseGotoStmt();
    case Tag::kContinue:
      return ParseContinueStmt();
    case Tag::kBreak:
      return ParseBreakStmt();
    case Tag::kReturn:
      return ParseReturnStmt();
    default:
      return ParseExprStmt();
  }
}

Stmt* Parser::ParseLabelStmt() {
  auto token{Expect(Tag::kIdentifier)};
  Expect(Tag::kColon);

  TryParseAttributeSpec();

  auto name{token.GetIdentifier()};
  if (FindLabel(name)) {
    Error(token, "redefine of label: '{}'", token.GetIdentifier());
  }

  auto label{MakeAstNode<LabelStmt>(token, name, ParseStmt())};
  labels_[name] = label;

  return label;
}

Stmt* Parser::ParseCaseStmt() {
  auto token{Expect(Tag::kCase)};

  auto lhs{ParseInt64Constant()};

  if (Try(Tag::kEllipsis)) {
    auto rhs{ParseInt64Constant()};
    Expect(Tag::kColon);
    return MakeAstNode<CaseStmt>(token, lhs, rhs, ParseStmt());
  } else {
    Expect(Tag::kColon);
    return MakeAstNode<CaseStmt>(token, lhs, ParseStmt());
  }
}

Stmt* Parser::ParseDefaultStmt() {
  auto token{Expect(Tag::kDefault)};
  Expect(Tag::kColon);

  return MakeAstNode<DefaultStmt>(token, ParseStmt());
}

CompoundStmt* Parser::ParseCompoundStmt(Type* func_type) {
  auto token{Expect(Tag::kLeftBrace)};

  EnterBlock(func_type);

  auto stmts{MakeAstNode<CompoundStmt>(token)};
  compound_stmt_.push(stmts);

  while (!Try(Tag::kRightBrace)) {
    if (IsDecl(Peek())) {
      stmts->AddStmt(ParseDecl());
    } else {
      stmts->AddStmt(ParseStmt());
    }
  }

  ExitBlock();
  compound_stmt_.pop();

  return stmts;
}

Stmt* Parser::ParseExprStmt() {
  auto token{Peek()};
  if (Try(Tag::kSemicolon)) {
    return MakeAstNode<ExprStmt>(token);
  } else {
    auto ret{MakeAstNode<ExprStmt>(token, ParseExpr())};
    Expect(Tag::kSemicolon);
    return ret;
  }
}

Stmt* Parser::ParseIfStmt() {
  auto token{Expect(Tag::kIf)};

  Expect(Tag::kLeftParen);
  auto cond{ParseExpr()};
  Expect(Tag::kRightParen);

  auto then_block{ParseStmt()};
  if (Try(Tag::kElse)) {
    return MakeAstNode<IfStmt>(token, cond, then_block, ParseStmt());
  } else {
    return MakeAstNode<IfStmt>(token, cond, then_block);
  }
}

Stmt* Parser::ParseSwitchStmt() {
  auto token{Expect(Tag::kSwitch)};

  Expect(Tag::kLeftParen);
  auto cond{ParseExpr()};
  Expect(Tag::kRightParen);

  return MakeAstNode<SwitchStmt>(token, cond, ParseStmt());
}

Stmt* Parser::ParseWhileStmt() {
  auto token{Expect(Tag::kWhile)};

  Expect(Tag::kLeftParen);
  auto cond{ParseExpr()};
  Expect(Tag::kRightParen);

  return MakeAstNode<WhileStmt>(token, cond, ParseStmt());
}

Stmt* Parser::ParseDoWhileStmt() {
  auto token{Expect(Tag::kDo)};

  auto stmt{ParseStmt()};

  Expect(Tag::kWhile);
  Expect(Tag::kLeftParen);
  auto cond{ParseExpr()};
  Expect(Tag::kRightParen);
  Expect(Tag::kSemicolon);

  return MakeAstNode<DoWhileStmt>(token, cond, stmt);
}

Stmt* Parser::ParseForStmt() {
  auto token{Expect(Tag::kFor)};
  Expect(Tag::kLeftParen);

  Expr *init{}, *cond{}, *inc{};
  Stmt* block{};
  Stmt* decl{};

  EnterBlock();
  if (IsDecl(Peek())) {
    decl = ParseDecl(false);
  } else if (!Try(Tag::kSemicolon)) {
    init = ParseExpr();
    Expect(Tag::kSemicolon);
  }

  if (!Try(Tag::kSemicolon)) {
    cond = ParseExpr();
    Expect(Tag::kSemicolon);
  }

  if (!Try(Tag::kRightParen)) {
    inc = ParseExpr();
    Expect(Tag::kRightParen);
  }

  block = ParseStmt();
  ExitBlock();

  return MakeAstNode<ForStmt>(token, init, cond, inc, block, decl);
}

Stmt* Parser::ParseGotoStmt() {
  Expect(Tag::kGoto);
  auto tok{Expect(Tag::kIdentifier)};
  Expect(Tag::kSemicolon);

  auto ret{MakeAstNode<GotoStmt>(tok, tok.GetIdentifier())};
  gotos_.push_back(ret);

  return ret;
}

Stmt* Parser::ParseContinueStmt() {
  auto token{Expect(Tag::kContinue)};
  Expect(Tag::kSemicolon);

  return MakeAstNode<ContinueStmt>(token);
}

Stmt* Parser::ParseBreakStmt() {
  auto token{Expect(Tag::kBreak)};
  Expect(Tag::kSemicolon);

  return MakeAstNode<BreakStmt>(token);
}

Stmt* Parser::ParseReturnStmt() {
  auto token{Expect(Tag::kReturn)};

  if (Try(Tag::kSemicolon)) {
    return MakeAstNode<ReturnStmt>(token);
  } else {
    auto expr{ParseExpr()};
    expr = Expr::MayCastTo(expr, func_def_->GetFuncType()->FuncGetReturnType());

    Expect(Tag::kSemicolon);

    return MakeAstNode<ReturnStmt>(token, expr);
  }
}

/*
 * Decl
 */
CompoundStmt* Parser::ParseDecl(bool maybe_func_def) {
  if (Try(Tag::kStaticAssert)) {
    ParseStaticAssertDecl();
    return nullptr;
  } else {
    std::uint32_t storage_class_spec{}, func_spec{};
    std::int32_t align{};
    auto base_type{ParseDeclSpec(&storage_class_spec, &func_spec, &align)};

    if (Try(Tag::kSemicolon)) {
      return nullptr;
    } else {
      if (maybe_func_def) {
        return ParseInitDeclaratorList(base_type, storage_class_spec, func_spec,
                                       align);
      } else {
        auto ret{ParseInitDeclaratorList(base_type, storage_class_spec,
                                         func_spec, align)};
        Expect(Tag::kSemicolon);
        return ret;
      }
    }
  }
}

void Parser::ParseStaticAssertDecl() {
  Expect(Tag::kLeftParen);
  auto expr{ParseConstantExpr()};
  Expect(Tag::kComma);

  auto msg{ParseStringLiteral(false)->GetStr()};
  Expect(Tag::kRightParen);
  Expect(Tag::kSemicolon);

  if (!CalcConstantExpr{}.CalcInteger(expr)) {
    Error(expr, "static_assert failed \"{}\"", msg);
  }
}

/*
 * Decl Spec
 */
QualType Parser::ParseDeclSpec(std::uint32_t* storage_class_spec,
                               std::uint32_t* func_spec, std::int32_t* align) {
#define CHECK_AND_SET_STORAGE_CLASS_SPEC(spec)                  \
  if (*storage_class_spec != 0) {                               \
    Error(tok, "duplicated storage class specifier");           \
  } else if (!storage_class_spec) {                             \
    Error(tok, "storage class specifier are not allowed here"); \
  }                                                             \
  *storage_class_spec |= spec;

#define CHECK_AND_SET_FUNC_SPEC(spec)                                   \
  if (*func_spec & spec) {                                              \
    Warning(tok, "duplicate function specifier declaration specifier"); \
  } else if (!func_spec) {                                              \
    Error(tok, "function specifiers are not allowed here");             \
  }                                                                     \
  *func_spec |= spec;

#define ERROR Error(tok, "two or more data types in declaration specifiers");

#define TYPEOF_CHECK                                              \
  if (has_typeof) {                                               \
    Error(tok, "It is not allowed to use type specifiers here."); \
  }

  std::uint32_t type_spec{}, type_qual{};
  bool has_typeof{false};

  Token tok;
  QualType type;

  while (true) {
    TryParseAttributeSpec();

    tok = Next();

    switch (tok.GetTag()) {
      // GCC 扩展
      case Tag::kExtension:
        break;
      case Tag::kTypeof:
        if (type_spec != 0) {
          Error(tok, "It is not allowed to use typeof here.");
        }
        type = ParseTypeof();
        has_typeof = true;
        break;

        // Storage Class Specifier, 至多有一个
      case Tag::kTypedef:
        CHECK_AND_SET_STORAGE_CLASS_SPEC(kTypedef) break;
      case Tag::kExtern:
        CHECK_AND_SET_STORAGE_CLASS_SPEC(kExtern) break;
      case Tag::kStatic:
        CHECK_AND_SET_STORAGE_CLASS_SPEC(kStatic) break;
      case Tag::kAuto:
        CHECK_AND_SET_STORAGE_CLASS_SPEC(kAuto) break;
      case Tag::kRegister:
        CHECK_AND_SET_STORAGE_CLASS_SPEC(kRegister) break;
      case Tag::kThreadLocal:
        Error(tok, "Does not support _Thread_local");

        // Type specifier
      case Tag::kVoid:
        if (type_spec) {
          ERROR
        }
        TYPEOF_CHECK type_spec |= kVoid;
        break;
      case Tag::kChar:
        if (type_spec & ~kCompChar) {
          ERROR
        }
        TYPEOF_CHECK type_spec |= kChar;
        break;
      case Tag::kShort:
        if (type_spec & ~kCompShort) {
          ERROR
        }
        TYPEOF_CHECK type_spec |= kShort;
        break;
      case Tag::kInt:
        if (type_spec & ~kCompInt) {
          ERROR
        }
        TYPEOF_CHECK type_spec |= kInt;
        break;
      case Tag::kLong:
        if (type_spec & ~kCompLong) {
          ERROR
        }
        TYPEOF_CHECK if (type_spec & kLong) {
          type_spec &= ~kLong;
          type_spec |= kLongLong;
        }
        else {
          type_spec |= kLong;
        }
        break;
      case Tag::kFloat:
        if (type_spec) {
          ERROR
        }
        TYPEOF_CHECK type_spec |= kFloat;
        break;
      case Tag::kDouble:
        if (type_spec & ~kCompDouble) {
          ERROR
        }
        TYPEOF_CHECK type_spec |= kDouble;
        break;
      case Tag::kSigned:
        if (type_spec & ~kCompSigned) {
          ERROR
        }
        TYPEOF_CHECK type_spec |= kSigned;
        break;
      case Tag::kUnsigned:
        if (type_spec & ~kCompUnsigned) {
          ERROR
        }
        TYPEOF_CHECK type_spec |= kUnsigned;
        break;
      case Tag::kBool:
        if (type_spec) {
          ERROR
        }
        TYPEOF_CHECK type_spec |= kBool;
        break;
      case Tag::kStruct:
      case Tag::kUnion:
        if (type_spec) {
          ERROR
        }
        TYPEOF_CHECK type = ParseStructUnionSpec(tok.GetTag() == Tag::kStruct);
        type_spec |= kStructUnionSpec;
        break;
      case Tag::kEnum:
        if (type_spec) {
          ERROR
        }
        TYPEOF_CHECK type = ParseEnumSpec();
        type_spec |= kEnumSpec;
        break;
      case Tag::kComplex:
        TYPEOF_CHECK Error(tok, "Does not support _Complex");
      case Tag::kAtomic:
        TYPEOF_CHECK Error(tok, "Does not support _Atomic");

        // Type qualifier
      case Tag::kConst:
        type_qual |= kConst;
        break;
      case Tag::kRestrict:
        type_qual |= kRestrict;
        break;
      case Tag::kVolatile:
        type_qual |= kVolatile;
        break;

        // Function specifier
      case Tag::kInline:
        CHECK_AND_SET_FUNC_SPEC(kInline) break;
      case Tag::kNoreturn:
        CHECK_AND_SET_FUNC_SPEC(kNoreturn) break;

      case Tag::kAlignas:
        if (!align) {
          Error(tok, "_Alignas are not allowed here");
        }
        *align = std::max(ParseAlignas(), *align);
        break;

      default: {
        if (type_spec == 0 && IsTypeName(tok)) {
          auto ident{scope_->FindUsual(tok.GetIdentifier())};
          type = ident->GetQualType();
          type_spec |= kTypedefName;

          //  typedef int A[];
          //  A a = {1, 2};
          //  A b = {3, 4, 5};
          // 防止类型被修改
          if (type->IsArrayTy() && !type->IsComplete()) {
            type = ArrayType::Get(type->ArrayGetElementType(),
                                  type->ArrayGetNumElements());
          }
        } else {
          goto finish;
        }
      }
    }
  }

finish:
  PutBack();

  TryParseAttributeSpec();

  switch (type_spec) {
    case 0:
      if (!has_typeof) {
        Error(tok, "type specifier missing: {}", tok.GetStr());
      }
      break;
    case kVoid:
      type = VoidType::Get();
      break;
    case kStructUnionSpec:
    case kEnumSpec:
    case kTypedefName:
      break;
    default:
      type = ArithmeticType::Get(type_spec);
  }

  return QualType{type.GetType(), type.GetTypeQual() | type_qual};

#undef CHECK_AND_SET_STORAGE_CLASS_SPEC
#undef CHECK_AND_SET_FUNC_SPEC
#undef ERROR
#undef TYPEOF_CHECK
}

Type* Parser::ParseStructUnionSpec(bool is_struct) {
  TryParseAttributeSpec();

  auto tok{Peek()};
  std::string tag_name;

  if (Try(Tag::kIdentifier)) {
    tag_name = tok.GetIdentifier();
    // 定义
    if (Try(Tag::kLeftBrace)) {
      auto tag{scope_->FindTagInCurrScope(tag_name)};
      // 无前向声明
      if (!tag) {
        auto type{StructType::Get(is_struct, tag_name, scope_)};
        auto ident{MakeAstNode<IdentifierExpr>(tok, tag_name, type)};
        scope_->InsertTag(ident);

        ParseStructDeclList(type);
        Expect(Tag::kRightBrace);
        return type;
      } else {
        if (tag->GetType()->IsComplete()) {
          Error(tok, "redefinition struct or union :{}", tag_name);
        } else {
          ParseStructDeclList(dynamic_cast<StructType*>(tag->GetType()));

          Expect(Tag::kRightBrace);
          return tag->GetType();
        }
      }
    } else {
      // 可能是前向声明或普通的声明
      auto tag{scope_->FindTag(tag_name)};

      if (tag) {
        return tag->GetType();
      } else {
        auto type{StructType::Get(is_struct, tag_name, scope_)};
        auto ident{MakeAstNode<IdentifierExpr>(tok, tag_name, type)};
        scope_->InsertTag(ident);
        return type;
      }
    }
  } else {
    // 无标识符只能是定义
    Expect(Tag::kLeftBrace);

    auto type{StructType::Get(is_struct, "", scope_)};
    ParseStructDeclList(type);

    Expect(Tag::kRightBrace);
    return type;
  }
}

void Parser::ParseStructDeclList(StructType* type) {
  assert(!type->IsComplete());

  auto scope_backup{scope_};
  scope_ = type->GetScope();

  while (!Test(Tag::kRightBrace)) {
    if (Try(Tag::kStaticAssert)) {
      ParseStaticAssertDecl();
    } else {
      std::int32_t align{};
      auto base_type{ParseDeclSpec(nullptr, nullptr, &align)};

      do {
        Token tok;
        auto copy{base_type};
        ParseDeclarator(tok, copy);

        TryParseAttributeSpec();

        // 位域
        if (Try(Tag::kColon)) {
          ParseBitField(type, tok, copy);
          continue;
        }

        // struct A {
        //  int a;
        //  struct {
        //    int c;
        //  };
        //};
        if (std::empty(tok.GetStr())) {
          // 此时该 struct / union 不能有名字
          if (copy->IsStructOrUnionTy() && !copy->StructHasName()) {
            auto anonymous{
                MakeAstNode<ObjectExpr>(tok, "", copy, 0, kNone, true)};
            type->MergeAnonymous(anonymous);
            continue;
          } else {
            Error(Peek(), "declaration does not declare anything");
          }
        } else {
          auto name{tok.GetIdentifier()};

          if (type->GetMember(name)) {
            Error(Peek(), "duplicate member:{}", name);
          } else if (copy->IsArrayTy() && !copy->IsComplete()) {
            // 可能是柔性数组
            // 若结构体定义了至少一个具名成员,
            // 则额外声明其最后成员拥有不完整的数组类型
            if (type->IsStruct() && std::size(type->GetMembers()) > 0) {
              auto member{MakeAstNode<ObjectExpr>(tok, name, copy)};
              type->AddMember(member);
              Expect(Tag::kSemicolon);

              goto finalize;
            } else {
              Error(Peek(), "field '{}' has incomplete type", name);
            }
          } else if (copy->IsFunctionTy()) {
            Error(Peek(), "field '{}' declared as a function", name);
          } else {
            auto member{MakeAstNode<ObjectExpr>(tok, name, copy)};
            type->AddMember(member);
          }
        }
      } while (Try(Tag::kComma));

      Expect(Tag::kSemicolon);
    }
  }

finalize:
  TryParseAttributeSpec();

  type->SetComplete(true);

  // struct / union 中的 tag 的作用域与该 struct / union 所在的作用域相同
  for (const auto& [name, tag] : scope_->AllTagInCurrScope()) {
    if (scope_backup->FindTagInCurrScope(name)) {
      Error(tag->GetLoc(), "redefinition of tag {}", tag->GetName());
    } else {
      scope_backup->InsertTag(name, tag);
    }
  }

  scope_ = scope_backup;
}

void Parser::ParseBitField(StructType* type, const Token& tok,
                           QualType member_type) {
  if (!member_type->IsIntegerTy() && !member_type->IsBoolTy()) {
    Error(tok, "expect integer or bool type for bitfield but got ('{}')",
          member_type.ToString());
  }

  auto expr{ParseConstantExpr()};
  auto width{CalcConstantExpr{}.CalcInteger(expr)};

  if (width < 0) {
    Error(expr, "expect non negative value");
  } else if (width == 0 && !std::empty(tok.GetStr())) {
    Error(tok, "no declarator expected for a bitfield with width 0");
  } else if (width > member_type->GetWidth() * 8) {
    Error(expr, "width exceeds its type");
  }

  auto offset{type->GetOffset() - member_type->GetWidth()};
  offset = StructType::MakeAlign(std::max(offset, 0), type->GetAlign());

  std::int32_t bit_field_offset;
  std::int8_t begin;

  if (!type->IsStruct()) {
    begin = 0;
    bit_field_offset = 0;
  } else if (type->GetNumMembers() == 0) {
    begin = 0;
    bit_field_offset = 0;
  } else {
    auto last = type->GetMembers().back();
    auto total_bits = last->GetOffset() * 8;
    if (last->BitFieldWidth()) {
      total_bits += last->BitFieldEnd();
    } else {
      total_bits += last->GetType()->GetWidth() * 8;
    }

    if (width == 0) width = type->GetWidth() * 8 - total_bits;
    if (width == 0) return;
    if (width + total_bits <= type->GetWidth() * 8) {
      begin = total_bits % 8;
      bit_field_offset = total_bits / 8;
    } else {
      begin = 0;
      bit_field_offset =
          StructType::MakeAlign(type->GetOffset(), type->GetWidth());
    }
  }

  ObjectExpr* bit_field;
  if (std::empty(tok.GetStr())) {
    bit_field = MakeAstNode<ObjectExpr>(tok, "", member_type, 0, Linkage::kNone,
                                        true, begin, width);
  } else {
    bit_field = MakeAstNode<ObjectExpr>(tok, tok.GetIdentifier(), member_type,
                                        0, Linkage::kNone, false, begin, width);
  }

  type->AddBitField(bit_field, bit_field_offset);
}

Type* Parser::ParseEnumSpec() {
  TryParseAttributeSpec();

  std::string tag_name;
  auto tok{Peek()};

  if (Try(Tag::kIdentifier)) {
    tag_name = tok.GetIdentifier();
    // 定义
    if (Try(Tag::kLeftBrace)) {
      auto tag{scope_->FindTagInCurrScope(tag_name)};

      if (!tag) {
        auto type{ArithmeticType::Get(32)};
        auto ident{MakeAstNode<IdentifierExpr>(tok, tag_name, type)};
        scope_->InsertTag(tag_name, ident);
        ParseEnumerator();

        Expect(Tag::kRightBrace);
        return type;
      } else {
        // 不允许前向声明，如果当前作用域中有 tag 则就是重定义
        Error(tok, "redefinition of enumeration tag: {}", tag_name);
      }
    } else {
      // 只能是普通声明
      auto tag{scope_->FindTag(tag_name)};
      if (tag) {
        return tag->GetType();
      } else {
        Error(tok, "unknown enumeration: {}", tag_name);
      }
    }
  } else {
    Expect(Tag::kLeftBrace);
    ParseEnumerator();
    Expect(Tag::kRightBrace);
    return ArithmeticType::Get(32);
  }
}

void Parser::ParseEnumerator() {
  std::int32_t val{};

  do {
    auto tok{Expect(Tag::kIdentifier)};
    TryParseAttributeSpec();

    auto name{tok.GetIdentifier()};
    auto ident{scope_->FindUsualInCurrScope(name)};

    if (ident) {
      Error(tok, "redefinition of enumerator '{}'", name);
    }

    if (Try(Tag::kEqual)) {
      auto expr{ParseConstantExpr()};
      val = CalcConstantExpr{}.CalcInteger(expr);
    }

    auto enumer{MakeAstNode<EnumeratorExpr>(tok, tok.GetIdentifier(), val)};
    ++val;
    scope_->InsertUsual(name, enumer);

    Try(Tag::kComma);
  } while (!Test(Tag::kRightBrace));
}

std::int32_t Parser::ParseAlignas() {
  Expect(Tag::kLeftParen);

  std::int32_t align{};
  auto tok{Peek()};

  if (IsTypeName(tok)) {
    auto type{ParseTypeName()};
    align = type->GetAlign();
  } else {
    auto expr{ParseConstantExpr()};
    align = CalcConstantExpr{}.CalcInteger(expr);
  }

  Expect(Tag::kRightParen);

  if (align < 0 || ((align - 1) & align)) {
    Error(tok, "requested alignment is not a power of 2");
  }

  return align;
}

/*
 * Declarator
 */
CompoundStmt* Parser::ParseInitDeclaratorList(QualType& base_type,
                                              std::uint32_t storage_class_spec,
                                              std::uint32_t func_spec,
                                              std::int32_t align) {
  auto stmts{MakeAstNode<CompoundStmt>(Peek())};

  do {
    auto copy{base_type};
    stmts->AddStmt(
        ParseInitDeclarator(copy, storage_class_spec, func_spec, align));
    TryParseAttributeSpec();
  } while (Try(Tag::kComma));

  return stmts;
}

Declaration* Parser::ParseInitDeclarator(QualType& base_type,
                                         std::uint32_t storage_class_spec,
                                         std::uint32_t func_spec,
                                         std::int32_t align) {
  auto token{Peek()};
  Token tok;
  ParseDeclarator(tok, base_type);

  if (std::empty(tok.GetStr())) {
    Error(token, "expect identifier");
  }

  auto decl{
      MakeDeclaration(tok, base_type, storage_class_spec, func_spec, align)};

  bool flag{false};

  if (decl && decl->IsObjDecl() && !decl->GetIdent()->GetType()->IsComplete() &&
      Test(Tag::kEqual)) {
    flag = true;
  }

  if (decl && decl->IsObjDecl()) {
    if (Try(Tag::kEqual)) {
      if (!scope_->IsFileScope() &&
          !(scope_->IsBlockScope() && storage_class_spec & kStatic)) {
        ParseInitDeclaratorSub(decl);
      } else {
        decl->SetConstant(
            ParseConstantInitializer(decl->GetIdent()->GetType(), false, true));
      }
    }

    if (flag) {
      auto obj{decl->GetIdent()->ToObjectExpr()};
      if (scope_->IsFileScope()) {
        CreateGlobalVar(obj);
      } else if (scope_->IsBlockScope() && storage_class_spec & kStatic) {
        obj->SetName(func_def_->GetName() + "." + obj->GetName());
        obj->SetGlobalPtr(
            CreateLocalStaticVar(obj->GetQualType(), obj->GetName()));
      }
    }

    if (scope_->IsFileScope() ||
        (scope_->IsBlockScope() && storage_class_spec & kStatic)) {
      auto obj{decl->GetIdent()->ToObjectExpr()};
      assert(obj != nullptr);

      if (!obj->IsStatic() && !obj->IsExtern() && !decl->HasConstantInit()) {
        obj->GetGlobalPtr()->setLinkage(llvm::GlobalVariable::CommonLinkage);
      }

      if (decl->HasConstantInit()) {
        obj->GetGlobalPtr()->setInitializer(decl->GetConstant());
      } else {
        if (!obj->IsExtern()) {
          obj->GetGlobalPtr()->setInitializer(
              GetConstantZero(obj->GetType()->GetLLVMType()));
        }
      }
    }
  }

  return decl;
}

void Parser::ParseInitDeclaratorSub(Declaration* decl) {
  auto ident{decl->GetIdent()};

  if (!scope_->IsFileScope() && ident->GetLinkage() == kExternal) {
    Error(ident->GetLoc(), "{} has both 'extern' and initializer",
          ident->GetName());
  }

  if (!ident->GetType()->IsComplete() && !ident->GetType()->IsArrayTy()) {
    Error(ident->GetLoc(), "variable '{}' has initializer but incomplete type",
          ident->GetName());
  }

  std::vector<Initializer> inits;
  if (auto constant{ParseInitializer(inits, ident->GetType(), false, true)}) {
    decl->SetConstant(constant);
  } else {
    decl->AddInits(inits);
  }
}

void Parser::ParseDeclarator(Token& tok, QualType& base_type) {
  ParsePointer(base_type);
  ParseDirectDeclarator(tok, base_type);
}

void Parser::ParsePointer(QualType& type) {
  while (Try(Tag::kStar)) {
    type = QualType{PointerType::Get(type), ParseTypeQualList()};
  }
}

std::uint32_t Parser::ParseTypeQualList() {
  std::uint32_t type_qual{};

  auto token{Peek()};
  while (true) {
    if (Try(Tag::kConst)) {
      type_qual |= kConst;
    } else if (Try(Tag::kRestrict)) {
      type_qual |= kRestrict;
    } else if (Try(Tag::kVolatile)) {
      type_qual |= kVolatile;
    } else if (Try(Tag::kAtomic)) {
      Error(token, "Does not support _Atomic");
    } else {
      break;
    }
    token = Peek();
  }

  return type_qual;
}

void Parser::ParseDirectDeclarator(Token& tok, QualType& base_type) {
  if (Test(Tag::kIdentifier)) {
    tok = Next();
    ParseDirectDeclaratorTail(base_type);
  } else if (Try(Tag::kLeftParen)) {
    auto begin{index_};
    auto temp{QualType{ArithmeticType::Get(kInt)}};
    // 此时的 base_type 不一定是正确的, 先跳过括号中的内容
    ParseDeclarator(tok, temp);
    Expect(Tag::kRightParen);

    ParseDirectDeclaratorTail(base_type);
    auto end{index_};

    index_ = begin;
    ParseDeclarator(tok, base_type);
    Expect(Tag::kRightParen);
    index_ = end;
  } else {
    ParseDirectDeclaratorTail(base_type);
  }
}

void Parser::ParseDirectDeclaratorTail(QualType& base_type) {
  if (Try(Tag::kLeftSquare)) {
    if (base_type->IsFunctionTy()) {
      Error(Peek(), "the element of array cannot be a function");
    }

    auto len{ParseArrayLength()};
    Expect(Tag::kRightSquare);

    ParseDirectDeclaratorTail(base_type);

    if (!base_type->IsComplete()) {
      Error(Peek(), "has incomplete element type");
    }

    base_type = ArrayType::Get(base_type, len);
  } else if (Try(Tag::kLeftParen)) {
    if (base_type->IsFunctionTy()) {
      Error(Peek(), "the return value of function cannot be function");
    } else if (base_type->IsArrayTy()) {
      Error(Peek(), "the return value of function cannot be array");
    }

    EnterProto();
    auto [params, var_args]{ParseParamTypeList()};
    ExitProto();

    Expect(Tag::kRightParen);

    ParseDirectDeclaratorTail(base_type);

    base_type = FunctionType::Get(base_type, params, var_args);
  }
}

std::int64_t Parser::ParseArrayLength() {
  if (Test(Tag::kRightSquare)) {
    return -1;
  }

  auto expr{ParseAssignExpr()};

  if (!expr->GetQualType()->IsIntegerTy()) {
    Error(expr, "The array size must be an integer: '{}'",
          expr->GetType()->ToString());
  }

  // 不支持变长数组
  auto len{CalcConstantExpr{}.CalcInteger(expr)};

  if (len < 0) {
    Error(expr, "Array size must be greater than zero: '{}'", len);
  }

  return len;
}

std::pair<std::vector<ObjectExpr*>, bool> Parser::ParseParamTypeList() {
  if (Test(Tag::kRightParen)) {
    return {{}, false};
  }

  auto param{ParseParamDecl()};
  if (param->GetType()->IsVoidTy()) {
    return {{}, false};
  }

  std::vector<ObjectExpr*> params;
  params.push_back(param);

  while (Try(Tag::kComma)) {
    if (Try(Tag::kEllipsis)) {
      return {params, true};
    }

    param = ParseParamDecl();
    if (param->GetType()->IsVoidTy()) {
      Error(param->GetLoc(),
            "'void' must be the first and only parameter if specified");
    }
    params.push_back(param);
  }

  return {params, false};
}

// declaration-specifiers declarator
// declaration-specifiers abstract-declarator（此时不能是函数定义）
ObjectExpr* Parser::ParseParamDecl() {
  auto base_type{ParseDeclSpec(nullptr, nullptr, nullptr)};

  Token tok;
  ParseDeclarator(tok, base_type);

  base_type = Type::MayCast(base_type);

  if (std::empty(tok.GetStr())) {
    return MakeAstNode<ObjectExpr>(tok, "", base_type, 0, kNone, true);
  }

  auto decl{MakeDeclaration(tok, base_type, 0, 0, 0)};
  auto obj{decl->GetIdent()->ToObjectExpr()};
  obj->SetDecl(decl);

  return obj;
}

/*
 * Type Name
 */
QualType Parser::ParseTypeName() {
  auto base_type{ParseDeclSpec(nullptr, nullptr, nullptr)};
  ParseAbstractDeclarator(base_type);
  return base_type;
}

void Parser::ParseAbstractDeclarator(QualType& type) {
  ParsePointer(type);
  ParseDirectAbstractDeclarator(type);
}

void Parser::ParseDirectAbstractDeclarator(QualType& type) {
  Token tok;
  ParseDirectDeclarator(tok, type);

  if (!std::empty(tok.GetStr())) {
    Error(tok, "unexpected identifier '{}'", tok.GetStr());
  }
}

/*
 * Init
 */
// initializer:
//  assignment-expression
//  { initializer-list }
//  { initializer-list , }
llvm::Constant* Parser::ParseInitializer(std::vector<Initializer>& inits,
                                         QualType type, bool designated,
                                         bool force_brace) {
  // 比如解析 {[2]=1}
  if (designated && !Test(Tag::kPeriod) && !Test(Tag::kLeftSquare)) {
    Expect(Tag::kEqual);
  }

  if (type->IsArrayTy()) {
    // int a[2] = 1;
    // 不能直接 Expect , 如果有 '{' , 只能由 ParseArrayInitializer 来处理
    if (force_brace && !Test(Tag::kLeftBrace) && !Test(Tag::kStringLiteral)) {
      Expect(Tag::kLeftBrace);
    } else if (auto str{ParseLiteralInitializer(type.GetType())}; !str) {
      ParseArrayInitializer(inits, type.GetType(), designated);
      type->SetComplete(true);
    } else {
      return str;
    }
  } else if (type->IsStructOrUnionTy()) {
    if (!Test(Tag::kPeriod) && !Test(Tag::kLeftBrace)) {
      // struct A a = {...};
      // struct A b = a;
      // 或者是
      // struct {
      //    struct {
      //      int a;
      //      int b;
      //    } x;
      //    struct {
      //      char c[8];
      //    } y;
      //  } v = {
      //      1,
      //      2,
      //  };
      auto begin{index_};
      auto expr{ParseAssignExpr()};
      if (type->Compatible(expr->GetType())) {
        inits.emplace_back(type.GetType(), expr, indexs_);
        return nullptr;
      } else {
        index_ = begin;
      }
    }

    ParseStructInitializer(inits, type.GetType(), designated);
  } else {
    // 标量类型
    // int a={10}; / int a={10,}; 都是合法的
    auto has_brace{Try(Tag::kLeftBrace)};
    auto expr{ParseAssignExpr()};

    if (has_brace) {
      Try(Tag::kComma);
      Expect(Tag::kRightBrace);
    }

    inits.emplace_back(type.GetType(), expr, indexs_);
  }

  return nullptr;
}

void Parser::ParseArrayInitializer(std::vector<Initializer>& inits, Type* type,
                                   std::int32_t designated) {
  std::int64_t index{};
  auto has_brace{Try(Tag::kLeftBrace)};

  while (true) {
    if (Test(Tag::kRightBrace)) {
      if (has_brace) {
        Next();
      }
      return;
    }

    // e.g.
    // int a[10][10] = {1, [2][2] = 3};
    if (!designated && !has_brace &&
        (Test(Tag::kPeriod) || Test(Tag::kLeftSquare))) {
      // put ',' back
      PutBack();
      return;
    }

    if ((designated = Try(Tag::kLeftSquare))) {
      auto expr{ParseAssignExpr()};
      if (!expr->GetType()->IsIntegerTy()) {
        Error(expr, "expect integer type");
      }

      index = CalcConstantExpr{}.CalcInteger(expr);
      Expect(Tag::kRightSquare);

      if (type->IsComplete() && index >= type->ArrayGetNumElements()) {
        Error(expr, "array designator index {} exceeds array bounds", index);
      }
    }

    indexs_.push_back({type, index});
    ParseInitializer(inits, type->ArrayGetElementType(), designated, false);
    indexs_.pop_back();
    designated = false;
    ++index;

    // int a[] = {1, 2, [5] = 3}; 这种也是合法的
    if (!type->IsComplete()) {
      type->ArraySetNumElements(std::max(static_cast<std::int64_t>(index),
                                         type->ArrayGetNumElements()));
    }

    if (!Try(Tag::kComma)) {
      if (has_brace) {
        Expect(Tag::kRightBrace);
      }
      return;
    }
  }
}

llvm::Constant* Parser::ParseLiteralInitializer(Type* type) {
  if (!type->ArrayGetElementType()->IsIntegerTy()) {
    return nullptr;
  }

  auto has_brace{Try(Tag::kLeftBrace)};
  if (!Test(Tag::kStringLiteral)) {
    if (has_brace) {
      PutBack();
    }
    return nullptr;
  }

  auto str_node{ParseStringLiteral()};

  if (has_brace) {
    Try(Tag::kComma);
    Expect(Tag::kRightBrace);
  }

  if (!type->IsComplete()) {
    type->ArraySetNumElements(str_node->GetType()->ArrayGetNumElements());
    type->SetComplete(true);
  }

  if (str_node->GetType()->ArrayGetNumElements() >
      type->ArrayGetNumElements()) {
    Error(str_node->GetLoc(),
          "initializer-string for char array is too long '{}' to '{}",
          str_node->GetType()->ArrayGetNumElements(),
          type->ArrayGetNumElements());
  }

  if (str_node->GetType()->ArrayGetElementType()->GetWidth() !=
      type->ArrayGetElementType()->GetWidth()) {
    Error(str_node->GetLoc(), "Different character types '{}' vs '{}",
          str_node->GetType()->ArrayGetElementType()->ToString(),
          type->ArrayGetElementType()->ToString());
  }

  return str_node->GetPtr();
}

void Parser::ParseStructInitializer(std::vector<Initializer>& inits, Type* type,
                                    bool designated) {
  auto has_brace{Try(Tag::kLeftBrace)};
  auto member_iter{std::begin(type->StructGetMembers())};

  while (true) {
    if (Test(Tag::kRightBrace)) {
      if (has_brace) {
        Next();
      }
      return;
    }

    if (!designated && !has_brace &&
        (Test(Tag::kPeriod) || Test(Tag::kLeftSquare))) {
      PutBack();
      return;
    }

    if ((designated = Try(Tag::kPeriod))) {
      auto tok{Expect(Tag::kIdentifier)};
      auto name{tok.GetIdentifier()};

      if (!type->StructGetMember(name)) {
        Error(tok, "member '{}' not found", name);
      }

      member_iter = GetStructDesignator(type, name);
    }

    if (member_iter == std::end(type->StructGetMembers())) {
      break;
    }

    if ((*member_iter)->IsAnonymous()) {
      if (designated) {
        PutBack();
        PutBack();
      }

      indexs_.push_back(
          {type, member_iter - std::begin(type->StructGetMembers())});
      ParseInitializer(inits, (*member_iter)->GetType(), designated, false);
      indexs_.pop_back();
    } else {
      indexs_.push_back(
          {type, member_iter - std::begin(type->StructGetMembers())});
      ParseInitializer(inits, (*member_iter)->GetType(), designated, false);
      indexs_.pop_back();
    }

    designated = false;
    ++member_iter;

    if (!type->IsStructTy()) {
      break;
    }

    if (!has_brace && member_iter == std::end(type->StructGetMembers())) {
      break;
    }

    if (!Try(Tag::kComma)) {
      if (has_brace) {
        Expect(Tag::kRightBrace);
      }
      return;
    }
  }

  if (has_brace) {
    Try(Tag::kComma);
    if (!Try(Tag::kRightBrace)) {
      Error(Peek(), "excess members in struct initializer");
    }
  }
}

/*
 * ConstantInit
 */
llvm::Constant* Parser::ParseConstantInitializer(QualType type, bool designated,
                                                 bool force_brace) {
  if (designated && !Test(Tag::kPeriod) && !Test(Tag::kLeftSquare)) {
    Expect(Tag::kEqual);
  }

  if (type->IsArrayTy()) {
    if (force_brace && !Test(Tag::kLeftBrace) && !Test(Tag::kStringLiteral)) {
      Expect(Tag::kLeftBrace);
    } else if (auto p{ParseConstantLiteralInitializer(type.GetType())}; !p) {
      auto arr{ParseConstantArrayInitializer(type.GetType(), designated)};
      type->SetComplete(true);
      return arr;
    } else {
      return p;
    }
  } else if (type->IsStructOrUnionTy()) {
    return ParseConstantStructInitializer(type.GetType(), designated);
  } else {
    auto has_brace{Try(Tag::kLeftBrace)};
    auto expr{ParseAssignExpr()};

    if (has_brace) {
      Try(Tag::kComma);
      Expect(Tag::kRightBrace);
    }

    auto constant{CalcConstantExpr{}.Calc(expr)};
    if (constant) {
      return ConstantCastTo(constant, type->GetLLVMType(),
                            expr->GetType()->IsUnsigned());
    } else {
      Error(expr, "expect constant expression");
    }
  }

  assert(false);
  return nullptr;
}

llvm::Constant* Parser::ParseConstantArrayInitializer(Type* type,
                                                      std::int32_t designated) {
  std::int64_t index{};
  auto has_brace{Try(Tag::kLeftBrace)};
  // 可能为零或 -1
  auto size{type->ArrayGetNumElements()};
  size = (size == -1 ? size + 1 : size);
  auto zero{GetConstantZero(type->ArrayGetElementType()->GetLLVMType())};
  std::vector<llvm::Constant*> val(size, zero);

  while (true) {
    if (Test(Tag::kRightBrace)) {
      if (has_brace) {
        Next();
      }
      return llvm::ConstantArray::get(
          llvm::cast<llvm::ArrayType>(type->GetLLVMType()), val);
    }

    if (!designated && !has_brace &&
        (Test(Tag::kPeriod) || Test(Tag::kLeftSquare))) {
      // put ',' back
      PutBack();
      return llvm::ConstantArray::get(
          llvm::cast<llvm::ArrayType>(type->GetLLVMType()), val);
    }

    if ((designated = Try(Tag::kLeftSquare))) {
      auto expr{ParseAssignExpr()};
      if (!expr->GetType()->IsIntegerTy()) {
        Error(expr, "expect integer type");
      }

      index = CalcConstantExpr{}.CalcInteger(expr);
      Expect(Tag::kRightSquare);

      if (type->IsComplete() && index >= type->ArrayGetNumElements()) {
        Error(expr, "array designator index {} exceeds array bounds", index);
      }
    }

    if (size) {
      val[index] = ParseConstantInitializer(type->ArrayGetElementType(),
                                            designated, false);
    } else {
      if (index >= static_cast<std::int64_t>(std::size(val))) {
        val.insert(std::end(val), index - std::size(val), zero);
        val.push_back(ParseConstantInitializer(type->ArrayGetElementType(),
                                               designated, false));
      } else {
        val[index] = ParseConstantInitializer(type->ArrayGetElementType(),
                                              designated, false);
      }
    }

    designated = false;
    ++index;

    if (type->IsComplete() && index >= type->ArrayGetNumElements()) {
      break;
    }

    if (!type->IsComplete()) {
      type->ArraySetNumElements(std::max(index, type->ArrayGetNumElements()));
    }

    if (!Try(Tag::kComma)) {
      if (has_brace) {
        Expect(Tag::kRightBrace);
      }
      return llvm::ConstantArray::get(
          llvm::cast<llvm::ArrayType>(type->GetLLVMType()), val);
    }
  }

  if (has_brace) {
    Try(Tag::kComma);
    if (!Try(Tag::kRightBrace)) {
      Error(Peek(), "excess elements in array initializer");
    }
  }

  return llvm::ConstantArray::get(
      llvm::cast<llvm::ArrayType>(type->GetLLVMType()), val);
}

llvm::Constant* Parser::ParseConstantLiteralInitializer(Type* type) {
  if (!type->ArrayGetElementType()->IsIntegerTy()) {
    return nullptr;
  }

  auto has_brace{Try(Tag::kLeftBrace)};
  if (!Test(Tag::kStringLiteral)) {
    if (has_brace) {
      PutBack();
    }
    return nullptr;
  }

  auto str_node{ParseStringLiteral()};

  if (has_brace) {
    Try(Tag::kComma);
    Expect(Tag::kRightBrace);
  }

  if (!type->IsComplete()) {
    type->ArraySetNumElements(str_node->GetType()->ArrayGetNumElements());
    type->SetComplete(true);
  }

  if (str_node->GetType()->ArrayGetNumElements() >
      type->ArrayGetNumElements()) {
    Error(str_node->GetLoc(),
          "initializer-string for char array is too long '{}' to '{}",
          str_node->GetType()->ArrayGetNumElements(),
          type->ArrayGetNumElements());
  }

  if (str_node->GetType()->ArrayGetElementType()->GetWidth() !=
      type->ArrayGetElementType()->GetWidth()) {
    Error(str_node->GetLoc(), "Different character types '{}' vs '{}",
          str_node->GetType()->ArrayGetElementType()->ToString(),
          type->ArrayGetElementType()->ToString());
  }

  return str_node->GetArr();
}

llvm::Constant* Parser::ParseConstantStructInitializer(Type* type,
                                                       bool designated) {
  auto has_brace{Try(Tag::kLeftBrace)};
  auto member_iter{std::begin(type->StructGetMembers())};
  std::vector<llvm::Constant*> val;
  bool is_struct{type->IsStructTy()};

  if (is_struct) {
    for (const auto& member : type->StructGetMembers()) {
      val.push_back(GetConstantZero(member->GetType()->GetLLVMType()));
    }
  } else {
    val.push_back(
        GetConstantZero(type->GetLLVMType()->getStructElementType(0)));
  }

  while (true) {
    if (Test(Tag::kRightBrace)) {
      if (has_brace) {
        Next();
      }
      return llvm::ConstantStruct::get(
          llvm::cast<llvm::StructType>(type->GetLLVMType()), val);
    }

    if (!designated && !has_brace &&
        (Test(Tag::kPeriod) || Test(Tag::kLeftSquare))) {
      PutBack();
      return llvm::ConstantStruct::get(
          llvm::cast<llvm::StructType>(type->GetLLVMType()), val);
    }

    if ((designated = Try(Tag::kPeriod))) {
      auto tok{Expect(Tag::kIdentifier)};
      auto name{tok.GetIdentifier()};

      if (!type->StructGetMember(name)) {
        Error(tok, "member '{}' not found", name);
      }

      member_iter = GetStructDesignator(type, name);
    }

    if (member_iter == std::end(type->StructGetMembers())) {
      break;
    }

    if ((*member_iter)->IsAnonymous()) {
      if (designated) {
        PutBack();
        PutBack();
      }
    }

    if (is_struct) {
      val[member_iter - std::begin(type->StructGetMembers())] =
          ParseConstantInitializer((*member_iter)->GetType(), designated,
                                   false);
    } else {
      val.front() = ParseConstantInitializer((*member_iter)->GetType(),
                                             designated, false);
    }

    designated = false;
    ++member_iter;

    if (!type->IsStructTy()) {
      break;
    }

    if (!has_brace && member_iter == std::end(type->StructGetMembers())) {
      break;
    }

    if (!Try(Tag::kComma)) {
      if (has_brace) {
        Expect(Tag::kRightBrace);
      }
      return llvm::ConstantStruct::get(
          llvm::cast<llvm::StructType>(type->GetLLVMType()), val);
    }
  }

  if (has_brace) {
    Try(Tag::kComma);
    if (!Try(Tag::kRightBrace)) {
      Error(Peek(), "excess members in struct initializer");
    }
  }

  return llvm::ConstantStruct::get(
      llvm::cast<llvm::StructType>(type->GetLLVMType()), val);
}

/*
 * GNU 扩展
 */
// attribute-specifier:
//  __ATTRIBUTE__ '(' '(' attribute-list-opt ')' ')'
//
// attribute-list:
//  attribute-opt
//  attribute-list ',' attribute-opt
//
// attribute:
//  attribute-name
//  attribute-name '(' ')'
//  attribute-name '(' parameter-list ')'
//
// attribute-name:
//  identifier
//
// parameter-list:
//  identifier
//  identifier ',' expression-list
//  expression-list-opt
//
// expression-list:
//  expression
//  expression-list ',' expression
// 可以有多个
void Parser::TryParseAttributeSpec() {
  while (Try(Tag::kAttribute)) {
    Expect(Tag::kLeftParen);
    Expect(Tag::kLeftParen);

    ParseAttributeList();

    Expect(Tag::kRightParen);
    Expect(Tag::kRightParen);
  }
}

void Parser::ParseAttributeList() {
  while (!Test(Tag::kRightParen)) {
    ParseAttribute();

    if (!Test(Tag::kRightParen)) {
      Expect(Tag::kComma);
    }
  }
}

void Parser::ParseAttribute() {
  Expect(Tag::kIdentifier);

  if (Try(Tag::kLeftParen)) {
    ParseAttributeParamList();
    Expect(Tag::kRightParen);
  }
}

void Parser::ParseAttributeParamList() {
  if (Try(Tag::kIdentifier)) {
    if (Try(Tag::kComma)) {
      ParseAttributeExprList();
    }
  } else {
    ParseAttributeExprList();
  }
}

void Parser::ParseAttributeExprList() {
  while (!Test(Tag::kRightParen)) {
    ParseExpr();

    if (!Test(Tag::kRightParen)) {
      Expect(Tag::kComma);
    }
  }
}

void Parser::TryParseAsm() {
  if (Try(Tag::kAsm)) {
    Expect(Tag::kLeftParen);
    ParseStringLiteral();
    Expect(Tag::kRightParen);
  }
}

QualType Parser::ParseTypeof() {
  Expect(Tag::kLeftParen);

  QualType type;

  if (!IsTypeName(Peek())) {
    auto expr{ParseExpr()};
    type = expr->GetQualType();
  } else {
    type = ParseTypeName();
  }

  Expect(Tag::kRightParen);

  return type;
}

Expr* Parser::TryParseStmtExpr() {
  if (Try(Tag::kLeftParen)) {
    if (Test(Tag::kLeftBrace)) {
      return ParseStmtExpr();
    } else {
      PutBack();
    }
  }

  return nullptr;
}

Expr* Parser::ParseStmtExpr() {
  auto block{ParseCompoundStmt()};
  Expect(Tag::kRightParen);
  return MakeAstNode<StmtExpr>(block->GetLoc(), block);
}

Expr* Parser::ParseTypeid() {
  auto token{Expect(Tag::kLeftParen)};
  auto expr{ParseExpr()};
  Expect(Tag::kRightParen);

  auto str{expr->GetType()->ToString()};
  return MakeAstNode<StringLiteralExpr>(token, str);
}

/*
 * built in
 */
Expr* Parser::ParseOffsetof() {
  Expect(Tag::kLeftParen);

  auto token{Peek()};
  if (!IsTypeName(token)) {
    Error(token, "expect type name");
  }
  auto type{ParseTypeName()};

  Expect(Tag::kComma);
  auto name{Expect(Tag::kIdentifier).GetIdentifier()};
  Expect(Tag::kRightParen);

  return MakeAstNode<ConstantExpr>(
      token, ArithmeticType::Get(kLong | kUnsigned),
      static_cast<std::uint64_t>(type->StructGetMember(name)->GetOffset()));
}

Expr* Parser::ParseHugeVal() {
  auto tok{Expect(Tag::kLeftParen)};
  Expect(Tag::kRightParen);

  return MakeAstNode<ConstantExpr>(
      tok, ArithmeticType::Get(kDouble),
      std::to_string(std::numeric_limits<double>::infinity()));
}

Expr* Parser::ParseInff() {
  auto tok{Expect(Tag::kLeftParen)};
  Expect(Tag::kRightParen);

  return MakeAstNode<ConstantExpr>(
      tok, ArithmeticType::Get(kFloat),
      std::to_string(std::numeric_limits<float>::infinity()));
}

void Parser::AddBuiltin() {
  auto loc{unit_->GetLoc()};

  auto va_list{StructType::Get(true, "__va_list_tag", scope_)};
  va_list->AddMember(
      MakeAstNode<ObjectExpr>(loc, "gp_offset", ArithmeticType::Get(kInt)));
  va_list->AddMember(
      MakeAstNode<ObjectExpr>(loc, "fp_offset", ArithmeticType::Get(kInt)));
  va_list->AddMember(MakeAstNode<ObjectExpr>(
      loc, "overflow_arg_area", PointerType::Get(VoidType::Get())));
  va_list->AddMember(MakeAstNode<ObjectExpr>(
      loc, "reg_save_area", PointerType::Get(VoidType::Get())));
  va_list->SetComplete(true);

  scope_->InsertUsual(MakeAstNode<IdentifierExpr>(
      loc, "__builtin_va_list", ArrayType::Get(va_list, 1), kNone, true));

  auto param1{MakeAstNode<ObjectExpr>(loc, "", va_list->GetPointerTo())};
  auto param2{MakeAstNode<ObjectExpr>(loc, "", ArithmeticType::Get(kInt))};

  auto start{FunctionType::Get(VoidType::Get(), {param1, param2})};
  start->SetName("__builtin_va_start");
  auto end{FunctionType::Get(VoidType::Get(), {param1})};
  end->SetName("__builtin_va_end");
  auto arg{FunctionType::Get(VoidType::Get(), {param1})};
  arg->SetName("__builtin_va_arg_sub");
  auto copy{FunctionType::Get(VoidType::Get(), {param1, param1})};
  copy->SetName("__builtin_va_copy");

  scope_->InsertUsual(MakeAstNode<IdentifierExpr>(loc, "__builtin_va_start",
                                                  start, kExternal, false));
  scope_->InsertUsual(MakeAstNode<IdentifierExpr>(loc, "__builtin_va_end", end,
                                                  kExternal, false));
  scope_->InsertUsual(MakeAstNode<IdentifierExpr>(loc, "__builtin_va_arg_sub",
                                                  arg, kExternal, false));
  scope_->InsertUsual(MakeAstNode<IdentifierExpr>(loc, "__builtin_va_copy",
                                                  copy, kExternal, false));

  auto sync_synchronize{FunctionType::Get(VoidType::Get(), {})};
  sync_synchronize->FuncSetName("__sync_synchronize");
  scope_->InsertUsual(MakeAstNode<IdentifierExpr>(
      loc, "__sync_synchronize", sync_synchronize, kExternal, false));

  auto param3{
      MakeAstNode<ObjectExpr>(loc, "", ArithmeticType::Get(kLong | kUnsigned))};
  auto alloca{
      FunctionType::Get(ArithmeticType::Get(kChar)->GetPointerTo(), {param3})};
  alloca->FuncSetName("__builtin_alloca");
  scope_->InsertUsual(MakeAstNode<IdentifierExpr>(loc, "__builtin_alloca",
                                                  alloca, kExternal, false));
}

}  // namespace kcc
