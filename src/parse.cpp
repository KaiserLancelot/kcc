//
// Created by kaiser on 2019/10/31.
//

#include "parse.h"

#include "calc.h"
#include "error.h"
#include "lex.h"

namespace kcc {

Parser::Parser(std::vector<Token> tokens) : tokens_{std::move(tokens)} {}

std::shared_ptr<TranslationUnit> Parser::ParseTranslationUnit() {
  auto unit{MakeAstNode<TranslationUnit>()};
  curr_scope_ = std::make_shared<Scope>(nullptr, kFile);

  while (HasNext()) {
    if (auto decl{ParseExternalDecl()}; decl) {
      unit->AddExtDecl(decl);
    }
  }

  return unit;
}

bool Parser::HasNext() { return !Peek().TagIs(Tag::kEof); }

Token Parser::Peek() { return tokens_[index_]; }

Token Parser::Next() { return tokens_[index_++]; }

void Parser::PutBack() { --index_; }

bool Parser::Test(Tag tag) { return Peek().TagIs(tag); }

bool Parser::Try(Tag tag) {
  if (Test(tag)) {
    Next();
    return true;
  } else {
    return false;
  }
}

void Parser::ParseStaticAssertDecl() {
  Expect(Tag::kLeftParen);
  auto expr{ParseConstantExpr()};
  Expect(Tag::kComma);

  auto msg{ParseStringLiteral(false)->GetStrVal()};
  Expect(Tag::kRightParen);
  Expect(Tag::kSemicolon);

  if (!CalcExpr<std::int32_t>{}.Calc(expr)) {
    Error(expr->GetToken(), "static_assert failed \"{}\"", msg);
  }
}

Token Parser::PeekPrev() { return tokens_[index_ - 1]; }

Token Parser::Expect(Tag tag) {
  if (!Test(tag)) {
    Error(tag, Peek());
  } else {
    return Next();
  }
}

std::shared_ptr<Expr> Parser::ParseExpr() { return ParseCommaExpr(); }

std::shared_ptr<Expr> Parser::ParseCommaExpr() {
  auto lhs{ParseAssignExpr()};

  auto tok{Peek()};
  while (Try(Tag::kComma)) {
    auto rhs{ParseAssignExpr()};
    lhs = MakeAstNode<BinaryOpExpr>(tok, lhs, rhs);

    tok = Peek();
  }

  return lhs;
}

std::shared_ptr<Expr> Parser::ParseAssignExpr() {
  auto lhs{ParseConditionExpr()};
  std::shared_ptr<Expr> rhs;

  auto tok{Next()};

  switch (tok.GetTag()) {
    case Tag::kEqual:
      rhs = ParseAssignExpr();
      break;
    case Tag::kStarEqual:
      rhs = ParseAssignExpr();
      rhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kStar, lhs, rhs);
      break;
    case Tag::kSlashEqual:
      rhs = ParseAssignExpr();
      rhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kSlash, lhs, rhs);
      break;
    case Tag::kPercentEqual:
      rhs = ParseAssignExpr();
      rhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kPercent, lhs, rhs);
      break;
    case Tag::kPlusEqual:
      rhs = ParseAssignExpr();
      rhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kPlus, lhs, rhs);
      break;
    case Tag::kMinusEqual:
      rhs = ParseAssignExpr();
      rhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kMinus, lhs, rhs);
      break;
    case Tag::kLessLessEqual:
      rhs = ParseAssignExpr();
      rhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kLessLess, lhs, rhs);
      break;
    case Tag::kGreaterGreaterEqual:
      rhs = ParseAssignExpr();
      rhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kGreaterGreater, lhs, rhs);
      break;
    case Tag::kAmpEqual:
      rhs = ParseAssignExpr();
      rhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kAmp, lhs, rhs);
      break;
    case Tag::kCaretEqual:
      rhs = ParseAssignExpr();
      rhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kCaret, lhs, rhs);
      break;
    case Tag::kPipeEqual:
      rhs = ParseAssignExpr();
      rhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kPipe, lhs, rhs);
      break;
    default: {
      PutBack();
      return lhs;
    }
  }

  return MakeAstNode<BinaryOpExpr>(tok, Tag::kEqual, lhs, rhs);
}

std::shared_ptr<Expr> Parser::ParseConditionExpr() {
  auto cond{ParseLogicalOrExpr()};
  auto tok{Peek()};

  // 条件运算符 ? 与 : 之间的表达式分析为如同加括号，忽略其相对于 ?: 的优先级
  if (Try(Tag::kQuestion)) {
    // GNU 扩展
    // a ?: b 相当于 a ? a: c
    auto true_expr{Test(Tag::kColon) ? cond : ParseExpr()};
    Expect(Tag::kColon);
    auto false_expr{ParseExpr()};

    return MakeAstNode<ConditionOpExpr>(tok, cond, true_expr, false_expr);
  }

  return cond;
}

std::shared_ptr<Expr> Parser::ParseLogicalOrExpr() {
  auto lhs{ParseLogicalAndExpr()};
  auto tok{Peek()};

  while (Try(Tag::kPipePipe)) {
    auto rhs{ParseLogicalAndExpr()};
    lhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kPipePipe, lhs, rhs);

    tok = Peek();
  }

  return lhs;
}

std::shared_ptr<Expr> Parser::ParseLogicalAndExpr() {
  auto lhs{ParseBitwiseOrExpr()};
  auto tok{Peek()};

  while (Try(Tag::kAmpAmp)) {
    auto rhs{ParseBitwiseOrExpr()};
    lhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kAmpAmp, lhs, rhs);

    tok = Peek();
  }

  return lhs;
}

std::shared_ptr<Expr> Parser::ParseBitwiseOrExpr() {
  auto lhs{ParseBitwiseXorExpr()};
  auto tok{Peek()};

  while (Try(Tag::kPipe)) {
    auto rhs{ParseBitwiseXorExpr()};
    lhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kPipe, lhs, rhs);

    tok = Peek();
  }

  return lhs;
}

std::shared_ptr<Expr> Parser::ParseBitwiseXorExpr() {
  auto lhs{ParseBitwiseAndExpr()};
  auto tok{Peek()};

  while (Try(Tag::kCaret)) {
    auto rhs{ParseBitwiseAndExpr()};
    lhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kCaret, lhs, rhs);

    tok = Peek();
  }

  return lhs;
}

