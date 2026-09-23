
#include "Precomp.h"
#include "Frame.h"
#include "Bytecode.h"
#include "ExpressionEvaluator.h"
#include "NativeFunc.h"
#include "Packages/Core/UTextBuffer.h"
#include "Packages/Core/UFunction.h"
#include "VM/ScriptCall.h"
#include "Packages/Core/Properties/UByteProperty.h"
#include "Packages/Core/Properties/UIntProperty.h"
#include "Packages/Core/Properties/UFloatProperty.h"
#include "Packages/Core/Properties/UBoolProperty.h"
#include "Packages/Core/Properties/UNameProperty.h"
#include "Packages/Core/Properties/UObjectProperty.h"
#include "Packages/Core/Properties/UPointerProperty.h"
#include "Packages/Core/Properties/UStructProperty.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Utils/AlignedAlloc.h"
#include "Commandlet/VM/DisassemblyCommandlet.h"

std::function<void()> Frame::RunDebugger;
Array<Breakpoint> Frame::Breakpoints;
Array<Frame*> Frame::Callstack;
FrameRunState Frame::RunState = FrameRunState::Running;
Frame* Frame::StepFrame = nullptr;
Expression* Frame::StepExpression = nullptr;
std::string Frame::ExceptionText;
std::unique_ptr<Iterator> Frame::CreatedIterator;

Frame::Frame(UObject* instance, UStruct* func)
{
	Object = instance;
	SetState(func);
}

void Frame::SetState(UStruct* func)
{
	Func = func;
	Variables.Init(func);
}

bool Frame::AddBreakpoint(const NameString& clsName, const NameString& funcName, const NameString& stateName, int statementIndex)
{
	Breakpoint bp;
	bp.Class = clsName;
	bp.Function = funcName;
	bp.State = stateName;

	UClass* cls = engine->packages->FindClass(clsName);
	if (!cls)
		return false;

	if (stateName.IsNone())
	{
		for (UField* child = cls->Children; child; child = child->Next)
		{
			if (child->Name == funcName && UObject::IsType<UFunction>(child))
			{
				UFunction* func = UObject::Cast<UFunction>(child);
				if (statementIndex < 0 || (size_t)statementIndex >= func->Code->Statements.size())
					return false;
				bp.Expr = func->Code->Statements[statementIndex];
				Breakpoints.push_back(bp);
				return true;
			}
		}
	}
	else
	{
		for (UField* child = cls->Children; child; child = child->Next)
		{
			if (child->Name == stateName && UObject::IsType<UState>(child))
			{
				UState* state = UObject::Cast<UState>(child);
				for (UField* stateChild = state->Children; stateChild; stateChild = stateChild->Next)
				{
					if (stateChild->Name == funcName && UObject::IsType<UFunction>(stateChild))
					{
						UFunction* func = UObject::Cast<UFunction>(stateChild);
						if (statementIndex < 0 || (size_t)statementIndex >= func->Code->Statements.size())
							return false;
						bp.Expr = func->Code->Statements[statementIndex];
						Breakpoints.push_back(bp);
						return true;
					}
				}
			}
		}
	}
	return false;
}

void Frame::Break()
{
	RunState = FrameRunState::DebugBreak;

	if (RunDebugger)
	{
		engine->audiodev->BreakpointTriggered();
		RunDebugger();
	}
	else
	{
		if (!ExceptionText.empty())
		{
			std::string callstack = Frame::GetCallstack();
			std::string message = "Script execution error:\r\n\r\n";
			message += ExceptionText;
			message += "\r\n\r\nCall stack:\r\n\r\n" + callstack;
			Exception::Throw(message);
		}
	}
}

void Frame::Resume()
{
	RunState = FrameRunState::Running;
}

void Frame::StepInto()
{
	StepFrame = Callstack.back();
	RunState = FrameRunState::StepInto;
}

void Frame::StepOver()
{
	StepFrame = Callstack.back();
	RunState = FrameRunState::StepOver;
}

void Frame::StepOut()
{
	StepFrame = Callstack.back();
	RunState = FrameRunState::StepOut;
}

void Frame::ThrowException(const std::string& text)
{
#if defined(_DEBUG) && defined(WIN32)
	DebugBreak();
#endif

	ExceptionText = text;
	Break();
}

