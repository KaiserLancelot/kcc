//
// Created by kaiser on 2019/11/17.
//

#include "llvm_common.h"

#include <cassert>

#include <llvm/Support/raw_ostream.h>

#include "error.h"

namespace kcc {

std::string LLVMTypeToStr(llvm::Type *type) {
  assert(type != nullptr);

  std::string s;
  llvm::raw_string_ostream rso{s};
  type->print(rso);
  return s;
}

std::string LLVMConstantToStr(llvm::Constant *constant) {
  assert(constant != nullptr);

  std::string s;
  llvm::raw_string_ostream rso{s};
  constant->print(rso);
  return s;
}

llvm::Constant *GetConstantZero(llvm::Type *type) {
  assert(type != nullptr);

  if (type->isIntegerTy()) {
    return llvm::ConstantInt::get(type, 0);
  } else if (type->isFloatingPointTy()) {
    return llvm::ConstantFP::get(type, 0.0);
  } else if (type->isPointerTy()) {
    return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(type));
  } else if (type->isAggregateType()) {
    return llvm::ConstantAggregateZero::get(type);
  } else {
    assert(false);
    return nullptr;
  }
}

std::int32_t FloatPointRank(llvm::Type *type) {
  assert(type != nullptr);

  if (type->isFloatTy()) {
    return 0;
  } else if (type->isDoubleTy()) {
    return 1;
  } else if (type->isX86_FP80Ty()) {
    return 2;
  } else {
    assert(false);
    return 0;
  }
}

bool IsArrCastToPtr(llvm::Value *value, llvm::Type *type) {
  assert(value != nullptr && type != nullptr);

  auto value_type{value->getType()};

  return value_type->isPointerTy() &&
         value_type->getPointerElementType()->isArrayTy() &&
         type->isPointerTy() &&
         value_type->getPointerElementType()
                 ->getArrayElementType()
                 ->getPointerTo() == type;
}

bool IsIntegerTy(llvm::Value *value) {
  assert(value != nullptr);
  return value->getType()->isIntegerTy();
}

bool IsFloatingPointTy(llvm::Value *value) {
  assert(value != nullptr);
  return value->getType()->isFloatingPointTy();
}

bool IsPointerTy(llvm::Value *value) {
  assert(value != nullptr);
  return value->getType()->isPointerTy();
}

bool IsFuncPointer(llvm::Type *type) {
  return type->isPointerTy() && type->getPointerElementType()->isFunctionTy();
}

bool IsArrayPointer(llvm::Type *type) {
  return type->isPointerTy() && type->getPointerElementType()->isArrayTy();
}

llvm::Constant *ConstantCastTo(llvm::Constant *value, llvm::Type *to,
                               bool is_unsigned) {
  assert(value != nullptr && to != nullptr);

  if (to->isIntegerTy(1)) {
    return ConstantCastToBool(value);
  }

  if (IsIntegerTy(value) && to->isIntegerTy()) {
    if (value->getType()->getIntegerBitWidth() > to->getIntegerBitWidth()) {
      return llvm::ConstantExpr::getTrunc(value, to);
    } else {
      if (is_unsigned) {
        return llvm::ConstantExpr::getZExt(value, to);
      } else {
        return llvm::ConstantExpr::getSExt(value, to);
      }
    }
  } else if (IsIntegerTy(value) && to->isFloatingPointTy()) {
    if (is_unsigned) {
      return llvm::ConstantExpr::getUIToFP(value, to);
    } else {
      return llvm::ConstantExpr::getSIToFP(value, to);
    }
  } else if (IsFloatingPointTy(value) && to->isIntegerTy()) {
    if (is_unsigned) {
      return llvm::ConstantExpr::getFPToUI(value, to);
    } else {
      return llvm::ConstantExpr::getFPToSI(value, to);
    }
  } else if (IsFloatingPointTy(value) && to->isFloatingPointTy()) {
    if (FloatPointRank(value->getType()) > FloatPointRank(to)) {
      return llvm::ConstantExpr::getFPTrunc(value, to);
    } else {
      return llvm::ConstantExpr::getFPExtend(value, to);
    }
  } else if (IsPointerTy(value) && to->isIntegerTy()) {
    return llvm::ConstantExpr::getPtrToInt(value, to);
  } else if (IsIntegerTy(value) && to->isPointerTy()) {
    return llvm::ConstantExpr::getIntToPtr(value, to);
  } else if (to->isVoidTy() || value->getType() == to) {
    return value;
  } else if (IsArrCastToPtr(value, to)) {
    auto zero{llvm::ConstantInt::get(Builder.getInt64Ty(), 0)};
    return llvm::ConstantExpr::getInBoundsGetElementPtr(
        nullptr, value, llvm::ArrayRef<llvm::Constant *>{zero, zero});
  } else if (IsPointerTy(value) && to->isPointerTy()) {
    return llvm::ConstantExpr::getPointerCast(value, to);
  } else {
    Error("can not cast this expression with type '{}' to '{}'",
          LLVMTypeToStr(value->getType()), LLVMTypeToStr(to));
  }
}

