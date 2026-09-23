#pragma once

#include "ExpressionVisitor.h"
#include "Package/NameString.h"

class UObject;
class UClass;
class UStruct;
class UFunction;
class UProperty;

class Expression
{
public:
	virtual ~Expression() = default;
	virtual void Visit(ExpressionVisitor* visitor) = 0;

	int StatementIndex = -1;

	// The leaves ExpressionEvaluator::Value makes without the visitor: about
	// half of all expressions evaluated. Everything else is Other.
	enum class LeafKind : uint8_t
	{
		Other, LocalVariable, InstanceVariable, BoolVariable, Self, NoObject, ObjectConst, NameConst,
		IntConst, IntZero, IntOne, IntConstByte, ByteConst, FloatConst, True, False
	};
	LeafKind Leaf = LeafKind::Other;

	// How ExpressionEvaluator's typed evaluators (EvalBool, EvalNumber, ...)
	// take this node: as a plain value, with no ExpressionValue. Decided when
	// one first meets it (ExpressionEvaluator::Classify). Generic nodes go
	// through Value and its conversions, as every node did before.
	enum class TypedKind : uint8_t
	{
		Unknown, Generic,
		// Local and instance variables, read in place. Keep these together:
		// ExpressionEvaluator tells a variable from a value by the range.
		LocalByte, LocalInt, LocalBool, LocalFloat, LocalObject, LocalName,
		InstanceByte, InstanceInt, InstanceBool, InstanceFloat, InstanceObject, InstanceName,
		LocalBoolVariable, InstanceBoolVariable, // BoolVariable over a local or instance bool
		// Constants
		Self, NoObject, ObjectConst, NameConst, IntConst, IntZero, IntOne, IntConstByte, ByteConst, FloatConst, True, False,
		// Skip (the right side of && and ||): its operand's value, for the
		// typed evaluators only -- no operator takes it as an operand
		Skip,
		// Conversions
		ByteToInt, ByteToBool, ByteToFloat, IntToByte, IntToBool, IntToFloat, BoolToByte, BoolToInt, BoolToFloat,
		FloatToByte, FloatToInt, FloatToBool, ObjectToBool, NameToBool,
		// && and ||, and the operators ExpressionEvaluator evaluates in place
		// (its fast operators, UFunction::FastOperator)
		AndAnd, OrOr,
		NotEqual_ObjectObject, EqualEqual_ObjectObject, Not_PreBool,
		Less_IntInt, LessEqual_IntInt, Greater_IntInt, GreaterEqual_IntInt, EqualEqual_IntInt, NotEqual_IntInt,
		Less_FloatFloat, LessEqual_FloatFloat, Greater_FloatFloat, GreaterEqual_FloatFloat, NotEqual_FloatFloat,
		Add_FloatFloat, Subtract_FloatFloat, Multiply_FloatFloat, Divide_FloatFloat, Subtract_IntInt,
		AddAdd_Int, AddEqual_IntInt, AddEqual_FloatFloat, SubtractEqual_FloatFloat,
		EqualEqual_NameName, NotEqual_NameName
	};
	TypedKind Typed = TypedKind::Unknown;

	// How Frame::Run runs this node as a statement: the commonest kinds in
	// place (Frame::RunInPlace), without the ExpressionEvalResult
	// ExpressionEvaluator::Eval makes; decided when first run
	// (Frame::ClassifyStatement).
	enum class StatementKind : uint8_t
	{
		Unknown, General, Jump, JumpIfNot, Let, LetBool, Call, ReturnNothing, IteratorNext
	};
	StatementKind Statement = StatementKind::Unknown;
};

class LocalVariableExpression : public Expression
{
public:
	LocalVariableExpression() { Leaf = LeafKind::LocalVariable; }
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	UProperty* Variable = nullptr;

	// Variable's offset and bool mask, kept here for the typed evaluators
	// (set by ExpressionEvaluator::Classify)
	uint32_t Offset = 0;
	uint32_t Mask = 0;
};

class InstanceVariableExpression : public Expression
{
public:
	InstanceVariableExpression() { Leaf = LeafKind::InstanceVariable; }
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	UProperty* Variable = nullptr;

	// As LocalVariableExpression's
	uint32_t Offset = 0;
	uint32_t Mask = 0;
};

class DefaultVariableExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	UProperty* Variable = nullptr;
};

class ReturnExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class SwitchExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	int Size = 0;
	Expression* Condition = nullptr;
};

class JumpExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	uint16_t Offset = 0;
	int Target = -1; // Offset's statement index (Frame::ClassifyStatement)
};

class JumpIfNotExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	uint16_t Offset = 0;
	int Target = -1; // Offset's statement index (Frame::ClassifyStatement)
	Expression* Condition = nullptr;
};

class StopExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }
};

class AssertExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	uint16_t Line = 0;
	Expression* Condition = nullptr;
};

class CaseExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	uint16_t NextOffset = 0;
	Expression* Value = nullptr;
};

class NothingExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }
};

struct LabelEntry
{
	NameString Name;
	uint32_t Offset = 0;
};

class LabelTableExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Array<LabelEntry> Labels;
};

class GotoLabelExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class EatStringExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class LetExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* LeftSide = nullptr;
	Expression* RightSide = nullptr;
};

class DynArrayElementExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Index = nullptr;
	Expression* Array = nullptr;
};

class NewExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* ParentExpr = nullptr;
	Expression* NameExpr = nullptr;
	Expression* FlagsExpr = nullptr;
	Expression* ClassExpr = nullptr;
};

class ClassContextExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* ObjectExpr = nullptr;
	uint16_t NullExprCodeOffset = 0;
	uint8_t ZeroFillSize = 0;
	Expression* ContextExpr = nullptr;
};

class MetaCastExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	UClass* Class = nullptr;
	Expression* Value = nullptr;
};

class LetBoolExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* LeftSide = nullptr;
	Expression* RightSide = nullptr;
};

class Unknown0x15Expression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class SelfExpression : public Expression
{
public:
	SelfExpression() { Leaf = LeafKind::Self; }
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }
};

class SkipExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	uint16_t Skip = 0;
	Expression* Value = nullptr;
};

class ContextExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* ObjectExpr = nullptr;
	uint16_t NullExprCodeOffset = 0;
	uint8_t ZeroFillSize = 0;
	Expression* ContextExpr = nullptr;
};

class ArrayElementExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Index = nullptr;
	Expression* Array = nullptr;
};

class IntConstExpression : public Expression
{
public:
	IntConstExpression() { Leaf = LeafKind::IntConst; }
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	uint32_t Value = 0;
};

class FloatConstExpression : public Expression
{
public:
	FloatConstExpression() { Leaf = LeafKind::FloatConst; }
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	float Value = 0.0f;
};

class StringConstExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	std::string Value;
};

class ObjectConstExpression : public Expression
{
public:
	ObjectConstExpression() { Leaf = LeafKind::ObjectConst; }
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	UObject* Object = nullptr;
};

class NameConstExpression : public Expression
{
public:
	NameConstExpression() { Leaf = LeafKind::NameConst; }
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	NameString Value;
};

class RotationConstExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	int32_t Pitch = 0;
	int32_t Yaw = 0;
	int32_t Roll = 0;
};

class VectorConstExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	float X = 0.0f;
	float Y = 0.0f;
	float Z = 0.0f;
};

class ByteConstExpression : public Expression
{
public:
	ByteConstExpression() { Leaf = LeafKind::ByteConst; }
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	uint8_t Value = 0;
};

class IntZeroExpression : public Expression
{
public:
	IntZeroExpression() { Leaf = LeafKind::IntZero; }
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }
};

class IntOneExpression : public Expression
{
public:
	IntOneExpression() { Leaf = LeafKind::IntOne; }
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }
};

class TrueExpression : public Expression
{
public:
	TrueExpression() { Leaf = LeafKind::True; }
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }
};

class FalseExpression : public Expression
{
public:
	FalseExpression() { Leaf = LeafKind::False; }
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }
};

class NativeParmExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	UObject* Object = nullptr;
};

class NoObjectExpression : public Expression
{
public:
	NoObjectExpression() { Leaf = LeafKind::NoObject; }
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }
};

class Unknown0x2bExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	uint8_t Unknown = 0;
	Expression* Value = nullptr;
};

class IntConstByteExpression : public Expression
{
public:
	IntConstByteExpression() { Leaf = LeafKind::IntConstByte; }
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	uint8_t Value = 0;
};

class BoolVariableExpression : public Expression
{
public:
	BoolVariableExpression() { Leaf = LeafKind::BoolVariable; }
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Variable = nullptr;

	// Variable's own offset and bool mask, when it is a local or instance
	// variable: the typed evaluators read it without visiting Variable
	uint32_t Offset = 0;
	uint32_t Mask = 0;
};

class DynamicCastExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	UClass* Class = nullptr;
	Expression* Value = nullptr;
};

class IteratorExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
	uint16_t Offset = 0;
};

class IteratorPopExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }
};

class IteratorNextExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }
};

class StructCmpEqExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	UObject* Struct = nullptr;
	Expression* Value1 = nullptr;
	Expression* Value2 = nullptr;
};

class StructCmpNeExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	UObject* Struct = nullptr;
	Expression* Value1 = nullptr;
	Expression* Value2 = nullptr;
};

class UnicodeStringConstExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	std::wstring Value;
};

class StructMemberExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	UProperty* Field = nullptr;
	Expression* Value = nullptr;
};

class RotatorToVectorExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class ByteToIntExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class ByteToBoolExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class ByteToFloatExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class IntToByteExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class IntToBoolExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class IntToFloatExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class BoolToByteExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class BoolToIntExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class BoolToFloatExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class FloatToByteExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class FloatToIntExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class FloatToBoolExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class Unknown0x46Expression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class ObjectToBoolExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class NameToBoolExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class StringToByteExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class StringToIntExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class StringToBoolExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class StringToFloatExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class StringToVectorExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class StringToRotatorExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class VectorToBoolExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class VectorToRotatorExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class RotatorToBoolExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class ByteToStringExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class IntToStringExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class BoolToStringExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class FloatToStringExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class ObjectToStringExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class NameToStringExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class VectorToStringExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class RotatorToStringExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class StringToNameExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class DynArrayToIntExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Expression* Value = nullptr;
};

class VirtualFunctionExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	NameString Name;
	Array<Expression*> Args;
};

class FinalFunctionExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	UFunction* Func = nullptr;
	Array<Expression*> Args;
};

class GlobalFunctionExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	NameString Name;
	Array<Expression*> Args;
};

class NativeFunctionExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	int nativeindex = 0;
	Array<Expression*> Args;

	// A typed operator's operands, Args[0] and Args[1], kept here for the
	// typed evaluators (set by ExpressionEvaluator::Classify)
	Expression* Operands[2] = {};
};

struct FunctionArgInfo
{
	int size = 0;
	int flags = 0; // 1 = out parameter
};

class FunctionArgumentsExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	Array<FunctionArgInfo> args;
};

struct ConstructArgument
{
	UProperty* Name = nullptr;
	Expression* Value = nullptr;
};

class ConstructExpression : public Expression
{
public:
	void Visit(ExpressionVisitor* visitor) override { visitor->Expr(this); }

	UStruct* Struct = nullptr;
	Array<ConstructArgument> Args;
};
