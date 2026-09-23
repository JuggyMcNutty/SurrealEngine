
#include "Precomp.h"
#include "ExpressionEvaluator.h"
#include "Expression.h"
#include "Bytecode.h"
#include "Frame.h"
#include "NativeFunc.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UFunction.h"
#include "ScriptCall.h"

ExpressionEvalResult ExpressionEvaluator::Eval(Expression* expr, UObject* self, UObject* context, void* localVariables)
{
	auto oldExpr = Frame::StepExpression;
	Frame::StepExpression = expr;

	// Breakpoints are set on statements only, so nested expressions need no check
	for (const Breakpoint& bp : Frame::Breakpoints)
	{
		if (bp.Expr == expr && bp.Enabled)
		{
			Frame::Break();
		}
	}

	ExpressionEvalResult result;
	ExpressionEvaluator evaluator(result, self, context, localVariables);
	expr->Visit(&evaluator);
	Frame::StepExpression = oldExpr;
	return result;
}

ExpressionValue ExpressionEvaluator::Value(Expression* expr)
{
	auto oldExpr = Frame::StepExpression;
	Frame::StepExpression = expr;

	ExpressionValue value;
	ExpressionValue* out = Out;
	Out = &value;
	expr->Visit(this);
	Out = out;

	Frame::StepExpression = oldExpr;
	return value;
}

ExpressionValue ExpressionEvaluator::Value(Expression* expr, UObject* context)
{
	UObject* oldContext = Context;
	Context = context;
	ExpressionValue value = Value(expr);
	Context = oldContext;
	return value;
}

void ExpressionEvaluator::PassThrough(Expression* expr, UObject* context)
{
	auto oldExpr = Frame::StepExpression;
	Frame::StepExpression = expr;

	UObject* oldContext = Context;
	Context = context;
	expr->Visit(this);
	Context = oldContext;

	Frame::StepExpression = oldExpr;
}

void ExpressionEvaluator::Expr(LocalVariableExpression* expr)
{
	*Out = ExpressionValue::Variable(LocalVariables, expr->Variable);
}

void ExpressionEvaluator::Expr(InstanceVariableExpression* expr)
{
	*Out = ExpressionValue::Variable(Context->PropertyData.Data, expr->Variable);
}

void ExpressionEvaluator::Expr(DefaultVariableExpression* expr)
{
	if (UObject::TryCast<UClass>(Context))
		*Out = ExpressionValue::Variable(Context->PropertyData.Data, expr->Variable);
	else
		*Out = ExpressionValue::Variable(Context->Class->GetDefaultObject<UObject>()->PropertyData.Data, expr->Variable);
}

void ExpressionEvaluator::Expr(ReturnExpression* expr)
{
	if (expr->Value)
		*Out = Value(expr->Value);
	else
		*Out = ExpressionValue::NothingValue();
	if (IsStatement())
		Result.Result = StatementResult::Return;
}

void ExpressionEvaluator::Expr(SwitchExpression* expr)
{
	*Out = Value(expr->Condition);
	if (IsStatement())
		Result.Result = StatementResult::Switch;
}

void ExpressionEvaluator::Expr(JumpExpression* expr)
{
	if (IsStatement())
	{
		Result.Result = StatementResult::Jump;
		Result.JumpAddress = expr->Offset;
	}
}

void ExpressionEvaluator::Expr(JumpIfNotExpression* expr)
{
	if (!Value(expr->Condition).ToBool() && IsStatement())
	{
		Result.Result = StatementResult::Jump;
		Result.JumpAddress = expr->Offset;
	}
}

void ExpressionEvaluator::Expr(StopExpression* expr)
{
	if (IsStatement())
		Result.Result = StatementResult::Stop;
}

void ExpressionEvaluator::Expr(AssertExpression* expr)
{
	if (!Value(expr->Condition).ToBool())
	{
		Frame::ThrowException("Script assert failed for " + Self->Name.ToString() + " line " + std::to_string(expr->Line));
	}
}

void ExpressionEvaluator::Expr(CaseExpression* expr)
{
	*Out = ExpressionValue::NothingValue();
}

void ExpressionEvaluator::Expr(NothingExpression* expr)
{
	*Out = ExpressionValue::NothingValue();
}