std::string debugCallstack;
const char* GetCallStack()
{
	debugCallstack = Frame::GetCallstack();
	return debugCallstack.c_str();
}

std::string Frame::GetName()
{
	std::string name;
	if (Func)
	{
		for (UStruct* s = Func; s != nullptr; s = s->StructParent)
		{
			if (name.empty())
				name = s->Name.ToString();
			else
				name = s->Name.ToString() + "." + name;
		}
	}
	return name;
}

std::string Frame::GetDisassembly(Expression* statement)
{
	std::string result;
	PrintPrettyExpression::Print([&](const std::string& text) { result += text; }, statement);
	return result;
}

std::string Frame::GetCallstack()
{
	std::string result;

#ifdef WIN32
	std::string newline = "\r\n";
#else
	std::string newline = "\n";
#endif

	for (auto it = Callstack.rbegin(); it != Callstack.rend(); ++it)
	{
		Frame* frame = *it;
		std::string name = frame->GetName();
		if (UStruct* func = frame->Func)
		{
			name += " line " + std::to_string(func->Line);

			if (frame->StatementIndex > 0) // StatementIndex points at the NEXT statement to be executed
			{
				name += ": ";
				name += GetDisassembly(func->Code->Statements[frame->StatementIndex - 1]);
			}
		}
		if (!result.empty()) result += newline;
		result += "at " + name;
	}
	return result;
}

const Array<UProperty*>& Frame::CallParms(UFunction* func)
{
	if (!func->CallParmsReady)
	{
		// A function's Properties are its UProperty children in order, collected at load
		for (UProperty* prop : func->Properties)
		{
			if (AllFlags(prop->PropFlags, PropertyFlags::Parm))
				func->CallParms.push_back(prop);
			if (!func->ReturnParm && AllFlags(prop->PropFlags, PropertyFlags::Parm | PropertyFlags::ReturnParm))
				func->ReturnParm = prop;
		}
		func->CallParmsReady = true;
	}
	return func->CallParms;
}

size_t CallArguments::Room(UFunction* func, size_t passed)
{
	// Frame::Call adds optional parameters up to the last, then a native's return value
	return std::max(passed, Frame::CallParms(func).size()) + 1;
}

ExpressionValue Frame::Call(UFunction* func, UObject* instance, Array<ExpressionValue> args)
{
	CallArguments callArgs(CallArguments::Room(func, args.size()));
	for (ExpressionValue& arg : args)
		callArgs.push_back(std::move(arg));
	return Call(func, instance, callArgs);
}

ExpressionValue Frame::Call(UFunction* func, UObject* instance, CallArguments& args)
{
	if (!instance)
	{
		LogMessage("Accessed None when calling " + func->Name.ToString());
		LogMessage(Frame::GetCallstack());
		return ExpressionValue::NothingValue();
	}

	TraceCall(func, instance, args);

	// Whether func is an event is a property of its name: look it up once
	if (func->EventIndex == -2)
	{
		EventName eventName = {};
		func->EventIndex = NameStringToEventName(func->Name, eventName) ? (int)eventName : -1;
	}
	bool enabled = func->EventIndex >= 0 ? instance->IsEventEnabled((EventName)func->EventIndex) : instance->IsNonEventEnabled(func->Name);
	if (!enabled)
	{
		return ExpressionValue::NothingValue();
	}

	// Trailing optional args may be missing. Add nothing values so the args list matches the function signature.
	const Array<UProperty*>& parms = CallParms(func);
	for (size_t i = args.size(); i < parms.size() && AllFlags(parms[i]->PropFlags, PropertyFlags::Parm | PropertyFlags::OptionalParm); i++)
		args.push_back(ExpressionValue::NothingValue());

	if (AllFlags(func->FuncFlags, FunctionFlags::Native))
	{
		return CallNative(func, instance, args);
	}
	else
	{
		return CallScript(func, instance, args);
	}
}