llvm::Constant *ConstantCastToBool(llvm::Constant *value) {
  assert(value != nullptr);

  if (value->getType()->isIntegerTy(1)) {
    return value;
  }

  if (IsIntegerTy(value) || IsPointerTy(value)) {
    return llvm::ConstantExpr::getICmp(llvm::CmpInst::ICMP_NE, value,
                                       GetConstantZero(value->getType()));
  } else if (IsFloatingPointTy(value)) {
    return llvm::ConstantExpr::getFCmp(llvm::CmpInst::FCMP_ONE, value,
                                       GetConstantZero(value->getType()));
  } else {
    Error("this constant expression can not cast to bool: '{}'",
          LLVMTypeToStr(value->getType()));
  }
}

llvm::ConstantInt *GetInt32Constant(std::int32_t value) {
  return llvm::ConstantInt::get(Builder.getInt32Ty(), value);
}

llvm::Value *CastTo(llvm::Value *value, llvm::Type *to, bool is_unsigned) {
  assert(value != nullptr && to != nullptr);

  if (to->isIntegerTy(1)) {
    return CastToBool(value);
  }

  if (IsIntegerTy(value) && to->isIntegerTy()) {
    if (is_unsigned) {
      return Builder.CreateZExtOrTrunc(value, to);
    } else {
      return Builder.CreateSExtOrTrunc(value, to);
    }
  } else if (IsIntegerTy(value) && to->isFloatingPointTy()) {
    if (is_unsigned) {
      return Builder.CreateUIToFP(value, to);
    } else {
      return Builder.CreateSIToFP(value, to);
    }
  } else if (IsFloatingPointTy(value) && to->isIntegerTy()) {
    if (is_unsigned) {
      return Builder.CreateFPToUI(value, to);
    } else {
      return Builder.CreateFPToSI(value, to);
    }
  } else if (IsFloatingPointTy(value) && to->isFloatingPointTy()) {
    if (FloatPointRank(value->getType()) > FloatPointRank(to)) {
      return Builder.CreateFPTrunc(value, to);
    } else {
      return Builder.CreateFPExt(value, to);
    }
  } else if (IsPointerTy(value) && to->isIntegerTy()) {
    return Builder.CreatePtrToInt(value, to);
  } else if (IsIntegerTy(value) && to->isPointerTy()) {
    return Builder.CreateIntToPtr(value, to);
  } else if (to->isVoidTy() || value->getType() == to) {
    return value;
  } else if (IsArrCastToPtr(value, to)) {
    return Builder.CreateInBoundsGEP(
        value, {Builder.getInt64(0), Builder.getInt64(0)});
  } else if (IsPointerTy(value) && to->isPointerTy()) {
    return Builder.CreatePointerCast(value, to);
  } else {
    Error("can not cast this expression with type '{}' to '{}'",
          LLVMTypeToStr(value->getType()), LLVMTypeToStr(to));
  }
}