void ExpressionEvaluator::Expr(LabelTableExpression* expr)
{
	// Klingon honor guard has this! (UE 251)
	Frame::ThrowException("Label table expression is not implemented");
}

void ExpressionEvaluator::Expr(GotoLabelExpression* expr)
{
	NameString label = Value(expr->Value).ToName();
	if (IsStatement())
	{
		Result.Result = StatementResult::GotoLabel;
		Result.Label = label;
	}
}

void ExpressionEvaluator::Expr(EatStringExpression* expr)
{
	Value(expr->Value);
	*Out = ExpressionValue::NothingValue();
}

void ExpressionEvaluator::Expr(LetExpression* expr)
{
	ExpressionValue lvalue = Value(expr->LeftSide);
	ExpressionValue rvalue = Value(expr->RightSide);
	if (lvalue.GetType() != ExpressionValueType::Nothing)
	{
		lvalue.Store(rvalue);
		*Out = std::move(lvalue);
	}
	else
	{
		*Out = std::move(rvalue);
	}
}

void ExpressionEvaluator::Expr(LetBoolExpression* expr)
{
	ExpressionValue lvalue = Value(expr->LeftSide);
	ExpressionValue rvalue = Value(expr->RightSide);
	if (lvalue.GetType() != ExpressionValueType::Nothing)
	{
		lvalue.Store(rvalue);
		*Out = std::move(lvalue);
	}
	else
	{
		*Out = std::move(rvalue);
	}
}

void ExpressionEvaluator::Expr(DynArrayElementExpression* expr)
{
	int index = Value(expr->Index).ToInt();
	auto arrayval = Value(expr->Array);
	if (arrayval.IsVariable())
	{
		if (index < 0)
		{
			LogMessage("Negative index used");
			*Out = ExpressionValue::NothingValue();
		}
		else
		{
			*Out = arrayval.DynArrayItemAt(index);
		}
	}
	else
	{
		Frame::ThrowException("Array is not a variable in DynArrayElementExpression");
	}
}

void ExpressionEvaluator::Expr(NewExpression* expr)
{
	ExpressionValue outer = Value(expr->ParentExpr);
	ExpressionValue name = Value(expr->NameExpr);
	ExpressionValue flags = Value(expr->FlagsExpr);
	UClass* cls = UObject::Cast<UClass>(Value(expr->ClassExpr).ToObject());

	// To do: package needs to be grabbed from outer, or the "transient package" if it is None, a virtual package for runtime objects
	Package* package = engine->packages->GetPackage("Engine");

	UObject* newObj = package->NewObject(
		name.GetType() == ExpressionValueType::Nothing ? NameString() : name.ToName(),
		cls,
		flags.GetType() == ExpressionValueType::Nothing ? ObjectFlags::NoFlags : (ObjectFlags)flags.ToInt(),
		true);

	if (outer.GetType() != ExpressionValueType::Nothing)
		newObj->Outer() = outer.ToObject();

	*Out = ExpressionValue::ObjectValue(newObj);
}

void ExpressionEvaluator::Expr(ClassContextExpression* expr)
{
	ExpressionValue object = Value(expr->ObjectExpr);
	UClass* cls = UObject::TryCast<UClass>(object.ToObject());
	if (cls)
	{
		PassThrough(expr->ContextExpr, cls->GetDefaultObject<UObject>());
	}
	else
	{
		Frame::ThrowException("Class reference is None");
	}
}

void ExpressionEvaluator::Expr(MetaCastExpression* expr)
{
	UObject* value = Value(expr->Value).ToObject();
	if (value && value != expr->Class)
	{
		UClass* cls = UObject::TryCast<UClass>(value);
		while (cls)
		{
			if (cls == expr->Class)
				break;
			cls = static_cast<UClass*>(cls->BaseStruct);
		}
		if (!cls)
			value = nullptr;
	}
	*Out = ExpressionValue::ObjectValue(value);
}

void ExpressionEvaluator::Expr(Unknown0x15Expression* expr)
{
	// Klingon honor guard has this! (UE 251)
	//Frame::ThrowException("Unknown0x15 expression encountered");
	if (IsStatement())
		Result.Result = StatementResult::Stop;
}

