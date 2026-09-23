#pragma once

#include "ExpressionVisitor.h"
#include "Expression.h"
#include "ExpressionValue.h"
#include "Iterator.h"

class UFunction;

enum class StatementResult
{
	Next,
	Jump,
	Return,
	Stop,
	GotoLabel,
	Switch,
	Iterator,
	IteratorNext,
	IteratorPop,
	AccessedNone
};

struct ExpressionEvalResult
{
	StatementResult Result = StatementResult::Next;
	uint16_t JumpAddress = 0;
	int LatentFunction = 0;
	NameString Label;
	ExpressionValue Value;
	std::unique_ptr<Iterator> Iter;
};

class ExpressionEvaluator : ExpressionVisitor
{
public:
	// Evaluates a statement: its value, and what the frame does next
	static ExpressionEvalResult Eval(Expression* expr, UObject* self, UObject* context, void* localVariables);

private:
	ExpressionEvaluator(ExpressionEvalResult& result, UObject* self, UObject* context, void* localVariables)
		: Result(result), Self(self), Context(context), LocalVariables(localVariables), Out(&result.Value) {}

	// A nested expression's value, from this same evaluator. Only the statement
	// says what the frame does next: whatever else a nested expression reports
	// is dropped, as it was when every expression had an evaluator and a
	// result of its own.
	ExpressionValue Value(Expression* expr);
	ExpressionValue Value(Expression* expr, UObject* context);
	ExpressionValue VisitValue(Expression* expr); // Value, through the visitor

	// Evaluates expr as this expression (Skip, Context): its value is this
	// one's, and so is what it says the frame does next.
	void PassThrough(Expression* expr, UObject* context);

	bool IsStatement() const { return Out == &Result.Value; }

	// A nested expression's value as a plain value, exactly as Value(expr)
	// and its ToBool (ToByte, ToInt, ToFloat, ToObject, ToName) would give it:
	// without an ExpressionValue where the node's Expression::TypedKind
	// allows, through Value where it does not (Generic).
	bool EvalBool(Expression* expr);
	bool EvalBool(Expression* expr, UObject* context);
	template<typename T> T EvalNumber(Expression* expr); // uint8_t, int32_t or float
	UObject* EvalObject(Expression* expr);
	NameString EvalName(Expression* expr);

	// Value(expr) and its conversion, for nodes no typed kind covers. Kept out
	// of line: the typed evaluators then need no ExpressionValue of their own.
	bool GenericBool(Expression* expr);
	template<typename T> T GenericNumber(Expression* expr);
	UObject* GenericObject(Expression* expr);
	NameString GenericName(Expression* expr);

	// A typed operator's operands, evaluated in the caller's own context
	template<typename T> void Operands(Expression* expr, T& a, T& b);
	void ObjectOperands(Expression* expr, UObject*& a, UObject*& b);
	void NameOperands(Expression* expr, NameString& a, NameString& b);
	template<typename T> T& Target(Expression* expr); // the variable ++, += and -= change

	// A typed operator's value, as the general path would have left it in Out
	ExpressionValue OperatorValue(Expression* expr);

	// A statement's assignment to a variable kind, stored as ExpressionValue's
	// Store would store it; false for any other left side
	bool Assign(Expression* lhs, Expression* rhs);

	// Where a variable kind's value lives: in the locals (Local* kinds) or the
	// context's properties (Instance* kinds). E is the node's class: a
	// LocalVariableExpression, InstanceVariableExpression or BoolVariableExpression.
	template<typename E> uint8_t* Local(Expression* expr) const;
	template<typename E> uint8_t* Instance(Expression* expr) const;

	static void Classify(Expression* expr);
	static Expression::TypedKind OperatorKind(NativeFunctionExpression* expr);