llvm::Value *CastToBool(llvm::Value *value) {
  assert(value != nullptr);

  if (value->getType()->isIntegerTy(1)) {
    return value;
  }

  if (IsIntegerTy(value) || IsPointerTy(value)) {
    return Builder.CreateICmpNE(value, GetZero(value->getType()));
  } else if (IsFloatingPointTy(value)) {
    return Builder.CreateFCmpONE(value, GetZero(value->getType()));
  } else {
    Error("this constant expression can not cast to bool: '{}'",
          LLVMTypeToStr(value->getType()));
  }
}

llvm::Value *GetZero(llvm::Type *type) {
  assert(type != nullptr);

  if (type->isIntegerTy()) {
    return llvm::ConstantInt::get(type, 0);
  } else if (type->isFloatingPointTy()) {
    return llvm::ConstantFP::get(type, 0.0);
  } else if (type->isPointerTy()) {
    return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(type));
  } else {
    assert(false);
    return nullptr;
  }
}

llvm::GlobalVariable *CreateGlobalCompoundLiteral(QualType type,
                                                  llvm::Constant *init) {
  assert(init != nullptr);
  auto var{new llvm::GlobalVariable(
      *Module, type->GetLLVMType(), type.IsConst(),
      llvm::GlobalValue::InternalLinkage, init, ".compoundliteral")};
  var->setAlignment(type->GetAlign());

  return var;
}

llvm::GlobalVariable *CreateGlobalString(llvm::Constant *init,
                                         std::int32_t align) {
  assert(init != nullptr);
  auto var{new llvm::GlobalVariable(*Module, init->getType(), true,
                                    llvm::GlobalValue::PrivateLinkage, init,
                                    ".str")};
  var->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  var->setAlignment(align);

  return var;
}

void CreateGlobalVar(ObjectExpr *obj) {
  auto type{obj->GetType()->GetLLVMType()};
  auto name{obj->GetName()};

  // 在符号表中查找指定的全局变量, 如果不存在则添加并返回它
  Module->getOrInsertGlobal(name, type);
  auto ptr{Module->getNamedGlobal(name)};
  obj->SetGlobalPtr(ptr);

  ptr->setAlignment(obj->GetAlign());
  if (obj->IsStatic()) {
    ptr->setLinkage(llvm::GlobalVariable::InternalLinkage);
  } else if (obj->IsExtern()) {
    ptr->setLinkage(llvm::GlobalVariable::ExternalLinkage);
  } else {
    ptr->setDSOLocal(true);
  }
}

llvm::GlobalVariable *CreateLocalStaticVar(QualType type,
                                           const std::string &name) {
  auto var{
      new llvm::GlobalVariable(*Module, type->GetLLVMType(), type.IsConst(),
                               llvm::GlobalValue::InternalLinkage,
                               GetConstantZero(type->GetLLVMType()), name)};
  var->setAlignment(type->GetAlign());

  return var;
}

const llvm::fltSemantics &GetFloatTypeSemantics(llvm::Type *type) {
  assert(type->isFloatingPointTy());
  if (type->isFloatTy()) {
    return TargetInfo->getFloatFormat();
  } else if (type->isDoubleTy()) {
    return TargetInfo->getDoubleFormat();
  } else if (type->isX86_FP80Ty()) {
    return TargetInfo->getLongDoubleFormat();
  } else {
    assert(false);
    return TargetInfo->getDoubleFormat();
  }
}

llvm::Type *GetBitFieldSpace(std::int8_t width) {
  if (width <= 8) {
    return Builder.getInt8Ty();
  } else {
    return llvm::ArrayType::get(Builder.getInt8Ty(), (width + 7) / 8);
  }
}

std::int32_t GetLLVMTypeSize(llvm::Type *type) {
  return Module->getDataLayout().getTypeAllocSize(type);
}

}  // namespace kcc