void ExpressionEvaluator::Expr(SelfExpression* expr)
{
	*Out = ExpressionValue::ObjectValue(Self);
}

void ExpressionEvaluator::Expr(SkipExpression* expr)
{
	PassThrough(expr->Value, Context);
}

void ExpressionEvaluator::Expr(ContextExpression* expr)
{
	UObject* context = Value(expr->ObjectExpr).ToObject();
	if (context)
	{
		PassThrough(expr->ContextExpr, context);
	}
	else if (IsStatement())
	{
		Result.Result = StatementResult::AccessedNone;
	}
}

void ExpressionEvaluator::Expr(ArrayElementExpression* expr)
{
	int index = Value(expr->Index).ToInt();
	auto arrayval = Value(expr->Array);
	if (arrayval.IsVariable())
	{
		*Out = arrayval.ItemAt(index);
	}
	else
	{
		Frame::ThrowException("Array is not a variable in ArrayElementExpression");
	}
}

void ExpressionEvaluator::Expr(IntConstExpression* expr)
{
	*Out = ExpressionValue::IntValue(expr->Value);
}

void ExpressionEvaluator::Expr(FloatConstExpression* expr)
{
	*Out = ExpressionValue::FloatValue(expr->Value);
}

void ExpressionEvaluator::Expr(StringConstExpression* expr)
{
	*Out = ExpressionValue::StringValue(expr->Value);
}

void ExpressionEvaluator::Expr(ObjectConstExpression* expr)
{
	*Out = ExpressionValue::ObjectValue(expr->Object);
}

void ExpressionEvaluator::Expr(NameConstExpression* expr)
{
	*Out = ExpressionValue::NameValue(expr->Value);
}

void ExpressionEvaluator::Expr(RotationConstExpression* expr)
{
	*Out = ExpressionValue::RotatorValue({ expr->Pitch, expr->Yaw, expr->Roll });
}

void ExpressionEvaluator::Expr(VectorConstExpression* expr)
{
	*Out = ExpressionValue::VectorValue({ expr->X, expr->Y, expr->Z });
}

void ExpressionEvaluator::Expr(ByteConstExpression* expr)
{
	*Out = ExpressionValue::ByteValue(expr->Value);
}

void ExpressionEvaluator::Expr(IntZeroExpression* expr)
{
	*Out = ExpressionValue::IntValue(0);
}

void ExpressionEvaluator::Expr(IntOneExpression* expr)
{
	*Out = ExpressionValue::IntValue(1);
}

void ExpressionEvaluator::Expr(TrueExpression* expr)
{
	*Out = ExpressionValue::BoolValue(true);
}

void ExpressionEvaluator::Expr(FalseExpression* expr)
{
	*Out = ExpressionValue::BoolValue(false);
}

void ExpressionEvaluator::Expr(NativeParmExpression* expr)
{
	Frame::ThrowException("Native parm expression is not implemented");
}

void ExpressionEvaluator::Expr(NoObjectExpression* expr)
{
	*Out = ExpressionValue::ObjectValue(nullptr);
}

void ExpressionEvaluator::Expr(Unknown0x2bExpression* expr)
{
	PassThrough(expr->Value, Context); // This may have been a truncating instruction from back when strings had a fixed size (package version 61 and earlier)
}

void ExpressionEvaluator::Expr(IntConstByteExpression* expr)
{
	*Out = ExpressionValue::ByteValue(expr->Value);
}

void ExpressionEvaluator::Expr(BoolVariableExpression* expr)
{
	*Out = Value(expr->Variable);
}

void ExpressionEvaluator::Expr(DynamicCastExpression* expr)
{
	UObject* value = Value(expr->Value).ToObject();
	if (value && !value->IsA(expr->Class->Name))
		value = nullptr;
	*Out = ExpressionValue::ObjectValue(value);
}

void ExpressionEvaluator::Expr(IteratorExpression* expr)
{
	Value(expr->Value);
	std::unique_ptr<Iterator> iter = std::move(Frame::CreatedIterator);
	if (IsStatement())
	{
		Result.Result = StatementResult::Iterator;
		Result.Iter = std::move(iter);
		Result.JumpAddress = expr->Offset;
	}
}

