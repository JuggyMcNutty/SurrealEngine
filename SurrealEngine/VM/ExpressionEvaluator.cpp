
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

using TypedKind = Expression::TypedKind;

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
	// The commonest leaves, made here as their Expr would make them. None can
	// throw, so none needs to be the StepExpression.
	switch (expr->Leaf)
	{
	default: break;
	case Expression::LeafKind::LocalVariable: return ExpressionValue::Variable(LocalVariables, static_cast<LocalVariableExpression*>(expr)->Variable);
	case Expression::LeafKind::InstanceVariable: return ExpressionValue::Variable(Context->PropertyData.Data, static_cast<InstanceVariableExpression*>(expr)->Variable);
	case Expression::LeafKind::BoolVariable: return Value(static_cast<BoolVariableExpression*>(expr)->Variable);
	case Expression::LeafKind::Self: return ExpressionValue::ObjectValue(Self);
	case Expression::LeafKind::NoObject: return ExpressionValue::ObjectValue(nullptr);
	case Expression::LeafKind::ObjectConst: return ExpressionValue::ObjectValue(static_cast<ObjectConstExpression*>(expr)->Object);
	case Expression::LeafKind::NameConst: return ExpressionValue::NameValue(static_cast<NameConstExpression*>(expr)->Value);
	case Expression::LeafKind::IntConst: return ExpressionValue::IntValue(static_cast<IntConstExpression*>(expr)->Value);
	case Expression::LeafKind::IntZero: return ExpressionValue::IntValue(0);
	case Expression::LeafKind::IntOne: return ExpressionValue::IntValue(1);
	case Expression::LeafKind::IntConstByte: return ExpressionValue::ByteValue(static_cast<IntConstByteExpression*>(expr)->Value);
	case Expression::LeafKind::ByteConst: return ExpressionValue::ByteValue(static_cast<ByteConstExpression*>(expr)->Value);
	case Expression::LeafKind::FloatConst: return ExpressionValue::FloatValue(static_cast<FloatConstExpression*>(expr)->Value);
	case Expression::LeafKind::True: return ExpressionValue::BoolValue(true);
	case Expression::LeafKind::False: return ExpressionValue::BoolValue(false);
	}
	return VisitValue(expr);
}

ExpressionValue ExpressionEvaluator::VisitValue(Expression* expr)
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
	if (!EvalBool(expr->Condition) && IsStatement())
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
	if (IsStatement() && Assign(expr->LeftSide, expr->RightSide))
		return;

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
	if (IsStatement() && Assign(expr->LeftSide, expr->RightSide))
		return;

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
	if (expr->Typed == TypedKind::Unknown)
		Classify(expr);

	// The general path takes a None context to Frame::Call, which reports it
	if (expr->Typed != TypedKind::Generic && Context)
		*Out = OperatorValue(expr);
	else
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
static TypedKind FindFastOperator(UFunction* func)
{
	struct Entry { int index; const char* name; TypedKind op; };
	static const Entry entries[] =
	{
		{ 119, "NotEqual_ObjectObject", TypedKind::NotEqual_ObjectObject },
		{ 114, "EqualEqual_ObjectObject", TypedKind::EqualEqual_ObjectObject },
		{ 129, "Not_PreBool", TypedKind::Not_PreBool },
		{ 150, "Less_IntInt", TypedKind::Less_IntInt },
		{ 152, "LessEqual_IntInt", TypedKind::LessEqual_IntInt },
		{ 151, "Greater_IntInt", TypedKind::Greater_IntInt },
		{ 153, "GreaterEqual_IntInt", TypedKind::GreaterEqual_IntInt },
		{ 154, "EqualEqual_IntInt", TypedKind::EqualEqual_IntInt },
		{ 155, "NotEqual_IntInt", TypedKind::NotEqual_IntInt },
		{ 176, "Less_FloatFloat", TypedKind::Less_FloatFloat },
		{ 178, "LessEqual_FloatFloat", TypedKind::LessEqual_FloatFloat },
		{ 177, "Greater_FloatFloat", TypedKind::Greater_FloatFloat },
		{ 179, "GreaterEqual_FloatFloat", TypedKind::GreaterEqual_FloatFloat },
		{ 181, "NotEqual_FloatFloat", TypedKind::NotEqual_FloatFloat },
		{ 174, "Add_FloatFloat", TypedKind::Add_FloatFloat },
		{ 175, "Subtract_FloatFloat", TypedKind::Subtract_FloatFloat },
		{ 171, "Multiply_FloatFloat", TypedKind::Multiply_FloatFloat },
		{ 172, "Divide_FloatFloat", TypedKind::Divide_FloatFloat },
		{ 147, "Subtract_IntInt", TypedKind::Subtract_IntInt },
		{ 165, "AddAdd_Int", TypedKind::AddAdd_Int },
		{ 161, "AddEqual_IntInt", TypedKind::AddEqual_IntInt },
		{ 184, "AddEqual_FloatFloat", TypedKind::AddEqual_FloatFloat },
		{ 185, "SubtractEqual_FloatFloat", TypedKind::SubtractEqual_FloatFloat },
		{ 254, "EqualEqual_NameName", TypedKind::EqualEqual_NameName },
		{ 255, "NotEqual_NameName", TypedKind::NotEqual_NameName },
	};
	if (AllFlags(func->FuncFlags, FunctionFlags::Native))
	{
		for (const Entry& entry : entries)
		{
			if (entry.index == func->NativeFuncIndex && func->Name == entry.name)
				return entry.op;
		}
	}
	return TypedKind::Generic;
}