ExpressionValue Frame::CallScript(UFunction* func, UObject* instance, CallArguments& args)
{
	Frame frame(instance, func);
	const Array<UProperty*>& parms = func->CallParms;

	// Store args in function frame local variables
	for (size_t i = 0; i < parms.size() && i < args.size(); i++)
	{
		ExpressionValue lvalue = ExpressionValue::Variable(frame.Variables.Data, parms[i]);
		lvalue.Store(args[i]);
	}

	// Run the function
	ExpressionValue result = frame.Run().Value;

	// Load the result from the frame local result variable
	result.Load();

	// Copy out params from frame local variables
	for (size_t i = 0; i < parms.size() && i < args.size(); i++)
	{
		if (AllFlags(parms[i]->PropFlags, PropertyFlags::OutParm))
		{
			ExpressionValue lvalue = ExpressionValue::Variable(frame.Variables.Data, parms[i]);
			args[i].Store(lvalue);
		}
	}

	if (func->ReturnParm && result.GetType() == ExpressionValueType::Nothing)
	{
		result = ExpressionValue::DefaultValue(func->ReturnParm);
	}

	return result;
}

ExpressionValue Frame::CallNative(UFunction* func, UObject* instance, CallArguments& args)
{
	// Native functions expect the last parameter to be the return value
	bool returnparmfound = func->ReturnParm != nullptr;
	if (returnparmfound)
		args.push_back(ExpressionValue::PropertyValue(func->ReturnParm));

	if (func->NativeFuncIndex != 0)
	{
		auto& callback = NativeFunctions::NativeByIndex[func->NativeFuncIndex];
		if (callback)
		{
			Frame frame(instance, func, Frame::NoLocals());
			ActiveCallStackFrame activeFrame(&frame);
			try
			{
				callback(instance, args.data());
			}
			catch (const std::exception& e)
			{
				LogMessage(std::string("Script error: ") + e.what());
				return ExpressionValue::NothingValue();
			}
			catch (...)
			{
				LogMessage("Script error: Unknown error");
				return ExpressionValue::NothingValue();
			}
		}
		else
		{
			Exception::Throw("Unknown native function " + func->NativeStruct->Name.ToString() + "." + func->Name.ToString());
		}
	}
	else
	{
		auto& callback = NativeFunctions::NativeByName[{ func->Name, func->NativeStruct->Name }];
		if (callback)
		{
			Frame frame(instance, func, Frame::NoLocals());
			ActiveCallStackFrame activeFrame(&frame);
			try
			{
				callback(instance, args.data());
			}
			catch (const std::exception& e)
			{
				LogMessage(std::string("Script error: ") + e.what());
				return ExpressionValue::NothingValue();
			}
			catch (...)
			{
				LogMessage("Script error: Unknown error");
				return ExpressionValue::NothingValue();
			}
		}
		else
		{
			Exception::Throw("Unknown native function " + func->NativeStruct->Name.ToString() + "." + func->Name.ToString());
		}
	}

	return returnparmfound ? std::move(args.back()) : ExpressionValue::NothingValue();
}

void Frame::TraceCall(UFunction* func, UObject* instance, const CallArguments& args)
{
#if 0 // To do: create a commandlet that lets us do this
	static NameString TraceActorClass = "CTFGame";
	static NameString TraceActorFunc = "PostBeginPlay";
	if (instance->Class->Name == TraceActorClass && func->Name == TraceActorFunc)
	{
		LogMessage("RemainingBots=" + std::to_string(instance->GetInt("RemainingBots")));
		LogMessage("InitialBots=" + std::to_string(instance->GetInt("InitialBots")));
		std::string traceMessage = "Called " + func->Name.ToString() + "(";
		bool first = true;
		for (auto& arg : args)
		{
			if (first)
				first = false;
			else
				traceMessage += ", ";
			if (arg.GetType() == ExpressionValueType::ValueString)
			{
				traceMessage += "\"";
				traceMessage += arg.ToString();
				traceMessage += "\"";
			}
			else if (arg.GetType() == ExpressionValueType::ValueName)
			{
				traceMessage += "'";
				traceMessage += arg.ToName().ToString();
				traceMessage += "'";
			}
			else if (arg.GetType() == ExpressionValueType::ValueBool)
			{
				traceMessage += "'";
				traceMessage += arg.ToBool() ? "true" : "false";
				traceMessage += "'";
			}
			else if (arg.GetType() == ExpressionValueType::ValueObject)
			{
				traceMessage += arg.ToObject() ? UObject::GetUClassName(arg.ToObject()).ToString() : "null";
			}
			else if (arg.GetType() == ExpressionValueType::Nothing)
			{
				traceMessage += "None";
			}
			else
			{
				traceMessage += "?";
			}
		}
		traceMessage += ")";
		LogMessage(traceMessage);
	}
#endif
}