void ExpressionEvaluator::Expr(IteratorPopExpression* expr)
{
	if (IsStatement())
		Result.Result = StatementResult::IteratorPop;
}

void ExpressionEvaluator::Expr(IteratorNextExpression* expr)
{
	if (IsStatement())
		Result.Result = StatementResult::IteratorNext;
}

void ExpressionEvaluator::Expr(StructCmpEqExpression* expr)
{
	ExpressionValue val1 = Value(expr->Value1);
	ExpressionValue val2 = Value(expr->Value2);
	*Out = ExpressionValue::BoolValue(val1.IsEqual(val2));
}

void ExpressionEvaluator::Expr(StructCmpNeExpression* expr)
{
	ExpressionValue val1 = Value(expr->Value1);
	ExpressionValue val2 = Value(expr->Value2);
	*Out = ExpressionValue::BoolValue(!val1.IsEqual(val2));
}

void ExpressionEvaluator::Expr(StructMemberExpression* expr)
{
	if (expr->Field)
		*Out = Value(expr->Value).Member(expr->Field);
	else
		Frame::ThrowException("Null field encountered in struct member expression");
}

void ExpressionEvaluator::Expr(UnicodeStringConstExpression* expr)
{
	std::string s;
	s.reserve(expr->Value.size());
	for (wchar_t c : expr->Value)
		s.push_back(c < 128 ? c : '?');
	*Out = ExpressionValue::StringValue(s);
}

void ExpressionEvaluator::Expr(RotatorToVectorExpression* expr)
{
	Rotator rot = Value(expr->Value).ToRotator();
	*Out = ExpressionValue::VectorValue(Coords::Rotation(rot).XAxis);
}

void ExpressionEvaluator::Expr(ByteToIntExpression* expr)
{
	*Out = ExpressionValue::IntValue(Value(expr->Value).ToByte());
}

void ExpressionEvaluator::Expr(ByteToBoolExpression* expr)
{
	*Out = ExpressionValue::BoolValue(Value(expr->Value).ToByte() != 0);
}

void ExpressionEvaluator::Expr(ByteToFloatExpression* expr)
{
	*Out = ExpressionValue::FloatValue(Value(expr->Value).ToByte());
}

void ExpressionEvaluator::Expr(IntToByteExpression* expr)
{
	*Out = ExpressionValue::ByteValue(Value(expr->Value).ToInt());
}

void ExpressionEvaluator::Expr(IntToBoolExpression* expr)
{
	*Out = ExpressionValue::BoolValue(Value(expr->Value).ToInt());
}

void ExpressionEvaluator::Expr(IntToFloatExpression* expr)
{
	*Out = ExpressionValue::FloatValue((float)Value(expr->Value).ToInt());
}

void ExpressionEvaluator::Expr(BoolToByteExpression* expr)
{
	*Out = ExpressionValue::ByteValue(Value(expr->Value).ToBool());
}

void ExpressionEvaluator::Expr(BoolToIntExpression* expr)
{
	*Out = ExpressionValue::IntValue(Value(expr->Value).ToBool());
}

void ExpressionEvaluator::Expr(BoolToFloatExpression* expr)
{
	*Out = ExpressionValue::FloatValue(Value(expr->Value).ToBool());
}

void ExpressionEvaluator::Expr(FloatToByteExpression* expr)
{
	*Out = ExpressionValue::ByteValue((int)Value(expr->Value).ToFloat());
}

void ExpressionEvaluator::Expr(FloatToIntExpression* expr)
{
	*Out = ExpressionValue::IntValue((int)Value(expr->Value).ToFloat());
}

void ExpressionEvaluator::Expr(FloatToBoolExpression* expr)
{
	*Out = ExpressionValue::BoolValue((bool)Value(expr->Value).ToFloat());
}

void ExpressionEvaluator::Expr(Unknown0x46Expression* expr)
{
	Frame::ThrowException("Unknown0x46 expression encountered");
}

void ExpressionEvaluator::Expr(ObjectToBoolExpression* expr)
{
	*Out = ExpressionValue::BoolValue(Value(expr->Value).ToObject() != nullptr);
}