bool ExpressionEvaluator::CallFastOperator(UFunction* func, const Array<Expression*>& exprArgs)
{
	if (func->FastOperator < 0)
		func->FastOperator = (int)FindFastOperator(func);
	TypedKind op = (TypedKind)func->FastOperator;
	size_t arity = (op == TypedKind::Not_PreBool || op == TypedKind::AddAdd_Int) ? 1 : 2;
	if (op == TypedKind::Generic || exprArgs.size() != arity || !Context)
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
		case TypedKind::NotEqual_ObjectObject: *Out = ExpressionValue::BoolValue(a.ToObject() != b.ToObject()); break;
		case TypedKind::EqualEqual_ObjectObject: *Out = ExpressionValue::BoolValue(a.ToObject() == b.ToObject()); break;
		case TypedKind::Not_PreBool: *Out = ExpressionValue::BoolValue(!a.ToBool()); break;
		case TypedKind::Less_IntInt: *Out = ExpressionValue::BoolValue(a.ToInt() < b.ToInt()); break;
		case TypedKind::LessEqual_IntInt: *Out = ExpressionValue::BoolValue(a.ToInt() <= b.ToInt()); break;
		case TypedKind::Greater_IntInt: *Out = ExpressionValue::BoolValue(a.ToInt() > b.ToInt()); break;
		case TypedKind::GreaterEqual_IntInt: *Out = ExpressionValue::BoolValue(a.ToInt() >= b.ToInt()); break;
		case TypedKind::EqualEqual_IntInt: *Out = ExpressionValue::BoolValue(a.ToInt() == b.ToInt()); break;
		case TypedKind::NotEqual_IntInt: *Out = ExpressionValue::BoolValue(a.ToInt() != b.ToInt()); break;
		case TypedKind::Less_FloatFloat: *Out = ExpressionValue::BoolValue(a.ToFloat() < b.ToFloat()); break;
		case TypedKind::LessEqual_FloatFloat: *Out = ExpressionValue::BoolValue(a.ToFloat() <= b.ToFloat()); break;
		case TypedKind::Greater_FloatFloat: *Out = ExpressionValue::BoolValue(a.ToFloat() > b.ToFloat()); break;
		case TypedKind::GreaterEqual_FloatFloat: *Out = ExpressionValue::BoolValue(a.ToFloat() >= b.ToFloat()); break;
		case TypedKind::NotEqual_FloatFloat: *Out = ExpressionValue::BoolValue(a.ToFloat() != b.ToFloat()); break;
		case TypedKind::Add_FloatFloat: *Out = ExpressionValue::FloatValue(a.ToFloat() + b.ToFloat()); break;
		case TypedKind::Subtract_FloatFloat: *Out = ExpressionValue::FloatValue(a.ToFloat() - b.ToFloat()); break;
		case TypedKind::Multiply_FloatFloat: *Out = ExpressionValue::FloatValue(a.ToFloat() * b.ToFloat()); break;
		case TypedKind::Divide_FloatFloat: *Out = ExpressionValue::FloatValue(a.ToFloat() / b.ToFloat()); break;
		case TypedKind::Subtract_IntInt: *Out = ExpressionValue::IntValue(a.ToInt() - b.ToInt()); break;
		case TypedKind::AddAdd_Int: { int32_t& A = a.ToType<int32_t&>(); *Out = ExpressionValue::IntValue(A++); break; }
		case TypedKind::AddEqual_IntInt: { int32_t& A = a.ToType<int32_t&>(); int32_t B = b.ToInt(); *Out = ExpressionValue::IntValue(A += B); break; }
		case TypedKind::AddEqual_FloatFloat: { float& A = a.ToType<float&>(); float B = b.ToFloat(); *Out = ExpressionValue::FloatValue(A += B); break; }
		case TypedKind::SubtractEqual_FloatFloat: { float& A = a.ToType<float&>(); float B = b.ToFloat(); *Out = ExpressionValue::FloatValue(A -= B); break; }
		case TypedKind::EqualEqual_NameName: *Out = ExpressionValue::BoolValue(a.ToName() == b.ToName()); break;
		case TypedKind::NotEqual_NameName: *Out = ExpressionValue::BoolValue(a.ToName() != b.ToName()); break;
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
		*Out = ExpressionValue::BoolValue(EvalBool(exprArgs[0], Self) && EvalBool(exprArgs[1], Self));
	}
	else if (func->NativeFuncIndex == 132)
	{
		*Out = ExpressionValue::BoolValue(EvalBool(exprArgs[0], Self) || EvalBool(exprArgs[1], Self));
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

/////////////////////////////////////////////////////////////////////////////
// Typed evaluation
//
// Nearly half of all statements are conditions (JumpIfNot), and most of what
// they and the fast operators evaluate are variables, constants, conversions
// and other operators. Each such node's value went into an 88-byte
// ExpressionValue -- constructed, moved up by assignment through a type
// switch, converted, destructed -- to give a bool, a number or an object.
// The typed evaluators read and compute those directly. What each gives is
// exactly what Value(expr).ToBool() (ToByte, ToInt, ToFloat, ToObject,
// ToName) gives, with the same C++ conversions; a Generic node, one no typed
// kind covers, goes through exactly that.
//
// Operators get typed kinds only when that changes nothing: every operand's
// type is one the operator's conversions accept, so nothing can throw where
// the general path would have caught it. And the general path reads every
// operand once all of them are evaluated, so a variable operand is read after
// the operand after it is evaluated (which might change it).

static bool IsVariable(TypedKind kind)
{
	return kind >= TypedKind::LocalByte && kind <= TypedKind::InstanceBoolVariable;
}

// The type of value a typed kind yields; Nothing for Generic
static ExpressionValueType TypedValueType(TypedKind kind)
{
	switch (kind)
	{
	default:
		return ExpressionValueType::Nothing;
	case TypedKind::LocalByte: case TypedKind::InstanceByte: case TypedKind::IntConstByte: case TypedKind::ByteConst:
	case TypedKind::IntToByte: case TypedKind::BoolToByte: case TypedKind::FloatToByte:
		return ExpressionValueType::ValueByte;
	case TypedKind::LocalInt: case TypedKind::InstanceInt: case TypedKind::IntConst: case TypedKind::IntZero: case TypedKind::IntOne:
	case TypedKind::ByteToInt: case TypedKind::BoolToInt: case TypedKind::FloatToInt:
	case TypedKind::Subtract_IntInt: case TypedKind::AddAdd_Int: case TypedKind::AddEqual_IntInt:
		return ExpressionValueType::ValueInt;
	case TypedKind::LocalFloat: case TypedKind::InstanceFloat: case TypedKind::FloatConst:
	case TypedKind::ByteToFloat: case TypedKind::IntToFloat: case TypedKind::BoolToFloat:
	case TypedKind::Add_FloatFloat: case TypedKind::Subtract_FloatFloat: case TypedKind::Multiply_FloatFloat: case TypedKind::Divide_FloatFloat:
	case TypedKind::AddEqual_FloatFloat: case TypedKind::SubtractEqual_FloatFloat:
		return ExpressionValueType::ValueFloat;
	case TypedKind::LocalObject: case TypedKind::InstanceObject: case TypedKind::Self: case TypedKind::NoObject: case TypedKind::ObjectConst:
		return ExpressionValueType::ValueObject;
	case TypedKind::LocalName: case TypedKind::InstanceName: case TypedKind::NameConst:
		return ExpressionValueType::ValueName;
	case TypedKind::LocalBool: case TypedKind::InstanceBool: case TypedKind::LocalBoolVariable: case TypedKind::InstanceBoolVariable:
	case TypedKind::True: case TypedKind::False:
	case TypedKind::ByteToBool: case TypedKind::IntToBool: case TypedKind::FloatToBool: case TypedKind::ObjectToBool: case TypedKind::NameToBool:
	case TypedKind::AndAnd: case TypedKind::OrOr: case TypedKind::Not_PreBool:
	case TypedKind::NotEqual_ObjectObject: case TypedKind::EqualEqual_ObjectObject:
	case TypedKind::Less_IntInt: case TypedKind::LessEqual_IntInt: case TypedKind::Greater_IntInt: case TypedKind::GreaterEqual_IntInt:
	case TypedKind::EqualEqual_IntInt: case TypedKind::NotEqual_IntInt:
	case TypedKind::Less_FloatFloat: case TypedKind::LessEqual_FloatFloat: case TypedKind::Greater_FloatFloat: case TypedKind::GreaterEqual_FloatFloat:
	case TypedKind::NotEqual_FloatFloat:
	case TypedKind::EqualEqual_NameName: case TypedKind::NotEqual_NameName:
		return ExpressionValueType::ValueBool;
	}
}

static bool IsNumber(ExpressionValueType type)
{
	return type == ExpressionValueType::ValueByte || type == ExpressionValueType::ValueInt || type == ExpressionValueType::ValueFloat;
}

// A local or instance variable's kind: first is LocalByte or InstanceByte
static TypedKind VariableKind(UProperty* prop, TypedKind first)
{
	int index;
	switch (prop->ValueType)
	{
	default: return TypedKind::Generic;
	case ExpressionValueType::ValueByte: index = 0; break;
	case ExpressionValueType::ValueInt: index = 1; break;
	case ExpressionValueType::ValueBool: index = 2; break;
	case ExpressionValueType::ValueFloat: index = 3; break;
	case ExpressionValueType::ValueObject: index = 4; break;
	case ExpressionValueType::ValueName: index = 5; break;
	}
	return (TypedKind)((int)first + index);
}

TypedKind ExpressionEvaluator::OperatorKind(NativeFunctionExpression* expr)
{
	UFunction* func = (size_t)expr->nativeindex < NativeFunctions::FuncByIndex.size() ? NativeFunctions::FuncByIndex[expr->nativeindex] : nullptr;
	if (!func)
		return TypedKind::Generic;

	const Array<Expression*>& args = expr->Args;
	if (func->FastOperator < 0)
		func->FastOperator = (int)FindFastOperator(func);
	TypedKind op = (TypedKind)func->FastOperator;
	if (op == TypedKind::Generic)
	{
		// As ExpressionEvaluator::Call recognises them
		if ((func->NativeFuncIndex != 130 && func->NativeFuncIndex != 132) || args.size() != 2)
			return TypedKind::Generic;
		expr->Operands[0] = args[0];
		expr->Operands[1] = args[1];
		return func->NativeFuncIndex == 130 ? TypedKind::AndAnd : TypedKind::OrOr;
	}

	size_t arity = (op == TypedKind::Not_PreBool || op == TypedKind::AddAdd_Int) ? 1 : 2;
	if (args.size() != arity)
		return TypedKind::Generic;
	for (Expression* arg : args)
	{
		if (arg->Typed == TypedKind::Unknown)
			Classify(arg);
	}

	TypedKind a = args[0]->Typed;
	ExpressionValueType typeA = TypedValueType(a);
	ExpressionValueType typeB = arity == 2 ? TypedValueType(args[1]->Typed) : ExpressionValueType::Nothing;
	bool typed;
	switch (op)
	{
	case TypedKind::NotEqual_ObjectObject:
	case TypedKind::EqualEqual_ObjectObject:
		typed = typeA == ExpressionValueType::ValueObject && typeB == ExpressionValueType::ValueObject;
		break;
	case TypedKind::EqualEqual_NameName:
	case TypedKind::NotEqual_NameName:
		typed = typeA == ExpressionValueType::ValueName && typeB == ExpressionValueType::ValueName;
		break;
	case TypedKind::Not_PreBool:
		typed = typeA == ExpressionValueType::ValueBool;
		break;
	// ++, += and -= change a variable of exactly their type (ToType<int32_t&>, ToType<float&>)
	case TypedKind::AddAdd_Int:
		typed = a == TypedKind::LocalInt || a == TypedKind::InstanceInt;
		break;
	case TypedKind::AddEqual_IntInt:
		typed = (a == TypedKind::LocalInt || a == TypedKind::InstanceInt) && IsNumber(typeB);
		break;
	case TypedKind::AddEqual_FloatFloat:
	case TypedKind::SubtractEqual_FloatFloat:
		typed = (a == TypedKind::LocalFloat || a == TypedKind::InstanceFloat) && IsNumber(typeB);
		break;
	default:
		typed = IsNumber(typeA) && IsNumber(typeB);
		break;
	}
	if (!typed)
		return TypedKind::Generic;
	expr->Operands[0] = args[0];
	expr->Operands[1] = arity == 2 ? args[1] : nullptr;
	return op;
}

void ExpressionEvaluator::Classify(Expression* expr)
{
	TypedKind kind = TypedKind::Generic;
	switch (expr->Leaf)
	{
	case Expression::LeafKind::LocalVariable:
		{
			auto variable = static_cast<LocalVariableExpression*>(expr);
			kind = VariableKind(variable->Variable, TypedKind::LocalByte);
			variable->Offset = (uint32_t)variable->Variable->DataOffset.DataOffset;
			variable->Mask = variable->Variable->DataOffset.BitfieldMask;
			break;
		}
	case Expression::LeafKind::InstanceVariable:
		{
			auto variable = static_cast<InstanceVariableExpression*>(expr);
			kind = VariableKind(variable->Variable, TypedKind::InstanceByte);
			variable->Offset = (uint32_t)variable->Variable->DataOffset.DataOffset;
			variable->Mask = variable->Variable->DataOffset.BitfieldMask;
			break;
		}
	case Expression::LeafKind::BoolVariable:
		{
			auto boolVariable = static_cast<BoolVariableExpression*>(expr);
			Expression* variable = boolVariable->Variable;
			if (variable->Typed == TypedKind::Unknown)
				Classify(variable);
			if (variable->Typed == TypedKind::LocalBool)
			{
				kind = TypedKind::LocalBoolVariable;
				boolVariable->Offset = static_cast<LocalVariableExpression*>(variable)->Offset;
				boolVariable->Mask = static_cast<LocalVariableExpression*>(variable)->Mask;
			}
			else if (variable->Typed == TypedKind::InstanceBool)
			{
				kind = TypedKind::InstanceBoolVariable;
				boolVariable->Offset = static_cast<InstanceVariableExpression*>(variable)->Offset;
				boolVariable->Mask = static_cast<InstanceVariableExpression*>(variable)->Mask;
			}
			break;
		}
	case Expression::LeafKind::Self: kind = TypedKind::Self; break;
	case Expression::LeafKind::NoObject: kind = TypedKind::NoObject; break;
	case Expression::LeafKind::ObjectConst: kind = TypedKind::ObjectConst; break;
	case Expression::LeafKind::NameConst: kind = TypedKind::NameConst; break;
	case Expression::LeafKind::IntConst: kind = TypedKind::IntConst; break;
	case Expression::LeafKind::IntZero: kind = TypedKind::IntZero; break;
	case Expression::LeafKind::IntOne: kind = TypedKind::IntOne; break;
	case Expression::LeafKind::IntConstByte: kind = TypedKind::IntConstByte; break;
	case Expression::LeafKind::ByteConst: kind = TypedKind::ByteConst; break;
	case Expression::LeafKind::FloatConst: kind = TypedKind::FloatConst; break;
	case Expression::LeafKind::True: kind = TypedKind::True; break;
	case Expression::LeafKind::False: kind = TypedKind::False; break;
	case Expression::LeafKind::Other:
		if (dynamic_cast<SkipExpression*>(expr)) kind = TypedKind::Skip;
		else if (dynamic_cast<ByteToIntExpression*>(expr)) kind = TypedKind::ByteToInt;
		else if (dynamic_cast<ByteToBoolExpression*>(expr)) kind = TypedKind::ByteToBool;
		else if (dynamic_cast<ByteToFloatExpression*>(expr)) kind = TypedKind::ByteToFloat;
		else if (dynamic_cast<IntToByteExpression*>(expr)) kind = TypedKind::IntToByte;
		else if (dynamic_cast<IntToBoolExpression*>(expr)) kind = TypedKind::IntToBool;
		else if (dynamic_cast<IntToFloatExpression*>(expr)) kind = TypedKind::IntToFloat;
		else if (dynamic_cast<BoolToByteExpression*>(expr)) kind = TypedKind::BoolToByte;
		else if (dynamic_cast<BoolToIntExpression*>(expr)) kind = TypedKind::BoolToInt;
		else if (dynamic_cast<BoolToFloatExpression*>(expr)) kind = TypedKind::BoolToFloat;
		else if (dynamic_cast<FloatToByteExpression*>(expr)) kind = TypedKind::FloatToByte;
		else if (dynamic_cast<FloatToIntExpression*>(expr)) kind = TypedKind::FloatToInt;
		else if (dynamic_cast<FloatToBoolExpression*>(expr)) kind = TypedKind::FloatToBool;
		else if (dynamic_cast<ObjectToBoolExpression*>(expr)) kind = TypedKind::ObjectToBool;
		else if (dynamic_cast<NameToBoolExpression*>(expr)) kind = TypedKind::NameToBool;
		else if (auto native = dynamic_cast<NativeFunctionExpression*>(expr)) kind = OperatorKind(native);
		break;
	}
	expr->Typed = kind;
}


#if defined(_MSC_VER)
#define VM_NOINLINE __declspec(noinline)
#else
#define VM_NOINLINE __attribute__((noinline))
#endif

template<typename E> uint8_t* ExpressionEvaluator::Local(Expression* expr) const
{
	return static_cast<uint8_t*>(LocalVariables) + static_cast<E*>(expr)->Offset;
}

template<typename E> uint8_t* ExpressionEvaluator::Instance(Expression* expr) const
{
	return static_cast<uint8_t*>(Context->PropertyData.Data) + static_cast<E*>(expr)->Offset;
}

template<typename E> static bool GetBool(const uint8_t* ptr, Expression* expr)
{
	return (*reinterpret_cast<const uint32_t*>(ptr) & static_cast<E*>(expr)->Mask) != 0;
}

template<typename E> static void SetBool(uint8_t* ptr, Expression* expr, bool value)
{
	BitfieldBool b = { reinterpret_cast<uint32_t*>(ptr), static_cast<E*>(expr)->Mask };
	b.Set(value);
}

template<typename E> static Expression* Operand(Expression* expr) { return static_cast<E*>(expr)->Value; }
static Expression* const* OperandsOf(Expression* expr) { return static_cast<NativeFunctionExpression*>(expr)->Operands; }

using LocalVar = LocalVariableExpression;
using InstanceVar = InstanceVariableExpression;
using BoolVar = BoolVariableExpression;

VM_NOINLINE bool ExpressionEvaluator::GenericBool(Expression* expr) { return Value(expr).ToBool(); }
VM_NOINLINE UObject* ExpressionEvaluator::GenericObject(Expression* expr) { return Value(expr).ToObject(); }
VM_NOINLINE NameString ExpressionEvaluator::GenericName(Expression* expr) { return Value(expr).ToName(); }
template<> VM_NOINLINE uint8_t ExpressionEvaluator::GenericNumber(Expression* expr) { return Value(expr).ToByte(); }
template<> VM_NOINLINE int32_t ExpressionEvaluator::GenericNumber(Expression* expr) { return Value(expr).ToInt(); }
template<> VM_NOINLINE float ExpressionEvaluator::GenericNumber(Expression* expr) { return Value(expr).ToFloat(); }

bool ExpressionEvaluator::EvalBool(Expression* expr, UObject* context)
{
	UObject* oldContext = Context;
	Context = context;
	bool value = EvalBool(expr);
	Context = oldContext;
	return value;
}

bool ExpressionEvaluator::EvalBool(Expression* expr)
{
	switch (expr->Typed)
	{
	default: break;
	case TypedKind::Unknown: Classify(expr); return EvalBool(expr);
	case TypedKind::LocalBool: return GetBool<LocalVar>(Local<LocalVar>(expr), expr);
	case TypedKind::InstanceBool: return GetBool<InstanceVar>(Instance<InstanceVar>(expr), expr);
	case TypedKind::LocalBoolVariable: return GetBool<BoolVar>(Local<BoolVar>(expr), expr);
	case TypedKind::InstanceBoolVariable: return GetBool<BoolVar>(Instance<BoolVar>(expr), expr);
	case TypedKind::True: return true;
	case TypedKind::False: return false;
	case TypedKind::Skip: return EvalBool(Operand<SkipExpression>(expr));
	case TypedKind::ByteToBool: return EvalNumber<uint8_t>(Operand<ByteToBoolExpression>(expr)) != 0;
	case TypedKind::IntToBool: return EvalNumber<int32_t>(Operand<IntToBoolExpression>(expr)) != 0;
	case TypedKind::FloatToBool: return (bool)EvalNumber<float>(Operand<FloatToBoolExpression>(expr));
	case TypedKind::ObjectToBool: return EvalObject(Operand<ObjectToBoolExpression>(expr)) != nullptr;
	case TypedKind::NameToBool: return EvalName(Operand<NameToBoolExpression>(expr)).IsNone() == false;
	case TypedKind::AndAnd: return EvalBool(OperandsOf(expr)[0], Self) && EvalBool(OperandsOf(expr)[1], Self);
	case TypedKind::OrOr: return EvalBool(OperandsOf(expr)[0], Self) || EvalBool(OperandsOf(expr)[1], Self);
	case TypedKind::Not_PreBool: if (!Context) break; return !EvalBool(OperandsOf(expr)[0], Self);
	case TypedKind::NotEqual_ObjectObject: if (!Context) break; { UObject* a; UObject* b; ObjectOperands(expr, a, b); return a != b; }
	case TypedKind::EqualEqual_ObjectObject: if (!Context) break; { UObject* a; UObject* b; ObjectOperands(expr, a, b); return a == b; }
	case TypedKind::EqualEqual_NameName: if (!Context) break; { NameString a, b; NameOperands(expr, a, b); return a == b; }
	case TypedKind::NotEqual_NameName: if (!Context) break; { NameString a, b; NameOperands(expr, a, b); return a != b; }
	case TypedKind::Less_IntInt: if (!Context) break; { int32_t a, b; Operands(expr, a, b); return a < b; }
	case TypedKind::LessEqual_IntInt: if (!Context) break; { int32_t a, b; Operands(expr, a, b); return a <= b; }
	case TypedKind::Greater_IntInt: if (!Context) break; { int32_t a, b; Operands(expr, a, b); return a > b; }
	case TypedKind::GreaterEqual_IntInt: if (!Context) break; { int32_t a, b; Operands(expr, a, b); return a >= b; }
	case TypedKind::EqualEqual_IntInt: if (!Context) break; { int32_t a, b; Operands(expr, a, b); return a == b; }
	case TypedKind::NotEqual_IntInt: if (!Context) break; { int32_t a, b; Operands(expr, a, b); return a != b; }
	case TypedKind::Less_FloatFloat: if (!Context) break; { float a, b; Operands(expr, a, b); return a < b; }
	case TypedKind::LessEqual_FloatFloat: if (!Context) break; { float a, b; Operands(expr, a, b); return a <= b; }
	case TypedKind::Greater_FloatFloat: if (!Context) break; { float a, b; Operands(expr, a, b); return a > b; }
	case TypedKind::GreaterEqual_FloatFloat: if (!Context) break; { float a, b; Operands(expr, a, b); return a >= b; }
	case TypedKind::NotEqual_FloatFloat: if (!Context) break; { float a, b; Operands(expr, a, b); return a != b; }
	}
	return GenericBool(expr);
}

template<typename T> T ExpressionEvaluator::EvalNumber(Expression* expr)
{
	// Each value is made in its own type, as Value makes it, then converted
	// with a C++ cast, as ToByte, ToInt and ToFloat convert
	switch (expr->Typed)
	{
	default: break;
	case TypedKind::Unknown: Classify(expr); return EvalNumber<T>(expr);
	case TypedKind::LocalByte: return (T)*Local<LocalVar>(expr);
	case TypedKind::LocalInt: return (T)*reinterpret_cast<int32_t*>(Local<LocalVar>(expr));
	case TypedKind::LocalFloat: return (T)*reinterpret_cast<float*>(Local<LocalVar>(expr));
	case TypedKind::InstanceByte: return (T)*Instance<InstanceVar>(expr);
	case TypedKind::InstanceInt: return (T)*reinterpret_cast<int32_t*>(Instance<InstanceVar>(expr));
	case TypedKind::InstanceFloat: return (T)*reinterpret_cast<float*>(Instance<InstanceVar>(expr));
	case TypedKind::IntConst: return (T)(int32_t)static_cast<IntConstExpression*>(expr)->Value;
	case TypedKind::IntZero: return (T)(int32_t)0;
	case TypedKind::IntOne: return (T)(int32_t)1;
	case TypedKind::IntConstByte: return (T)static_cast<IntConstByteExpression*>(expr)->Value;
	case TypedKind::ByteConst: return (T)static_cast<ByteConstExpression*>(expr)->Value;
	case TypedKind::FloatConst: return (T)static_cast<FloatConstExpression*>(expr)->Value;
	case TypedKind::Skip: return EvalNumber<T>(Operand<SkipExpression>(expr));
	case TypedKind::ByteToInt: return (T)(int32_t)EvalNumber<uint8_t>(Operand<ByteToIntExpression>(expr));
	case TypedKind::ByteToFloat: return (T)(float)EvalNumber<uint8_t>(Operand<ByteToFloatExpression>(expr));
	case TypedKind::IntToByte: return (T)(uint8_t)EvalNumber<int32_t>(Operand<IntToByteExpression>(expr));
	case TypedKind::IntToFloat: return (T)(float)EvalNumber<int32_t>(Operand<IntToFloatExpression>(expr));
	case TypedKind::BoolToByte: return (T)(uint8_t)EvalBool(Operand<BoolToByteExpression>(expr));
	case TypedKind::BoolToInt: return (T)(int32_t)EvalBool(Operand<BoolToIntExpression>(expr));
	case TypedKind::BoolToFloat: return (T)(float)EvalBool(Operand<BoolToFloatExpression>(expr));
	case TypedKind::FloatToByte: return (T)(uint8_t)(int)EvalNumber<float>(Operand<FloatToByteExpression>(expr));
	case TypedKind::FloatToInt: return (T)(int32_t)(int)EvalNumber<float>(Operand<FloatToIntExpression>(expr));
	case TypedKind::Add_FloatFloat: if (!Context) break; { float a, b; Operands(expr, a, b); return (T)(a + b); }
	case TypedKind::Subtract_FloatFloat: if (!Context) break; { float a, b; Operands(expr, a, b); return (T)(a - b); }
	case TypedKind::Multiply_FloatFloat: if (!Context) break; { float a, b; Operands(expr, a, b); return (T)(a * b); }
	case TypedKind::Divide_FloatFloat: if (!Context) break; { float a, b; Operands(expr, a, b); return (T)(a / b); }
	case TypedKind::Subtract_IntInt: if (!Context) break; { int32_t a, b; Operands(expr, a, b); return (T)(a - b); }
	case TypedKind::AddAdd_Int: if (!Context) break; { int32_t& a = Target<int32_t>(expr); return (T)(a++); }
	case TypedKind::AddEqual_IntInt:
		if (!Context) break;
		{
			UObject* context = Context;
			Context = Self;
			int32_t b = EvalNumber<int32_t>(OperandsOf(expr)[1]);
			Context = context;
			int32_t& a = Target<int32_t>(expr);
			return (T)(a += b);
		}
	case TypedKind::AddEqual_FloatFloat:
	case TypedKind::SubtractEqual_FloatFloat:
		if (!Context) break;
		{
			UObject* context = Context;
			Context = Self;
			float b = EvalNumber<float>(OperandsOf(expr)[1]);
			Context = context;
			float& a = Target<float>(expr);
			return (T)(expr->Typed == TypedKind::AddEqual_FloatFloat ? (a += b) : (a -= b));
		}
	}
	return GenericNumber<T>(expr);
}

UObject* ExpressionEvaluator::EvalObject(Expression* expr)
{
	switch (expr->Typed)
	{
	default: break;
	case TypedKind::Unknown: Classify(expr); return EvalObject(expr);
	case TypedKind::LocalObject: return *reinterpret_cast<UObject**>(Local<LocalVar>(expr));
	case TypedKind::InstanceObject: return *reinterpret_cast<UObject**>(Instance<InstanceVar>(expr));
	case TypedKind::Self: return Self;
	case TypedKind::NoObject: return nullptr;
	case TypedKind::ObjectConst: return static_cast<ObjectConstExpression*>(expr)->Object;
	case TypedKind::Skip: return EvalObject(Operand<SkipExpression>(expr));
	}
	return GenericObject(expr);
}

NameString ExpressionEvaluator::EvalName(Expression* expr)
{
	switch (expr->Typed)
	{
	default: break;
	case TypedKind::Unknown: Classify(expr); return EvalName(expr);
	case TypedKind::LocalName: return *reinterpret_cast<NameString*>(Local<LocalVar>(expr));
	case TypedKind::InstanceName: return *reinterpret_cast<NameString*>(Instance<InstanceVar>(expr));
	case TypedKind::NameConst: return static_cast<NameConstExpression*>(expr)->Value;
	case TypedKind::Skip: return EvalName(Operand<SkipExpression>(expr));
	}
	return GenericName(expr);
}

template<typename T> void ExpressionEvaluator::Operands(Expression* expr, T& a, T& b)
{
	Expression* const* args = OperandsOf(expr);
	UObject* context = Context;
	Context = Self;
	if (IsVariable(args[0]->Typed))
	{
		b = EvalNumber<T>(args[1]);
		a = EvalNumber<T>(args[0]);
	}
	else
	{
		a = EvalNumber<T>(args[0]);
		b = EvalNumber<T>(args[1]);
	}
	Context = context;
}

void ExpressionEvaluator::ObjectOperands(Expression* expr, UObject*& a, UObject*& b)
{
	Expression* const* args = OperandsOf(expr);
	UObject* context = Context;
	Context = Self;
	if (IsVariable(args[0]->Typed))
	{
		b = EvalObject(args[1]);
		a = EvalObject(args[0]);
	}
	else
	{
		a = EvalObject(args[0]);
		b = EvalObject(args[1]);
	}
	Context = context;
}

void ExpressionEvaluator::NameOperands(Expression* expr, NameString& a, NameString& b)
{
	Expression* const* args = OperandsOf(expr);
	UObject* context = Context;
	Context = Self;
	if (IsVariable(args[0]->Typed))
	{
		b = EvalName(args[1]);
		a = EvalName(args[0]);
	}
	else
	{
		a = EvalName(args[0]);
		b = EvalName(args[1]);
	}
	Context = context;
}

template<typename T> T& ExpressionEvaluator::Target(Expression* expr)
{
	// Classify only lets a Local or Instance variable of exactly T through
	Expression* variable = OperandsOf(expr)[0];
	UObject* context = Context;
	Context = Self;
	uint8_t* ptr = variable->Typed < TypedKind::InstanceByte ? Local<LocalVar>(variable) : Instance<InstanceVar>(variable);
	Context = context;
	return *reinterpret_cast<T*>(ptr);
}

ExpressionValue ExpressionEvaluator::OperatorValue(Expression* expr)
{
	switch (TypedValueType(expr->Typed))
	{
	case ExpressionValueType::ValueBool: return ExpressionValue::BoolValue(EvalBool(expr));
	case ExpressionValueType::ValueInt: return ExpressionValue::IntValue(EvalNumber<int32_t>(expr));
	default: return ExpressionValue::FloatValue(EvalNumber<float>(expr));
	}
}

bool ExpressionEvaluator::Assign(Expression* lhs, Expression* rhs)
{
	if (lhs->Typed == TypedKind::Unknown)
		Classify(lhs);

	// The right side first, then the store: the variable's place is the same
	// either side of it, as the context is
	switch (lhs->Typed)
	{
	default: return false;
	case TypedKind::LocalByte: { uint8_t v = EvalNumber<uint8_t>(rhs); *Local<LocalVar>(lhs) = v; return true; }
	case TypedKind::LocalInt: { int32_t v = EvalNumber<int32_t>(rhs); *reinterpret_cast<int32_t*>(Local<LocalVar>(lhs)) = v; return true; }
	case TypedKind::LocalFloat: { float v = EvalNumber<float>(rhs); *reinterpret_cast<float*>(Local<LocalVar>(lhs)) = v; return true; }
	case TypedKind::LocalObject: { UObject* v = EvalObject(rhs); *reinterpret_cast<UObject**>(Local<LocalVar>(lhs)) = v; return true; }
	case TypedKind::LocalName: { NameString v = EvalName(rhs); *reinterpret_cast<NameString*>(Local<LocalVar>(lhs)) = v; return true; }
	case TypedKind::LocalBool: { bool v = EvalBool(rhs); SetBool<LocalVar>(Local<LocalVar>(lhs), lhs, v); return true; }
	case TypedKind::InstanceByte: { uint8_t v = EvalNumber<uint8_t>(rhs); *Instance<InstanceVar>(lhs) = v; return true; }
	case TypedKind::InstanceInt: { int32_t v = EvalNumber<int32_t>(rhs); *reinterpret_cast<int32_t*>(Instance<InstanceVar>(lhs)) = v; return true; }
	case TypedKind::InstanceFloat: { float v = EvalNumber<float>(rhs); *reinterpret_cast<float*>(Instance<InstanceVar>(lhs)) = v; return true; }
	case TypedKind::InstanceObject: { UObject* v = EvalObject(rhs); *reinterpret_cast<UObject**>(Instance<InstanceVar>(lhs)) = v; return true; }
	case TypedKind::InstanceName: { NameString v = EvalName(rhs); *reinterpret_cast<NameString*>(Instance<InstanceVar>(lhs)) = v; return true; }
	case TypedKind::InstanceBool: { bool v = EvalBool(rhs); SetBool<InstanceVar>(Instance<InstanceVar>(lhs), lhs, v); return true; }
	case TypedKind::LocalBoolVariable: { bool v = EvalBool(rhs); SetBool<BoolVar>(Local<BoolVar>(lhs), lhs, v); return true; }
	case TypedKind::InstanceBoolVariable: { bool v = EvalBool(rhs); SetBool<BoolVar>(Instance<BoolVar>(lhs), lhs, v); return true; }
	}
}