void Frame::GotoLabel(const NameString& label)
{
	for (UClass* cls = Object->Class; cls != nullptr; cls = static_cast<UClass*>(cls->BaseStruct))
	{
		UState* state = cls->GetState(Func->Name);
		if (state)
		{
			int labelIndex = state->Code->FindLabelIndex(label.IsNone() ? NameString("Begin") : label);
			if (labelIndex != -1)
			{
				Func = state;
				StatementIndex = labelIndex;
				LatentState = LatentRunState::Continue;
				return;
			}
		}
	}
	LatentState = LatentRunState::Stop;
}

void Frame::Tick()
{
	if (LatentState == LatentRunState::Continue)
		Run();
}

ExpressionEvalResult Frame::Run()
{
	if (!Func)
		return {};

	ActiveCallStackFrame activeFrame(this);

	if (!Func->Code->Statements.empty())
		StepExpression = Func->Code->Statements[StatementIndex];

	if (RunState == FrameRunState::StepInto)
	{
		// We entered a new function. Break for step into.
		Break();
	}

	const int maxInstructions = 500'000;
	int instructionsRetired = 0;
	while (true)
	{
		if (StatementIndex >= Func->Code->Statements.size())
			ThrowException("Unexpected end of code statements");

		// Note: GotoState may change StatementIndex (jump to a different location) so we have to increment the index before executing the statement
		size_t curStatementIndex = StatementIndex;
		StatementIndex++;

		StepExpression = Func->Code->Statements[curStatementIndex];

		if (instructionsRetired >= maxInstructions)
		{
			LogMessage("Too many VM instructions executed in a single tick");
			Break();
		}
		else if ((RunState == FrameRunState::StepOver || RunState == FrameRunState::StepInto) && StepFrame == this)
		{
			// We are running a new expression. Break on step over, but also step into as there might not been a function to step into.
			Break();
		}
		else if (RunState == FrameRunState::StepOut && StepFrame == nullptr)
		{
			// We found the function exit point. Break the debugger.
			Break();
		}

		Expression* statement = Func->Code->Statements[curStatementIndex];
		ExpressionEvalResult result = ExpressionEvaluator::Eval(statement, Object, Object, Variables.Data);
		if (!Func)
			return result;
		switch (result.Result)
		{
		case StatementResult::Next:
			break;
		case StatementResult::Jump:
			StatementIndex = Func->Code->FindStatementIndex(result.JumpAddress);
			break;
		case StatementResult::Switch:
			ProcessSwitch(result.Value);
			break;
		case StatementResult::GotoLabel:
			{
				int index = Func->Code->FindLabelIndex(result.Label);
				if (index != -1)
				{
					StatementIndex = index;
				}
				else
				{
					// State gotos can jump to a parent state block!
					bool found = false;
					for (UClass* cls = Object->Class; cls != nullptr; cls = static_cast<UClass*>(cls->BaseStruct))
					{
						UState* state = cls->GetState(Func->Name);
						if (state)
						{
							int labelIndex = state->Code->FindLabelIndex(result.Label);
							if (labelIndex != -1)
							{
								Func = state;
								StatementIndex = labelIndex;
								found = true;
								break;
							}
						}
					}
					if (!found)
						ThrowException("Could not find label: " + result.Label.ToString());
				}
			}
			break;
		case StatementResult::Stop:
			LatentState = LatentRunState::Stop;
			return result;
		case StatementResult::Return:
			// Package 61 and earlier transfered the return value in an out parameter
			if (!static_cast<ReturnExpression*>(statement)->Value)
			{
				for (UField* field = Func->Children; field != nullptr; field = field->Next)
				{
					UProperty* prop = UObject::TryCast<UProperty>(field);
					if (prop && AllFlags(prop->PropFlags, PropertyFlags::Parm | PropertyFlags::ReturnParm))
					{
						result.Value = ExpressionValue::Variable(Variables.Data, prop);
						result.Value.Load();
						break;
					}
				}
			}

			if (RunState == FrameRunState::StepOut && StepFrame == this)
			{
				// We are exiting the function. Break on next instruction by requesting a break on next instruction.
				StepFrame = nullptr;
			}
			return result;
		case StatementResult::Iterator:
			if (!result.Iter)
				ThrowException("Iterator statement without an iterator in " + Object->Name.ToString() + "." + Func->Name.ToString());
			Iterators.push_back(std::move(result.Iter));
			Iterators.back()->StartStatementIndex = curStatementIndex + 1;
			Iterators.back()->EndStatementIndex = Func->Code->FindStatementIndex(result.JumpAddress);
			if (Iterators.back()->Next())
				StatementIndex = Iterators.back()->StartStatementIndex;
			else
				StatementIndex = Iterators.back()->EndStatementIndex;
			break;
		case StatementResult::IteratorNext:
			if (Iterators.empty())
				ThrowException("Iterator next statement without an iterator in " + Object->Name.ToString() + "." + Func->Name.ToString());
			if (Iterators.back()->Next())
				StatementIndex = Iterators.back()->StartStatementIndex;
			else
				StatementIndex = Iterators.back()->EndStatementIndex;
			break;
		case StatementResult::IteratorPop:
			if (Iterators.empty())
				ThrowException("Iterator pop statement without an iterator in " + Object->Name.ToString() + "." + Func->Name.ToString());
			Iterators.pop_back();
			break;
		case StatementResult::AccessedNone:
			LogMessage("Accessed None");
			LogMessage(Frame::GetCallstack());
			break;
		}

		if (Object->StateFrame.get() == this && LatentState != LatentRunState::Continue)
		{
			return result;
		}

		instructionsRetired++;
	}
}