void ExpressionEvaluator::Expr(NameToBoolExpression* expr)
{
	*Out = ExpressionValue::BoolValue(Value(expr->Value).ToName().IsNone() == false); // "None" is name index 0: no lookup by spelling
}

void ExpressionEvaluator::Expr(StringToByteExpression* expr)
{
	*Out = ExpressionValue::ByteValue(std::atoi(Value(expr->Value).ToString().c_str()));
}

void ExpressionEvaluator::Expr(StringToIntExpression* expr)
{
	*Out = ExpressionValue::IntValue(std::atoi(Value(expr->Value).ToString().c_str()));
}

void ExpressionEvaluator::Expr(StringToBoolExpression* expr)
{
	*Out = ExpressionValue::BoolValue(std::atoi(Value(expr->Value).ToString().c_str()));
}

void ExpressionEvaluator::Expr(StringToFloatExpression* expr)
{
	*Out = ExpressionValue::FloatValue((float)std::atof(Value(expr->Value).ToString().c_str()));
}

void ExpressionEvaluator::Expr(StringToVectorExpression* expr)
{
	std::string v = Value(expr->Value).ToString();
	auto pos1 = v.find_first_of(',');
	auto pos2 = v.find_first_of(',', pos1 + 1);
	if (pos1 != std::string::npos && pos2 != std::string::npos)
	{
		*Out = ExpressionValue::VectorValue({ (float)std::atof(v.substr(0, pos1).c_str()), (float)std::atof(v.substr(pos1 + 1, pos2 - pos1 - 1).c_str()), (float)std::atof(v.substr(pos2 + 1).c_str()) });
	}
	else
	{
		*Out = ExpressionValue::VectorValue({ 0.0f });
	}
}

void ExpressionEvaluator::Expr(StringToRotatorExpression* expr)
{
	std::string v = Value(expr->Value).ToString();
	auto pos1 = v.find_first_of(',');
	auto pos2 = v.find_first_of(',', pos1 + 1);
	if (pos1 != std::string::npos && pos2 != std::string::npos)
	{
		*Out = ExpressionValue::RotatorValue({ std::atoi(v.substr(0, pos1).c_str()), std::atoi(v.substr(pos1 + 1, pos2 - pos1 - 1).c_str()), std::atoi(v.substr(pos2 + 1).c_str()) });
	}
	else
	{
		*Out = ExpressionValue::RotatorValue({ 0, 0, 0 });
	}
}

void ExpressionEvaluator::Expr(VectorToBoolExpression* expr)
{
	*Out = ExpressionValue::BoolValue(Value(expr->Value).ToVector() != vec3(0.0f));
}

void ExpressionEvaluator::Expr(VectorToRotatorExpression* expr)
{
	*Out = ExpressionValue::RotatorValue(Rotator::FromVector(Value(expr->Value).ToVector()));
}

void ExpressionEvaluator::Expr(RotatorToBoolExpression* expr)
{
	*Out = ExpressionValue::BoolValue(Value(expr->Value).ToRotator() != Rotator(0, 0, 0));
}

void ExpressionEvaluator::Expr(ByteToStringExpression* expr)
{
	*Out = ExpressionValue::StringValue(std::to_string(Value(expr->Value).ToByte()));
}

void ExpressionEvaluator::Expr(IntToStringExpression* expr)
{
	*Out = ExpressionValue::StringValue(std::to_string(Value(expr->Value).ToInt()));
}

void ExpressionEvaluator::Expr(BoolToStringExpression* expr)
{
	*Out = ExpressionValue::StringValue(std::to_string(Value(expr->Value).ToBool()));
}

void ExpressionEvaluator::Expr(FloatToStringExpression* expr)
{
	*Out = ExpressionValue::StringValue(std::to_string(Value(expr->Value).ToFloat()));
}

void ExpressionEvaluator::Expr(ObjectToStringExpression* expr)
{
	UObject* obj = Value(expr->Value).ToObject();
	*Out = ExpressionValue::StringValue(obj ? obj->package->GetPackageName().ToString() + "." + obj->Name.ToString() : "None");
}