std::shared_ptr<Expr> Parser::ParseBitwiseAndExpr() {
  auto lhs{ParseEqualityExpr()};
  auto tok{Peek()};

  while (Try(Tag::kAmp)) {
    auto rhs{ParseEqualityExpr()};
    lhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kAmp, lhs, rhs);

    tok = Peek();
  }

  return lhs;
}

std::shared_ptr<Expr> Parser::ParseEqualityExpr() {
  auto lhs{ParseRelationExpr()};
  auto tok{Peek()};

  while (true) {
    if (Try(Tag::kEqual)) {
      auto rhs{ParseRelationExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kEqual, lhs, rhs);
      tok = Peek();
    } else if (Try(Tag::kExclaimEqual)) {
      auto rhs{ParseRelationExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kExclaimEqual, lhs, rhs);
      tok = Peek();
    } else {
      break;
    }
  }

  return lhs;
}

std::shared_ptr<Expr> Parser::ParseRelationExpr() {
  auto lhs{ParseShiftExpr()};
  auto tok{Peek()};

  while (true) {
    if (Try(Tag::kLess)) {
      auto rhs{ParseShiftExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kLess, lhs, rhs);
      tok = Peek();
    } else if (Try(Tag::kLessEqual)) {
      auto rhs{ParseShiftExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kLessEqual, lhs, rhs);
      tok = Peek();
    } else if (Try(Tag::kGreater)) {
      auto rhs{ParseShiftExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kGreater, lhs, rhs);
      tok = Peek();
    } else if (Try(Tag::kGreaterEqual)) {
      auto rhs{ParseShiftExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kGreaterEqual, lhs, rhs);
      tok = Peek();
    } else {
      break;
    }
  }

  return lhs;
}

std::shared_ptr<Expr> Parser::ParseShiftExpr() {
  auto lhs{ParseAdditiveExpr()};
  auto tok{Peek()};

  while (true) {
    if (Try(Tag::kLessLess)) {
      auto rhs{ParseAdditiveExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kLessLess, lhs, rhs);
      tok = Peek();
    } else if (Try(Tag::kGreaterGreater)) {
      auto rhs{ParseAdditiveExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kGreaterGreater, lhs, rhs);
      tok = Peek();
    } else {
      break;
    }
  }

  return lhs;
}

std::shared_ptr<Expr> Parser::ParseAdditiveExpr() {
  auto lhs{ParseMultiplicativeExpr()};
  auto tok{Peek()};

  while (true) {
    if (Try(Tag::kPlus)) {
      auto rhs{ParseMultiplicativeExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kPlus, lhs, rhs);
      tok = Peek();
    } else if (Try(Tag::kMinus)) {
      auto rhs{ParseMultiplicativeExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kMinus, lhs, rhs);
      tok = Peek();
    } else {
      break;
    }
  }

  return lhs;
}

std::shared_ptr<Expr> Parser::ParseMultiplicativeExpr() {
  auto lhs{ParseCastExpr()};
  auto tok{Peek()};

  while (true) {
    if (Try(Tag::kStar)) {
      auto rhs{ParseCastExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kStar, lhs, rhs);
      tok = Peek();
    } else if (Try(Tag::kSlash)) {
      auto rhs{ParseCastExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kSlash, lhs, rhs);
      tok = Peek();
    } else if (Try(Tag::kPercent)) {
      auto rhs{ParseCastExpr()};
      lhs = MakeAstNode<BinaryOpExpr>(tok, Tag::kPercent, lhs, rhs);
      tok = Peek();
    } else {
      break;
    }
  }

  return lhs;
}

std::shared_ptr<CompoundStmt> Parser::ParseDecl(bool maybe_func_def) {
  if (Try(Tag::kStaticAssert)) {
    ParseStaticAssertDecl();
    return nullptr;
  } else {
    auto base_type{ParseDeclSpec(false)};

    if (Try(Tag::kSemicolon)) {
      return nullptr;
    } else {
      if (maybe_func_def) {
        return ParseInitDeclaratorList(base_type);
      } else {
        auto ret{ParseInitDeclaratorList(base_type)};
        Expect(Tag::kSemicolon);
        return ret;
      }
    }
  }
}

std::shared_ptr<CompoundStmt> Parser::ParseInitDeclaratorList(
    std::shared_ptr<Type>& base_type) {
  std::vector<std::shared_ptr<Stmt>> stmts;

  do {
    if (auto decl{ParseInitDeclarator(base_type)}; decl) {
      stmts.push_back(decl);
    }
  } while (Try(Tag::kComma));

  return MakeAstNode<CompoundStmt>(stmts, curr_scope_);
}

std::shared_ptr<Declaration> Parser::ParseInitDeclarator(
    std::shared_ptr<Type>& base_type) {
  Token tok;
  ParseDeclarator(tok, base_type);

  auto decl{MakeDeclarator(tok, base_type)};

  if (decl && Try(Tag::kEqual) && decl->IsObj()) {
    decl->AddInits(ParseInitDeclaratorSub(decl->GetIdent()));
  }

  return decl;
}

void Parser::ParseDeclarator(Token& tok, std::shared_ptr<Type>& base_type) {
  ParsePointer(base_type);
  ParseDirectDeclarator(tok, base_type);
}

