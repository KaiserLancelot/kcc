//
// Created by kaiser on 2019/11/17.
//

#include "llvm_common.h"

#include <assert.h>

#include <llvm/Support/raw_ostream.h>

#include "error.h"

namespace kcc {

std::string LLVMTypeToStr(llvm::Type *type) {
  std::string s;
  llvm::raw_string_ostream rso{s};
  type->print(rso);
  return s;
}

std::string LLVMConstantToStr(llvm::Constant *type) {
  std::string s;
  llvm::raw_string_ostream rso{s};
  type->print(rso);
  return s;
}

llvm::Constant *GetConstantZero(llvm::Type *type) {
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
  auto value_type{value->getType()};

  return value_type->isPointerTy() &&
         value_type->getPointerElementType()->isArrayTy() &&
         type->isPointerTy() &&
         value_type->getPointerElementType()
                 ->getArrayElementType()
                 ->getPointerTo() == type;
}

bool IsIntegerTy(llvm::Value *value) { return value->getType()->isIntegerTy(); }

bool IsFloatingPointTy(llvm::Value *value) {
  return value->getType()->isFloatingPointTy();
}

bool IsPointerTy(llvm::Value *value) { return value->getType()->isPointerTy(); }

llvm::Constant *ConstantCastTo(llvm::Constant *value, llvm::Type *to,
                               bool is_unsigned) {
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
    Error("can not cast this expression with type'{}' to '{}'",
          LLVMTypeToStr(value->getType()), LLVMTypeToStr(to));
  }
}

llvm::Constant *ConstantCastToBool(llvm::Constant *value) {
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
    Error("this constant expression can not cast to bool '{}'",
          LLVMTypeToStr(value->getType()));
  }
}

llvm::ConstantInt *GetInt32Constant(std::int32_t value) {
  return llvm::ConstantInt::get(Builder.getInt32Ty(), value);
}

}  // namespace kcc