	void Expr(LocalVariableExpression* expr) override;
	void Expr(InstanceVariableExpression* expr) override;
	void Expr(DefaultVariableExpression* expr) override;
	void Expr(ReturnExpression* expr) override;
	void Expr(SwitchExpression* expr) override;
	void Expr(JumpExpression* expr) override;
	void Expr(JumpIfNotExpression* expr) override;
	void Expr(StopExpression* expr) override;
	void Expr(AssertExpression* expr) override;
	void Expr(CaseExpression* expr) override;
	void Expr(NothingExpression* expr) override;
	void Expr(LabelTableExpression* expr) override;
	void Expr(GotoLabelExpression* expr) override;
	void Expr(EatStringExpression* expr) override;
	void Expr(LetExpression* expr) override;
	void Expr(DynArrayElementExpression* expr) override;
	void Expr(NewExpression* expr) override;
	void Expr(ClassContextExpression* expr) override;
	void Expr(MetaCastExpression* expr) override;
	void Expr(LetBoolExpression* expr) override;
	void Expr(Unknown0x15Expression* expr) override;
	void Expr(SelfExpression* expr) override;
	void Expr(SkipExpression* expr) override;
	void Expr(ContextExpression* expr) override;
	void Expr(ArrayElementExpression* expr) override;
	void Expr(IntConstExpression* expr) override;
	void Expr(FloatConstExpression* expr) override;
	void Expr(StringConstExpression* expr) override;
	void Expr(ObjectConstExpression* expr) override;
	void Expr(NameConstExpression* expr) override;
	void Expr(RotationConstExpression* expr) override;
	void Expr(VectorConstExpression* expr) override;
	void Expr(ByteConstExpression* expr) override;
	void Expr(IntZeroExpression* expr) override;
	void Expr(IntOneExpression* expr) override;
	void Expr(TrueExpression* expr) override;
	void Expr(FalseExpression* expr) override;
	void Expr(NativeParmExpression* expr) override;
	void Expr(NoObjectExpression* expr) override;
	void Expr(Unknown0x2bExpression* expr) override;
	void Expr(IntConstByteExpression* expr) override;
	void Expr(BoolVariableExpression* expr) override;
	void Expr(DynamicCastExpression* expr) override;
	void Expr(IteratorExpression* expr) override;
	void Expr(IteratorPopExpression* expr) override;
	void Expr(IteratorNextExpression* expr) override;
	void Expr(StructCmpEqExpression* expr) override;
	void Expr(StructCmpNeExpression* expr) override;
	void Expr(UnicodeStringConstExpression* expr) override;
	void Expr(StructMemberExpression* expr) override;
	void Expr(RotatorToVectorExpression* expr) override;
	void Expr(ByteToIntExpression* expr) override;
	void Expr(ByteToBoolExpression* expr) override;
	void Expr(ByteToFloatExpression* expr) override;
	void Expr(IntToByteExpression* expr) override;
	void Expr(IntToBoolExpression* expr) override;
	void Expr(IntToFloatExpression* expr) override;
	void Expr(BoolToByteExpression* expr) override;
	void Expr(BoolToIntExpression* expr) override;
	void Expr(BoolToFloatExpression* expr) override;
	void Expr(FloatToByteExpression* expr) override;
	void Expr(FloatToIntExpression* expr) override;
	void Expr(FloatToBoolExpression* expr) override;
	void Expr(Unknown0x46Expression* expr) override;
	void Expr(ObjectToBoolExpression* expr) override;
	void Expr(NameToBoolExpression* expr) override;
	void Expr(StringToByteExpression* expr) override;
	void Expr(StringToIntExpression* expr) override;
	void Expr(StringToBoolExpression* expr) override;
	void Expr(StringToFloatExpression* expr) override;
	void Expr(StringToVectorExpression* expr) override;
	void Expr(StringToRotatorExpression* expr) override;
	void Expr(VectorToBoolExpression* expr) override;
	void Expr(VectorToRotatorExpression* expr) override;
	void Expr(RotatorToBoolExpression* expr) override;
	void Expr(ByteToStringExpression* expr) override;
	void Expr(IntToStringExpression* expr) override;
	void Expr(BoolToStringExpression* expr) override;
	void Expr(FloatToStringExpression* expr) override;
	void Expr(ObjectToStringExpression* expr) override;
	void Expr(NameToStringExpression* expr) override;
	void Expr(VectorToStringExpression* expr) override;
	void Expr(RotatorToStringExpression* expr) override;
	void Expr(StringToNameExpression* expr) override;
	void Expr(DynArrayToIntExpression* expr) override;
	void Expr(VirtualFunctionExpression* expr) override;
	void Expr(FinalFunctionExpression* expr) override;
	void Expr(GlobalFunctionExpression* expr) override;
	void Expr(NativeFunctionExpression* expr) override;
	void Expr(FunctionArgumentsExpression* expr) override;
	void Expr(ConstructExpression* expr) override;

	void Call(UFunction* func, const Array<Expression*>& exprArgs);
	bool CallFastOperator(UFunction* func, const Array<Expression*>& exprArgs);

	ExpressionEvalResult& Result;
	UObject* Self = nullptr;
	UObject* Context = nullptr;
	void* LocalVariables = nullptr;
	ExpressionValue* Out = nullptr; // where the expression being visited leaves its value
};