void ExpressionEvaluator::Expr(NameToStringExpression* expr)
{
	*Out = ExpressionValue::StringValue(Value(expr->Value).ToName().ToString());
}

void ExpressionEvaluator::Expr(VectorToStringExpression* expr)
{
	vec3 v = Value(expr->Value).ToVector();
	*Out = ExpressionValue::StringValue(std::to_string(v.x) + "," + std::to_string(v.y) + "," + std::to_string(v.z));
}

void ExpressionEvaluator::Expr(RotatorToStringExpression* expr)
{
	Rotator v = Value(expr->Value).ToRotator();
	*Out = ExpressionValue::StringValue(std::to_string(v.Pitch & 0xffff) + "," + std::to_string(v.Yaw & 0xffff) + "," + std::to_string(v.Roll & 0xffff));
}

void ExpressionEvaluator::Expr(StringToNameExpression* expr)
{
	std::string v = Value(expr->Value).ToString();
	*Out = ExpressionValue::NameValue(v);
}

void ExpressionEvaluator::Expr(DynArrayToIntExpression* expr)
{
	size_t count = Value(expr->Value).ToArray().GetSize();
	*Out = ExpressionValue::IntValue((int)count);
}

void ExpressionEvaluator::Expr(VirtualFunctionExpression* expr)
{
	UClass* contextClass = UObject::TryCast<UClass>(Context);
	if (!contextClass)
		contextClass = Context->Class;

	UFunction* func = FindScriptFunction(contextClass, Context->GetStateName(), expr->Name);
	if (func)
		Call(func, expr->Args);
	else
		Frame::ThrowException("Script virtual function " + expr->Name.ToString() + " not found!");
}

void ExpressionEvaluator::Expr(FinalFunctionExpression* expr)
{
	Call(expr->Func, expr->Args);
}

void ExpressionEvaluator::Expr(GlobalFunctionExpression* expr)
{
	// Global function calls skip the states and only searches normal member functions

	UClass* contextClass = UObject::TryCast<UClass>(Context);
	if (!contextClass)
		contextClass = Context->Class;

	for (UClass* cls = contextClass; cls != nullptr; cls = static_cast<UClass*>(cls->BaseStruct))
	{
		UFunction* func = cls->GetFunction(expr->Name);
		if (func)
		{
			Call(func, expr->Args);
			return;
		}
	}

	Frame::ThrowException("Script global function " + expr->Name.ToString() + " not found!");
}

void ExpressionEvaluator::Expr(NativeFunctionExpression* expr)
{
	Call(NativeFunctions::FuncByIndex[expr->nativeindex], expr->Args);
}

void ExpressionEvaluator::Expr(ConstructExpression* expr)
{
	Frame::ThrowException("Construct expression not implemented");
}

// The operators most script calls go to -- about 83% of the native calls on
// Liberty Island, and natives are 84% of all calls -- evaluated here instead
// of through Frame::Call, CallNative and a handler. Each does what its native
// in NObject.cpp does, with the same conversions, after every argument has
// been evaluated (in the caller's context), as the general path reads them.
// A function is matched by its native index and its name, so a game whose
// indexes differ keeps the general path.
enum class FastOp
{
	None,
	NotEqual_ObjectObject, EqualEqual_ObjectObject, Not_PreBool,
	Less_IntInt, LessEqual_IntInt, Greater_IntInt, GreaterEqual_IntInt, EqualEqual_IntInt, NotEqual_IntInt,
	Less_FloatFloat, LessEqual_FloatFloat, Greater_FloatFloat, GreaterEqual_FloatFloat, NotEqual_FloatFloat,
	Add_FloatFloat, Subtract_FloatFloat, Multiply_FloatFloat, Divide_FloatFloat, Subtract_IntInt,
	AddAdd_Int, AddEqual_IntInt, AddEqual_FloatFloat, SubtractEqual_FloatFloat,
	EqualEqual_NameName, NotEqual_NameName
};