void Frame::ProcessSwitch(const ExpressionValue& condition)
{
	SwitchExpression* switchexpr = static_cast<SwitchExpression*>(Func->Code->Statements[StatementIndex - 1]);
	while (true)
	{
		CaseExpression* caseexpr = static_cast<CaseExpression*>(Func->Code->Statements[StatementIndex++]);
		if (caseexpr->Value)
		{
			ExpressionValue casevalue = ExpressionEvaluator::Eval(caseexpr->Value, Object, Object, Variables.Data).Value;
			if (condition.IsEqual(casevalue))
				break;
			else
				StatementIndex = Func->Code->FindStatementIndex(caseexpr->NextOffset);
		}
		else
		{
			break;
		}
	}
}

/////////////////////////////////////////////////////////////////////////////

// Whether every property of s constructs to zero bytes and has nothing to
// destruct: numbers, bools, names, object references, and structs of those.
static bool IsPlainData(UStruct* s)
{
	for (UProperty* prop : s->Properties)
	{
		if (UStructProperty* structProp = UObject::TryCast<UStructProperty>(prop))
		{
			if (!structProp->Struct || !IsPlainData(structProp->Struct))
				return false;
		}
		else if (!UObject::TryCast<UByteProperty>(prop) && !UObject::TryCast<UIntProperty>(prop) &&
			!UObject::TryCast<UFloatProperty>(prop) && !UObject::TryCast<UBoolProperty>(prop) &&
			!UObject::TryCast<UNameProperty>(prop) && !UObject::TryCast<UObjectProperty>(prop) &&
			!UObject::TryCast<UPointerProperty>(prop))
		{
			return false;
		}
	}
	return true;
}

void LocalVariables::Init(UStruct* func)
{
	Reset();
	Func = func;
	if (func)
	{
		if (func->StructSize <= sizeof(Inline) && func->StructAlignment <= 16)
			Data = Inline;
		else
			Data = AlignedAlloc(func->StructAlignment, func->StructSize);

		// Most functions' locals are plain data, and constructing them one
		// virtual call at a time was a measurable part of every script call
		if (func->PlainDataLocals < 0)
			func->PlainDataLocals = IsPlainData(func) ? 1 : 0;

		if (func->PlainDataLocals)
		{
			memset(Data, 0, func->StructSize);
		}
		else
		{
			for (UProperty* prop : func->Properties)
			{
				prop->ConstructArray(static_cast<uint8_t*>(Data) + prop->DataOffset.DataOffset);
			}
		}
	}
}

void LocalVariables::Reset()
{
	if (Func && Data && !Func->PlainDataLocals)
	{
		for (UProperty* prop : Func->Properties)
		{
			prop->DestructArray(static_cast<uint8_t*>(Data) + prop->DataOffset.DataOffset);
		}
	}

	if (Data != Inline)
		AlignedFree(Data);
	Func = nullptr;
	Data = nullptr;
}