void Parser::ParseDirectDeclarator(Token& tok,
                                   std::shared_ptr<Type>& base_type) {
  if (Test(Tag::kIdentifier)) {
    tok = Next();
    ParseDirectDeclaratorTail(base_type);
  } else if (Try(Tag::kLeftParen)) {
    auto begin{index_};
    auto temp{ArithmeticType::Get(kInt)};
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

// 不支持在 struct / union 中使用 _Alignas
// 不支持在函数参数列表中使用 storage class specifier
// 在 struct / union 中和函数参数声明中只能使用 type specifier 和 type qualifier
std::shared_ptr<Type> Parser::ParseDeclSpec(bool only_spec_and_qual) {
#define CheckAndSetStorageClassSpec(spec)                       \
  if (storage_class_spec != 0) {                                \
    Error(tok, "duplicated storage class specifier");           \
  } else if (only_spec_and_qual) {                              \
    Error(tok, "storage class specifier are not allowed here"); \
  }                                                             \
  storage_class_spec |= spec;

#define CheckAndSetFuncSpec(spec)                                       \
  if (func_spec & spec) {                                               \
    Warning(tok, "duplicate function specifier declaration specifier"); \
  } else if (only_spec_and_qual) {                                      \
    Error(tok, "function specifiers are not allowed here");             \
  }                                                                     \
  func_spec |= spec;

#define ERROR Error(tok, "two or more data types in declaration specifiers");

  std::uint32_t type_spec{}, type_qualifiers{}, storage_class_spec{},
      func_spec{};
  std::int32_t align{};

  Token tok;
  std::shared_ptr<Type> type;

  while (true) {
    tok = Next();

    switch (tok.GetTag()) {
      case Tag::kExtension:
        break;

      // Storage Class Specifier, 至多有一个
      case Tag::kTypedef:
        CheckAndSetStorageClassSpec(kTypedef) break;
      case Tag::kExtern:
        CheckAndSetStorageClassSpec(kExtern) break;
      case Tag::kStatic:
        CheckAndSetStorageClassSpec(kStatic) break;
      case Tag::kAuto:
        CheckAndSetStorageClassSpec(kAuto) break;
      case Tag::kRegister:
        CheckAndSetStorageClassSpec(kRegister) break;
      case Tag::kThreadLocal:
        Error(tok, "Does not support _Thread_local");

        // Type specifier
      case Tag::kVoid:
        if (type_spec) {
          ERROR
        }
        type_spec |= kVoid;
        break;
      case Tag::kChar:
        if (type_spec & ~kCompChar) {
          ERROR
        }
        type_spec |= kChar;
        break;
      case Tag::kShort:
        if (type_spec & ~kCompShort) {
          ERROR
        }
        type_spec |= kShort;
        break;
      case Tag::kInt:
        if (type_spec & ~kCompInt) {
          ERROR
        }
        type_spec |= kInt;
        break;
      case Tag::kLong:
        if (type_spec & ~kCompLong) {
          ERROR
        }

        if (type_spec & kLong) {
          type_spec &= ~kLong;
          type_spec |= kLongLong;
        } else {
          type_spec |= kLong;
        }
        break;
      case Tag::kFloat:
        if (type_spec) {
          ERROR
        }
        type_spec |= kFloat;
        break;
      case Tag::kDouble:
        if (type_spec & ~kCompDouble) {
          ERROR
        }
        type_spec |= kDouble;
        break;
      case Tag::kSigned:
        if (type_spec & ~kCompSigned) {
          ERROR
        }
        type_spec |= kSigned;
        break;
      case Tag::kUnsigned:
        if (type_spec & ~kCompUnsigned) {
          ERROR
        }
        type_spec |= kUnsigned;
        break;
      case Tag::kBool:
        if (type_spec) {
          ERROR
        }
        type_spec |= kBool;
        break;
      case Tag::kStruct:
      case Tag::kUnion:
        if (type_spec) {
          ERROR
        }
        type = ParseStructUnionSpec(tok.GetTag() == Tag::kStruct);
        type_spec |= kStructUnionSpec;
        break;
      case Tag::kEnum:
        if (type_spec) {
          ERROR
        }
        type = ParseEnumSpec();
        type_spec |= kEnumSpec;
        break;
      case Tag::kComplex:
        Error(tok, "Does not support _Complex");
      case Tag::kAtomic:
        Error(tok, "Does not support _Atomic");

        // Type qualifier
      case Tag::kConst:
        type_qualifiers |= kConst;
        break;
      case Tag::kRestrict:
        type_qualifiers |= kRestrict;
        break;
      case Tag::kVolatile:
        Error(tok, "Does not support volatile");

        // Function specifier
      case Tag::kInline:
        CheckAndSetFuncSpec(kInline) break;
      case Tag::kNoreturn:
        CheckAndSetFuncSpec(kNoreturn) break;

      case Tag::kAlignas:
        if (only_spec_and_qual) {
          Error(tok, "_Alignas are not allowed here");
        }
        align = std::max(ParseAlignas(), align);
        break;

      default: {
        if (type_spec == 0 && IsTypeName(tok)) {
          auto ident{curr_scope_->FindNormal(tok.GetStr())};
          type = ident->GetType();
          // TODO ???
          type_spec |= kTypedefName;
        } else {
          goto finish;
        }
      }
    }
  }

finish:
  PutBack();

  if (!type_spec) {
    Error(tok, "type specifier missing: {}", tok.GetStr());
  }

  if (!type) {
    type = Type::Get(type_spec);
  }
  type->SetAlign(align);
  type->SetTypeQualifiers(type_qualifiers);
  type->SetFuncSpec(func_spec);
  type->SetStorageClassSpec(storage_class_spec);

  TryAttributeSpec();

  return type;

#undef CheckAndSetStorageClassSpec
#undef CheckAndSetFuncSpec
#undef ERROR
}

std::shared_ptr<Type> Parser::ParseStructUnionSpec(bool is_struct) {
  TryAttributeSpec();

  auto tok{Peek()};
  std::string tag_name;

  if (Try(Tag::kIdentifier)) {
    tag_name = tok.GetStr();
    // 定义
    if (Try(Tag::kLeftBrace)) {
      auto tag{curr_scope_->FindTagInCurrScope(tag_name)};
      // 无前向声明
      if (!tag) {
        auto type{StructType::Get(is_struct, true, curr_scope_)};
        type->SetName(tag_name);
        auto ident{MakeAstNode<Identifier>(tok, type, kNone, true)};
        ParseStructDeclList(type);
        curr_scope_->InsertTag(tag_name, ident);
        Expect(Tag::kRightBrace);
        return type;
      } else {
        if (tag->GetType()->IsComplete()) {
          Error(tok, "redefinition:{}", tag_name);
        } else {
          ParseStructDeclList(
              std::dynamic_pointer_cast<StructType>(tag->GetType()));
          Expect(Tag::kRightBrace);
          return tag->GetType();
        }
      }
    } else {
      // 可能是前向声明或普通的声明
      auto tag{curr_scope_->FindTag(tag_name)};

      if (tag) {
        return tag->GetType();
      }

      auto type{StructType::Get(is_struct, true, curr_scope_)};
      type->SetName(tag_name);
      auto ident{MakeAstNode<Identifier>(tok, type, kNone, true)};
      curr_scope_->InsertTag(tag_name, ident);
      return type;
    }
  } else {
    // 无标识符只能是定义
    Expect(Tag::kLeftBrace);

    auto type{StructType::Get(is_struct, false, curr_scope_)};
    ParseStructDeclList(type);

    Expect(Tag::kRightBrace);
    return type;
  }
}

void Parser::ParseStructDeclList(std::shared_ptr<StructType> type) {
  // TODO 为什么会指向相同的地方
  auto scope_backup{*curr_scope_};
  *curr_scope_ = *type->GetScope();

  while (!Test(Tag::kRightBrace)) {
    if (!HasNext()) {
      Error(PeekPrev(), "premature end of input");
    }

    if (Try(Tag::kStaticAssert)) {
      ParseStaticAssertDecl();
    } else {
      auto base_type{ParseDeclSpec(true)};

      do {
        Token tok;
        ParseDeclarator(tok, base_type);

        TryAttributeSpec();

        // TODO 位域

        // 可能是匿名 struct / union
        if (std::empty(tok.GetStr())) {
          if (base_type->IsStructTy() && !base_type->HasStructName()) {
            auto anony{MakeAstNode<Object>(Peek(), base_type, kNone, true)};
            type->MergeAnonymous(anony);
            continue;
          } else {
            Error(Peek(), "declaration does not declare anything");
          }
        } else {
          std::string name{tok.GetStr()};

          if (type->GetMember(name)) {
            Error(Peek(), "duplicate member:{}", name);
          } else if (base_type->IsArrayTy() && !base_type->IsComplete()) {
            // 可能是柔性数组
            if (type->IsStruct() && std::size(type->GetMembers()) > 0) {
              auto member{MakeAstNode<Object>(Peek(), base_type)};
              type->AddMember(member);
              // 必须是最后一个成员
              Expect(Tag::kSemicolon);
              Expect(Tag::kRightBrace);

              goto finalize;
            } else {
              Error(Peek(), "field '{}' has incomplete type", name);
            }
          } else if (base_type->IsFunctionTy()) {
            Error(Peek(), "field '{}' declared as a function", name);
          } else {
            auto member{MakeAstNode<Object>(tok, base_type)};
            type->AddMember(member);
          }
        }
      } while (Try(Tag::kComma));

      Expect(Tag::kSemicolon);
    }
  }

finalize:
  TryAttributeSpec();

  type->SetComplete(true);

  // struct / union 中的 tag 的作用域与该 struct / union 所在的作用域相同
  //  for (const auto& [name, tag] : curr_scope_->AllTagInCurrScope()) {
  //    if (scope_backup.FindTagInCurrScope(name)) {
  //      Error(tag->GetToken(), "redefinition of tag {}", tag->GetName());
  //    } else {
  //      scope_backup.InsertTag(name, tag);
  //    }
  //  }

  *type->GetScope() = *curr_scope_;
  *curr_scope_ = scope_backup;
}

std::shared_ptr<Type> Parser::ParseEnumSpec() {
  TryAttributeSpec();

  std::string tag_name;
  auto tok{Peek()};

  if (Try(Tag::kIdentifier)) {
    tag_name = tok.GetStr();
    // 定义
    if (Try(Tag::kLeftBrace)) {
      auto tag{curr_scope_->FindTagInCurrScope(tag_name)};

      if (!tag) {
        auto type{ArithmeticType::Get(32)};
        auto ident{MakeAstNode<Identifier>(tok, type, kNone, true)};
        ParseEnumerator(type);
        curr_scope_->InsertTag(tag_name, ident);
        Expect(Tag::kRightBrace);
        return type;
      } else {
        // 不允许前向声明，如果当前作用域中有 tag 则就是重定义
        Error(tok, "redefinition of enumeration tag: {}", tag_name);
      }
    } else {
      // 只能是普通声明
      auto tag{curr_scope_->FindTag(tag_name)};
      if (tag) {
        return tag->GetType();
      } else {
        Error(tok, "unknown enumeration: {}", tag_name);
      }
    }
  } else {
    Expect(Tag::kLeftBrace);
    auto type{ArithmeticType::Get(32)};
    ParseEnumerator(type);
    Expect(Tag::kRightBrace);
    return type;
  }
}

void Parser::ParseEnumerator(std::shared_ptr<Type> type) {
  std::int32_t val{};

  do {
    auto tok{Expect(Tag::kIdentifier)};
    TryAttributeSpec();
    auto name{tok.GetStr()};
    auto ident{curr_scope_->FindNormalInCurrScope(name)};

    if (ident) {
      Error(tok, "redefinition of enumerator '{}'", name);
    }

    if (Try(Tag::kEqual)) {
      auto expr{ParseConstantExpr()};
      val = CalcExpr<std::int32_t>{}.Calc(expr);
    }

    auto enumer{MakeAstNode<Enumerator>(tok, val)};
    ++val;
    curr_scope_->InsertNormal(name, enumer);

    Try(Tag::kComma);
  } while (!Test(Tag::kRightBrace));

  type->SetComplete(true);
}

std::int32_t Parser::ParseAlignas() {
  Expect(Tag::kLeftParen);

  std::int32_t align;
  auto tok{Peek()};

  if (IsTypeName(tok)) {
    auto type{ParseTypeName()};
    align = type->Align();
  } else {
    auto expr{ParseConstantExpr()};
    align = CalcExpr<std::int32_t>{}.Calc(expr);
  }

  Expect(Tag::kRightParen);

  // TODO 获取完整声明后检查 align 是否小于类型的 width
  if (align < 0 || ((align - 1) & align)) {
    Error(tok, "requested alignment is not a power of 2");
  }

  return align;
}

// 只支持
// direct-declarator[assignment-expression-opt]
// direct-declarator[parameter-type-list]
void Parser::ParseDirectDeclaratorTail(std::shared_ptr<Type>& base_type) {
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

    auto new_type{ArrayType::Get(base_type, len)};
    new_type->SetSpec(base_type->GetSpec());
    base_type = new_type;
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

    auto new_type{FunctionType::Get(base_type, params, var_args)};
    new_type->SetSpec(base_type->GetSpec());
    base_type = new_type;
  }
}

std::size_t Parser::ParseArrayLength() {
  auto expr{ParseAssignExpr()};

  if (!expr->GetType()->IsIntegerTy()) {
    Error(expr->GetToken(), "The array size must be an integer");
  }

  // 不支持变长数组
  auto len{CalcExpr<std::int32_t>{}.Calc(expr)};

  if (len <= 0) {
    Error(expr->GetToken(), "Array size must be greater than 0");
  }

  return len;
}

void Parser::EnterProto() {
  curr_scope_ = std::make_shared<Scope>(curr_scope_, kFuncProto);
}

void Parser::ExitProto() { curr_scope_ = curr_scope_->GetParent(); }

std::pair<std::vector<std::shared_ptr<Object>>, bool>
Parser::ParseParamTypeList() {
  if (Test(Tag::kRightParen)) {
    Warning(
        Next(),
        "The parameter list is not allowed to be empty, you should use void");
    return {{}, false};
  }

  auto param{ParseParamDecl()};
  if (param->GetType()->IsVoidTy()) {
    return {{}, false};
  }

  std::vector<std::shared_ptr<Object>> params;
  params.push_back(param);

  while (Try(Tag::kComma)) {
    if (Try(Tag::kEllipsis)) {
      return {params, true};
    }

    param = ParseParamDecl();
    if (param->GetType()->IsVoidTy()) {
      Error("error");
    }
    params.push_back(param);
  }

  return {params, false};
}

// declaration-specifiers declarator
// declaration-specifiers abstract-declarator（此时不能是函数定义）
std::shared_ptr<Object> Parser::ParseParamDecl() {
  // 不支持 storage class specifier
  auto base_type{ParseDeclSpec(true)};

  Token tok;
  ParseDeclarator(tok, base_type);
  base_type = Type::MayCast(base_type, true);

  if (std::empty(tok.GetStr())) {
    return MakeAstNode<Object>(Peek(), base_type, kNone, true);
  }

  auto ident{MakeDeclarator(tok, base_type)};

  return std::dynamic_pointer_cast<Object>(ident->GetIdent());
}

bool Parser::IsTypeName(const Token& tok) {
  if (tok.IsTypeSpecQual()) {
    return true;
  } else if (tok.IsIdentifier()) {
    auto ident{curr_scope_->FindNormal(tok)};
    if (ident && ident->IsTypeName()) {
      return true;
    }
  }

  return false;
}

std::shared_ptr<Type> Parser::ParseTypeName() {
  auto base_type{ParseDeclSpec(true)};
  ParseAbstractDeclarator(base_type);
  return base_type;
}

void Parser::ParseAbstractDeclarator(std::shared_ptr<Type>& type) {
  ParsePointer(type);
  ParseDirectAbstractDeclarator(type);
}

void Parser::ParseDirectAbstractDeclarator(std::shared_ptr<Type>& type) {
  Token tok;
  ParseDirectDeclarator(tok, type);
  auto name{tok.GetStr()};

  if (!std::empty(name)) {
    Error(tok, "unexpected identifier '{}'", name);
  }
}

void Parser::ParsePointer(std::shared_ptr<Type>& type) {
  while (Try(Tag::kStar)) {
    auto new_type{type->GetPointerTo()};
    new_type->SetSpec(type->GetSpec());
    type = new_type;
    ParseTypeQualList(type);
  }
}

void Parser::ParseTypeQualList(std::shared_ptr<Type>& type) {
  auto tok{Peek()};

  while (true) {
    if (Try(Tag::kConst)) {
      type->SetConstQualified();
    } else if (Try(Tag::kRestrict)) {
      type->SetRestrictQualified();
    } else if (Try(Tag::kVolatile)) {
      Error(tok, "Does not support volatile");
    } else if (Try(Tag::kAtomic)) {
      Error(tok, "Does not support _Atomic");
    } else {
      break;
    }
  }
}

std::shared_ptr<Expr> Parser::ParseConstantExpr() {
  return ParseConditionExpr();
}

std::shared_ptr<Constant> Parser::ParseStringLiteral(bool handle_escape) {
  auto tok{Expect(Tag::kStringLiteral)};

  auto str{Scanner{tok.GetStr()}.ScanStringLiteral(handle_escape)};
  while (Test(Tag::kStringLiteral)) {
    tok = Next();
    str += Scanner{tok.GetStr()}.ScanStringLiteral(handle_escape);
  }

  str += '\0';

  return MakeAstNode<Constant>(
      tok, ArrayType::Get(ArithmeticType::Get(8), std::size(str)), str);
}

// TODO check
std::shared_ptr<Declaration> Parser::MakeDeclarator(
    const Token& tok, const std::shared_ptr<Type>& type) {
  auto name{tok.GetStr()};

  if (type->IsTypedef()) {
    if (type->HasAlign() != 0) {
      Error(tok, "'_Alignas' attribute only applies to variables and fields");
    }

    auto ident{curr_scope_->FindNormalInCurrScope(name)};
    if (ident) {
      // 如果两次定义的类型兼容是可以的
      if (!type->Compatible(ident->GetType())) {
        Error(tok, "typedef redefinition with different types('{}' vs '{}'",
              type->ToString(), ident->GetType()->ToString());
      } else {
        Warning(tok, "Typedef redefinition");
      }
    } else {
      curr_scope_->InsertNormal(
          tok.GetStr(), MakeAstNode<Identifier>(tok, type, kNone, true));

      return nullptr;
    }
  } else if (type->IsVoidTy()) {
    Error(tok, "variable or field {} declared void", name);
  } else if (type->IsFunctionTy() && !curr_scope_->IsFileScope()) {
    Error(tok, "function definition is not allowed here");
  }

  Linkage linkage;
  if (curr_scope_->IsFileScope()) {
    if (type->IsStatic()) {
      linkage = kInternal;
    } else {
      linkage = kExternal;
    }
  } else {
    linkage = kNone;
  }

  //  auto ident{curr_scope_->FindNormalInCurrScope(tok)};
  //  // 可能是前向声明
  //  // 有链接对象（外部或内部）的声明可以重复
  //  if (ident) {
  //    if (linkage == kNone) {
  //      Error(tok, "redefinition of '{}'", name);
  //    } else if (linkage != ident->GetLinkage()) {
  //      Error(tok, "conflicting linkage '{}'", name);
  //    }
  //
  //    if (!ident->GetType()->IsComplete()) {
  //      ident->GetType()->SetComplete(type->IsComplete());
  //    }
  //  }

  std::shared_ptr<Identifier> ret;
  if (type->IsFunctionTy()) {
    if (type->HasAlign() != 0) {
      Error(tok, "'_Alignas' attribute only applies to variables and fields");
    }
    ret = MakeAstNode<Identifier>(tok, type, linkage, false);
  } else {
    ret = MakeAstNode<Object>(tok, type, linkage, false);
  }

  curr_scope_->InsertNormal(name, ret);

  return MakeAstNode<Declaration>(ret);
}

std::set<Initializer> Parser::ParseInitDeclaratorSub(
    std::shared_ptr<Identifier> ident) {
  if (!curr_scope_->IsFileScope() && ident->GetLinkage() == kExternal) {
    Error(ident->GetToken(), "{} has both 'extern' and initializer",
          ident->GetName());
  }

  if (!ident->GetType()->IsComplete() && !ident->GetType()->IsArrayTy()) {
    Error(ident->GetToken(),
          "variable '{}' has initializer but incomplete type",
          ident->GetName());
  }

  std::set<Initializer> inits;
  // ParseInitializer(inits, ident->GetType(), 0, false, true);
  return inits;
}

std::shared_ptr<ExtDecl> Parser::ParseExternalDecl() {
  std::shared_ptr<CompoundStmt> ext_decl;

  ext_decl = ParseDecl(true);

  if (ext_decl == nullptr) {
    return nullptr;
  }

  TryAsm();
  TryAttributeSpec();

  if (Test(Tag::kLeftBrace)) {
    auto stmt{ext_decl->GetStmts()};
    if (std::size(stmt) != 1) {
      Error(Peek(), "func def error");
    }

    auto ident{
        std::dynamic_pointer_cast<Declaration>(stmt.front())->GetIdent()};
    EnterFunc(ident);

    if (curr_func_def_->GetFuncType()->IsComplete()) {
      Error(ident->GetToken(), "redefinition of {}", ident->GetName());
    }

    ident->GetType()->SetComplete(true);
    for (const auto& param : ident->GetType()->GetFunctionParams()) {
      if (param->Anonymous()) {
        Error(param->GetToken(), "param name omitted");
      }
    }

    curr_func_def_->SetBody(ParseCompoundStmt(ident->GetType()));

    auto ret{curr_func_def_};

    ExitFunc();

    return ret;
  } else {
    Expect(Tag::kSemicolon);
    return ext_decl;
  }
}

std::shared_ptr<Expr> Parser::ParseCastExpr() {
  auto tok{Peek()};

  if (Try(Tag::kLeftParen)) {
    if (!IsTypeName(Peek())) {
      // TODO right?
      auto expr{ParseExpr()};
      Expect(Tag::kRightParen);
      return expr;
    }

    auto type{ParseTypeName()};
    Expect(Tag::kRightBrace);

    // TODO 复合字面量 ???

    return MakeAstNode<TypeCastExpr>(ParseCastExpr(), type);
  } else {
    return ParseUnaryExpr();
  }
}

std::shared_ptr<Expr> Parser::ParsePrimaryExpr() {
  auto tok{Peek()};

  if (Try(Tag::kLeftParen)) {
    auto expr{ParseExpr()};
    Expect(Tag::kRightParen);
    return expr;
  } else if (Peek().IsStringLiteral()) {
    return ParseStringLiteral(true);
  } else if (Peek().IsIdentifier()) {
    Next();
    auto ident{curr_scope_->FindNormal(tok)};
    if (ident) {
      return ident;
    } else {
      Error(tok, "undefined symbol: {}", tok.GetStr());
    }
  } else if (Peek().IsConstant()) {
    return ParseConstant();
  } else if (Try(Tag::kGeneric)) {
    // TODO
    return nullptr;
  } else {
    Error(tok, "{} unexpected", tok.GetStr());
  }
}

std::shared_ptr<Expr> Parser::ParsePostfixExpr() {
  // TODO 复合字面量
  return ParsePostfixExprTail(ParsePrimaryExpr());
}

/*
 * ++ --
 * + - ~
 * !
 * * &
 */
std::shared_ptr<Expr> Parser::ParseUnaryExpr() {
  auto tok{Next()};

  switch (tok.GetTag()) {
    // 默认为前缀
    case Tag::kPlusPlus:
    case Tag::kMinusMinus:
    case Tag::kPlus:
    case Tag::kMinus:
    case Tag::kTilde:
    case Tag::kExclaim:
    case Tag::kStar:
    case Tag::kAmp:
      return MakeAstNode<UnaryOpExpr>(tok, ParseUnaryExpr());
    case Tag::kSizeof:
      return ParseSizeof();
    case Tag::kAlignof:
      return ParseAlignof();
    default:
      PutBack();
      return ParsePostfixExpr();
  }
}

std::shared_ptr<Expr> Parser::ParseSizeof() {
  std::shared_ptr<Type> type;
  auto tok{Peek()};

  if (Try(Tag::kLeftParen)) {
    if (!IsTypeName(Peek())) {
      Error(Peek(), "expect type name");
    }

    type = ParseTypeName();
    Expect(Tag::kRightParen);
  } else {
    auto expr{ParseUnaryExpr()};
    type = expr->GetType();
  }

  if (!type->IsComplete()) {
    Error(tok, "sizeof(incomplete type)");
  }

  return MakeAstNode<Constant>(tok, Type::Get(kLong | kUnsigned),
                               static_cast<std::uint64_t>(type->GetWidth()));
}

std::shared_ptr<Expr> Parser::ParseAlignof() {
  std::shared_ptr<Type> type;
  auto tok{Peek()};

  Expect(Tag::kLeftParen);

  if (!IsTypeName(Peek())) {
    Error(Peek(), "expect type name");
  }

  type = ParseTypeName();
  Expect(Tag::kRightParen);

  return MakeAstNode<Constant>(tok, type->Align());
}

std::shared_ptr<Expr> Parser::ParsePostfixExprTail(std::shared_ptr<Expr> expr) {
  while (true) {
    auto tok{Next()};

    switch (tok.GetTag()) {
      case Tag::kLeftSquare: {
        auto rhs{ParseExpr()};
        Expect(Tag::kLeftSquare);
        return MakeAstNode<UnaryOpExpr>(
            tok, Tag::kStar,
            MakeAstNode<BinaryOpExpr>(tok, Tag::kPlus, expr, rhs));
      }
      case Tag::kLeftParen: {
        std::vector<std::shared_ptr<Expr>> args;
        while (!Try(Tag::kRightParen)) {
          args.push_back(ParseAssignExpr());
          if (!Test(Tag::kRightParen)) {
            Expect(Tag::kComma);
          }
        }
        return MakeAstNode<FuncCallExpr>(expr, args);
      }
      case Tag::kArrow: {
        expr = MakeAstNode<UnaryOpExpr>(tok, Tag::kStar, expr);

        auto member{Expect(Tag::kIdentifier)};
        auto member_name{member.GetStr()};

        auto type{expr->GetType()};
        if (!type->IsStructTy()) {
          Error(tok, "an struct/union expected");
        }

        auto rhs{type->GetStructMember(member_name)};
        if (!rhs) {
          Error(tok, "'{}' is not a member of '{}'", member_name,
                type->GetStructName());
        }

        return MakeAstNode<BinaryOpExpr>(tok, Tag::kPeriod, expr, rhs);
      }
      case Tag::kPeriod: {
        auto member{Expect(Tag::kIdentifier)};
        auto member_name{member.GetStr()};

        auto type{expr->GetType()};
        if (!type->IsStructTy()) {
          Error(tok, "an struct/union expected");
        }

        auto rhs{type->GetStructMember(member_name)};
        if (!rhs) {
          Error(tok, "'{}' is not a member of '{}'", member_name,
                type->GetStructName());
        }

        return MakeAstNode<BinaryOpExpr>(tok, Tag::kPeriod, expr, rhs);
      }
      case Tag::kPlusPlus:
        return MakeAstNode<UnaryOpExpr>(tok, Tag::kPostfixPlusPlus, expr);
      case Tag::kMinusMinus:
        return MakeAstNode<UnaryOpExpr>(tok, Tag::kPostfixMinusMinus, expr);
      default:
        PutBack();
        return expr;
    }
  }
}

std::shared_ptr<Expr> Parser::ParseConstant() {
  if (Peek().IsIntegerConstant()) {
    return ParseInteger();
  } else if (Peek().IsFloatConstant()) {
    return ParseFloat();
  } else if (Peek().IsCharacterConstant()) {
    return ParseCharacter();
  } else {
    assert(false);
    return nullptr;
  }
}

std::shared_ptr<Expr> Parser::ParseFloat() {
  auto tok{Next()};
  auto str{tok.GetStr()};
  double val;
  std::size_t end;

  try {
    val = std::stod(str, &end);
  } catch (const std::out_of_range& err) {
    Error(tok, "float out of range");
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

  return MakeAstNode<Constant>(tok, Type::Get(type_spec), val);
}

std::shared_ptr<Expr> Parser::ParseInteger() {
  auto tok{Next()};
  auto str{tok.GetStr()};
  std::uint64_t val;
  std::size_t end;

  try {
    // 当 base 为 0 时，自动检测进制
    val = std::stoull(str, &end, 0);
  } catch (const std::out_of_range& error) {
    Error(tok, "integer out of range");
  }

  auto backup{end};
  std::uint32_t type_spec{};
  for (auto ch{str[end]}; ch != '\0'; ch = str[++end]) {
    if (ch == 'u' || ch == 'U') {
      if (type_spec & kUnsigned) {
        Error(tok, "invalid suffix: {}", str.substr(backup));
      }
      type_spec |= kUnsigned;
    } else if (ch == 'l' || ch == 'L') {
      if ((type_spec & kLong) || (type_spec & kLongLong)) {
        Error(tok, "invalid suffix: {}", str.substr(backup));
      }

      if (str[end + 1] == 'l' || str[end + 1] == 'L') {
        type_spec |= kLongLong;
        ++end;
      } else {
        type_spec |= kLong;
      }
    } else {
      Error(tok, "invalid suffix: {}", str.substr(backup));
    }
  }

  // 十进制
  bool decimal{'1' <= str.front() && str.front() <= '9'};

  if (decimal) {
    switch (type_spec) {
      case 0:
        if ((val > static_cast<std::uint32_t>(
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
        } else if (val > static_cast<std::uint32_t>(
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

  return MakeAstNode<Constant>(tok, Type::Get(type_spec), val);
}

std::shared_ptr<Expr> Parser::ParseCharacter() {
  auto tok{Next()};
  auto val{Scanner{tok.GetStr()}.ScanCharacter()};
  return MakeAstNode<Constant>(tok, val);
}

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
void Parser::TryAttributeSpec() {
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

void Parser::TryAsm() {
  if (Try(Tag::kAsm)) {
    Expect(Tag::kLeftParen);
    ParseStringLiteral(false);
    Expect(Tag::kRightParen);
  }
}

std::shared_ptr<Stmt> Parser::ParseStmt() {
  auto tok{Peek()};

  TryAttributeSpec();

  switch (tok.GetTag()) {
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

std::shared_ptr<CompoundStmt> Parser::ParseCompoundStmt(
    std::shared_ptr<Type> func_type) {
  Expect(Tag::kLeftBrace);

  EnterBlock(func_type);

  std::vector<std::shared_ptr<Stmt>> stmts;
  while (!Try(Tag::kRightBrace)) {
    if (IsDecl(Peek())) {
      stmts.push_back(ParseDecl());
    } else {
      stmts.push_back(ParseStmt());
    }
  }

  auto scope{curr_scope_};

  ExitBlock();

  return MakeAstNode<CompoundStmt>(stmts, scope);
}

std::shared_ptr<IfStmt> Parser::ParseIfStmt() {
  Expect(Tag::kIf);
  Expect(Tag::kLeftParen);

  auto tok{Peek()};
  auto cond{ParseExpr()};
  if (!cond->GetType()->IsScalarTy()) {
    Error(tok, "expect scalar");
  }

  Expect(Tag::kRightParen);

  auto then_block{ParseStmt()};
  if (Try(Tag::kElse)) {
    return MakeAstNode<IfStmt>(cond, then_block, ParseStmt());
  } else {
    return MakeAstNode<IfStmt>(cond, then_block);
  }
}

std::shared_ptr<WhileStmt> Parser::ParseWhileStmt() {
  Expect(Tag::kWhile);
  Expect(Tag::kLeftParen);

  auto tok{Peek()};
  auto cond{ParseExpr()};
  if (!cond->GetType()->IsScalarTy()) {
    Error(tok, "expect scalar");
  }
  Expect(Tag::kRightParen);

  return MakeAstNode<WhileStmt>(cond, ParseStmt());
}

std::shared_ptr<DoWhileStmt> Parser::ParseDoWhileStmt() {
  Expect(Tag::kDo);

  auto stmt{ParseStmt()};

  Expect(Tag::kWhile);
  Expect(Tag::kLeftParen);

  auto tok{Peek()};
  auto cond{ParseExpr()};
  if (!cond->GetType()->IsScalarTy()) {
    Error(tok, "expect scalar");
  }
  Expect(Tag::kRightParen);

  return MakeAstNode<DoWhileStmt>(cond, stmt);
}

std::shared_ptr<ForStmt> Parser::ParseForStmt() {
  Expect(Tag::kFor);
  Expect(Tag::kLeftParen);

  std::shared_ptr<Expr> init, cond, inc;
  std::shared_ptr<Stmt> block;
  std::shared_ptr<Stmt> decl;

  EnterBlock();
  auto tok{Peek()};
  if (IsDecl(tok)) {
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

  return MakeAstNode<ForStmt>(init, cond, inc, block, decl);
}

std::shared_ptr<ReturnStmt> Parser::ParseReturnStmt() {
  Expect(Tag::kReturn);

  if (Try(Tag::kSemicolon)) {
    return MakeAstNode<ReturnStmt>();
  } else {
    auto expr{ParseExpr()};
    Expect(Tag::kSemicolon);

    expr = Expr::MayCastTo(
        expr, curr_func_def_->GetFuncType()->GetFunctionReturnType());
    return MakeAstNode<ReturnStmt>(expr);
  }
}

std::shared_ptr<ExprStmt> Parser::ParseExprStmt() {
  if (Try(Tag::kSemicolon)) {
    return MakeAstNode<ExprStmt>();
  } else {
    auto ret{MakeAstNode<ExprStmt>(ParseExpr())};
    Expect(Tag::kSemicolon);
    return ret;
  }
}

std::shared_ptr<CaseStmt> Parser::ParseCaseStmt() {
  Expect(Tag::kCase);

  auto expr{ParseExpr()};
  if (!expr->GetType()->IsIntegerTy()) {
    Error(expr->GetToken(), "expect integer");
  }
  auto val{CalcExpr<std::int32_t>{}.Calc(expr)};

  if (Try(Tag::kEllipsis)) {
    auto expr2{ParseExpr()};
    if (!expr2->GetType()->IsIntegerTy()) {
      Error(expr2->GetToken(), "expect integer");
    }
    auto val2{CalcExpr<std::int32_t>{}.Calc(expr)};
    Expect(Tag::kColon);
    return MakeAstNode<CaseStmt>(val, val2, ParseStmt());
  } else {
    Expect(Tag::kColon);
    return MakeAstNode<CaseStmt>(val, ParseStmt());
  }
}

std::shared_ptr<DefaultStmt> Parser::ParseDefaultStmt() {
  Expect(Tag::kDefault);
  Expect(Tag::kColon);

  return MakeAstNode<DefaultStmt>(ParseStmt());
}

std::shared_ptr<SwitchStmt> Parser::ParseSwitchStmt() {
  Expect(Tag::kSwitch);
  Expect(Tag::kLeftParen);

  auto cond{ParseExpr()};
  Expect(Tag::kRightParen);

  return MakeAstNode<SwitchStmt>(cond, ParseStmt());
}

std::shared_ptr<GotoStmt> Parser::ParseGotoStmt() {
  Expect(Tag::kGoto);
  auto tok{Expect(Tag::kIdentifier)};
  Expect(Tag::kSemicolon);

  return MakeAstNode<GotoStmt>(
      MakeAstNode<Identifier>(tok, Type::GetVoidPtrTy(), kNone, false));
}

std::shared_ptr<LabelStmt> Parser::ParseLabelStmt() {
  auto tok{Expect(Tag::kIdentifier)};
  Expect(Tag::kColon);

  TryAttributeSpec();

  return MakeAstNode<LabelStmt>(
      MakeAstNode<Identifier>(tok, Type::GetVoidPtrTy(), kNone, false));
}

std::shared_ptr<ContinueStmt> Parser::ParseContinueStmt() {
  Expect(Tag::kContinue);
  Expect(Tag::kSemicolon);

  return MakeAstNode<ContinueStmt>();
}

std::shared_ptr<BreakStmt> Parser::ParseBreakStmt() {
  Expect(Tag::kBreak);
  Expect(Tag::kSemicolon);

  return MakeAstNode<BreakStmt>();
}

void Parser::EnterBlock(std::shared_ptr<Type> func_type) {
  curr_scope_ = std::make_shared<Scope>(curr_scope_, kBlock);

  if (func_type) {
    for (const auto& param : func_type->GetFunctionParams()) {
      curr_scope_->InsertNormal(param->GetName(), param);
    }
  }
}

void Parser::ExitBlock() { curr_scope_ = curr_scope_->GetParent(); }

bool Parser::IsDecl(const Token& tok) {
  if (tok.IsDecl()) {
    return true;
  } else if (tok.IsIdentifier()) {
    auto ident{curr_scope_->FindNormal(tok)};
    if (ident && ident->IsTypeName()) {
      return true;
    }
  }

  return false;
}

void Parser::EnterFunc(std::shared_ptr<Identifier> ident) {
  curr_func_def_ = MakeAstNode<FuncDef>(ident);
}

void Parser::ExitFunc() { curr_func_def_ = nullptr; }

// TODO init
// void Parser::ParseInitializer(std::set<Initializer>& inits,
//                              std::shared_ptr<Type> type, std::int32_t offset,
//                              bool designated, bool force_brace) {
//  if (designated && !Test(Tag::kPeriod) && !Test(Tag::kLeftSquare)) {
//    Expect(Tag::kEqual);
//  }
//
//  if (type->IsArrayTy()) {
//  } else if (type->IsStructTy()) {
//  }
//}

}  // namespace kcc