static FastOp FindFastOperator(UFunction* func)
{
	struct Entry { int index; const char* name; FastOp op; };
	static const Entry entries[] =
	{
		{ 119, "NotEqual_ObjectObject", FastOp::NotEqual_ObjectObject },
		{ 114, "EqualEqual_ObjectObject", FastOp::EqualEqual_ObjectObject },
		{ 129, "Not_PreBool", FastOp::Not_PreBool },
		{ 150, "Less_IntInt", FastOp::Less_IntInt },
		{ 152, "LessEqual_IntInt", FastOp::LessEqual_IntInt },
		{ 151, "Greater_IntInt", FastOp::Greater_IntInt },
		{ 153, "GreaterEqual_IntInt", FastOp::GreaterEqual_IntInt },
		{ 154, "EqualEqual_IntInt", FastOp::EqualEqual_IntInt },
		{ 155, "NotEqual_IntInt", FastOp::NotEqual_IntInt },
		{ 176, "Less_FloatFloat", FastOp::Less_FloatFloat },
		{ 178, "LessEqual_FloatFloat", FastOp::LessEqual_FloatFloat },
		{ 177, "Greater_FloatFloat", FastOp::Greater_FloatFloat },
		{ 179, "GreaterEqual_FloatFloat", FastOp::GreaterEqual_FloatFloat },
		{ 181, "NotEqual_FloatFloat", FastOp::NotEqual_FloatFloat },
		{ 174, "Add_FloatFloat", FastOp::Add_FloatFloat },
		{ 175, "Subtract_FloatFloat", FastOp::Subtract_FloatFloat },
		{ 171, "Multiply_FloatFloat", FastOp::Multiply_FloatFloat },
		{ 172, "Divide_FloatFloat", FastOp::Divide_FloatFloat },
		{ 147, "Subtract_IntInt", FastOp::Subtract_IntInt },
		{ 165, "AddAdd_Int", FastOp::AddAdd_Int },
		{ 161, "AddEqual_IntInt", FastOp::AddEqual_IntInt },
		{ 184, "AddEqual_FloatFloat", FastOp::AddEqual_FloatFloat },
		{ 185, "SubtractEqual_FloatFloat", FastOp::SubtractEqual_FloatFloat },
		{ 254, "EqualEqual_NameName", FastOp::EqualEqual_NameName },
		{ 255, "NotEqual_NameName", FastOp::NotEqual_NameName },
	};
	if (AllFlags(func->FuncFlags, FunctionFlags::Native))
	{
		for (const Entry& entry : entries)
		{
			if (entry.index == func->NativeFuncIndex && func->Name == entry.name)
				return entry.op;
		}
	}
	return FastOp::None;
}

bool ExpressionEvaluator::CallFastOperator(UFunction* func, const Array<Expression*>& exprArgs)
{
	if (func->FastOperator < 0)
		func->FastOperator = (int)FindFastOperator(func);
	FastOp op = (FastOp)func->FastOperator;
	size_t arity = (op == FastOp::Not_PreBool || op == FastOp::AddAdd_Int) ? 1 : 2;
	if (op == FastOp::None || exprArgs.size() != arity || !Context)
		return false;

	UObject* context = Context;
	Context = Self;
	ExpressionValue a = Value(exprArgs[0]);
	ExpressionValue b = arity == 2 ? Value(exprArgs[1]) : ExpressionValue();
	Context = context;

	try
	{
		switch (op)
		{
		default: break;
		case FastOp::NotEqual_ObjectObject: *Out = ExpressionValue::BoolValue(a.ToObject() != b.ToObject()); break;
		case FastOp::EqualEqual_ObjectObject: *Out = ExpressionValue::BoolValue(a.ToObject() == b.ToObject()); break;
		case FastOp::Not_PreBool: *Out = ExpressionValue::BoolValue(!a.ToBool()); break;
		case FastOp::Less_IntInt: *Out = ExpressionValue::BoolValue(a.ToInt() < b.ToInt()); break;
		case FastOp::LessEqual_IntInt: *Out = ExpressionValue::BoolValue(a.ToInt() <= b.ToInt()); break;
		case FastOp::Greater_IntInt: *Out = ExpressionValue::BoolValue(a.ToInt() > b.ToInt()); break;
		case FastOp::GreaterEqual_IntInt: *Out = ExpressionValue::BoolValue(a.ToInt() >= b.ToInt()); break;
		case FastOp::EqualEqual_IntInt: *Out = ExpressionValue::BoolValue(a.ToInt() == b.ToInt()); break;
		case FastOp::NotEqual_IntInt: *Out = ExpressionValue::BoolValue(a.ToInt() != b.ToInt()); break;
		case FastOp::Less_FloatFloat: *Out = ExpressionValue::BoolValue(a.ToFloat() < b.ToFloat()); break;
		case FastOp::LessEqual_FloatFloat: *Out = ExpressionValue::BoolValue(a.ToFloat() <= b.ToFloat()); break;
		case FastOp::Greater_FloatFloat: *Out = ExpressionValue::BoolValue(a.ToFloat() > b.ToFloat()); break;
		case FastOp::GreaterEqual_FloatFloat: *Out = ExpressionValue::BoolValue(a.ToFloat() >= b.ToFloat()); break;
		case FastOp::NotEqual_FloatFloat: *Out = ExpressionValue::BoolValue(a.ToFloat() != b.ToFloat()); break;
		case FastOp::Add_FloatFloat: *Out = ExpressionValue::FloatValue(a.ToFloat() + b.ToFloat()); break;
		case FastOp::Subtract_FloatFloat: *Out = ExpressionValue::FloatValue(a.ToFloat() - b.ToFloat()); break;
		case FastOp::Multiply_FloatFloat: *Out = ExpressionValue::FloatValue(a.ToFloat() * b.ToFloat()); break;
		case FastOp::Divide_FloatFloat: *Out = ExpressionValue::FloatValue(a.ToFloat() / b.ToFloat()); break;
		case FastOp::Subtract_IntInt: *Out = ExpressionValue::IntValue(a.ToInt() - b.ToInt()); break;
		case FastOp::AddAdd_Int: { int32_t& A = a.ToType<int32_t&>(); *Out = ExpressionValue::IntValue(A++); break; }
		case FastOp::AddEqual_IntInt: { int32_t& A = a.ToType<int32_t&>(); int32_t B = b.ToInt(); *Out = ExpressionValue::IntValue(A += B); break; }
		case FastOp::AddEqual_FloatFloat: { float& A = a.ToType<float&>(); float B = b.ToFloat(); *Out = ExpressionValue::FloatValue(A += B); break; }
		case FastOp::SubtractEqual_FloatFloat: { float& A = a.ToType<float&>(); float B = b.ToFloat(); *Out = ExpressionValue::FloatValue(A -= B); break; }
		case FastOp::EqualEqual_NameName: *Out = ExpressionValue::BoolValue(a.ToName() == b.ToName()); break;
		case FastOp::NotEqual_NameName: *Out = ExpressionValue::BoolValue(a.ToName() != b.ToName()); break;
		}
		return true;
	}
	catch (...)
	{
	}

	// A conversion failed before anything was changed. The general path makes
	// the same conversions in the native's own frame, and reports and
	// recovers exactly as it always has.
	CallArguments args(CallArguments::Room(func, arity));
	args.push_back(std::move(a));
	if (arity == 2)
		args.push_back(std::move(b));
	*Out = Frame::Call(func, Context, args);
	return true;
}

void ExpressionEvaluator::Call(UFunction* func, const Array<Expression*>& exprArgs)
{
	if (CallFastOperator(func, exprArgs))
		return;

	if (func->NativeFuncIndex == 130)
	{
		*Out = ExpressionValue::BoolValue(Value(exprArgs[0], Self).ToBool() && Value(exprArgs[1], Self).ToBool());
	}
	else if (func->NativeFuncIndex == 132)
	{
		*Out = ExpressionValue::BoolValue(Value(exprArgs[0], Self).ToBool() || Value(exprArgs[1], Self).ToBool());
	}
	else
	{
		// Arguments are evaluated in the caller's own context; the call is made in this one
		CallArguments args(CallArguments::Room(func, exprArgs.size()));
		UObject* context = Context;
		Context = Self;
		for (Expression* arg : exprArgs)
			args.push_back(Value(arg));
		Context = context;
		*Out = Frame::Call(func, Context, args);
	}
}

void ExpressionEvaluator::Expr(FunctionArgumentsExpression* expr)
{
	*Out = ExpressionValue::NothingValue();
}
