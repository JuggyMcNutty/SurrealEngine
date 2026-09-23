#pragma once

#include "ExpressionValue.h"
#include "Iterator.h"

class DebuggerWindow;
class Bytecode;
class UObject;
class UFunction;
class Expression;
struct ExpressionEvalResult;

enum class FrameRunState
{
	Running,
	DebugBreak,
	StepInto,
	StepOver,
	StepOut
};

enum class LatentRunState
{
	Continue,
	Stop,
	Sleep,
	FinishAnim,
	FinishInterpolation,
	MoveTo,
	MoveToward,
	StrafeTo,
	StrafeFacing,
	TurnTo,
	TurnToward,
	WaitForLanding
};

struct Breakpoint
{
	NameString Class;
	NameString Function;
	NameString State;
	Expression* Expr = nullptr;
	UProperty* Property = nullptr; // For watchpoints. Not implemented yet.
	bool Enabled = true;
};

class LocalVariables
{
public:
	LocalVariables() = default;
	LocalVariables(const LocalVariables&) = delete;
	LocalVariables& operator=(const LocalVariables&) = delete;
	~LocalVariables() { Reset(); }

	// Constructs the locals of func, after destructing any there were
	void Init(UStruct* func);
	void Reset();

	UStruct* Func = nullptr;
	void* Data = nullptr;

private:
	// Most functions' locals fit here, so a script call allocates nothing for them
	alignas(16) uint8_t Inline[256];
};

// A call's arguments: the caller's, then what Frame::Call adds -- Nothing for
// trailing optional parameters left out, a native's return value. Up to
// InlineCount live on the stack, which covers nearly every call; Room says
// how many a call to a function can need.
class CallArguments
{
public:
	explicit CallArguments(size_t capacity)
	{
		if (capacity > InlineCount)
		{
			Values = static_cast<ExpressionValue*>(::operator new(capacity * sizeof(ExpressionValue)));
			Capacity = capacity;
		}
	}

	~CallArguments()
	{
		for (size_t i = 0; i < Count; i++)
			Values[i].~ExpressionValue();
		if (Values != reinterpret_cast<ExpressionValue*>(Inline))
			::operator delete(Values);
	}

	CallArguments(const CallArguments&) = delete;
	CallArguments& operator=(const CallArguments&) = delete;

	static size_t Room(UFunction* func, size_t passed);

	void push_back(ExpressionValue&& value)
	{
		if (Count == Capacity)
			Grow();
		new (Values + Count) ExpressionValue(std::move(value));
		Count++;
	}

	ExpressionValue* data() { return Values; }
	size_t size() const { return Count; }
	ExpressionValue& operator[](size_t index) { return Values[index]; }
	const ExpressionValue& operator[](size_t index) const { return Values[index]; }
	ExpressionValue& back() { return Values[Count - 1]; }
	const ExpressionValue* begin() const { return Values; }
	const ExpressionValue* end() const { return Values + Count; }

private:
	void Grow()
	{
		size_t capacity = Capacity * 2 + 1;
		ExpressionValue* values = static_cast<ExpressionValue*>(::operator new(capacity * sizeof(ExpressionValue)));
		for (size_t i = 0; i < Count; i++)
		{
			new (values + i) ExpressionValue(std::move(Values[i]));
			Values[i].~ExpressionValue();
		}
		if (Values != reinterpret_cast<ExpressionValue*>(Inline))
			::operator delete(Values);
		Values = values;
		Capacity = capacity;
	}

	static const size_t InlineCount = 8;
	alignas(ExpressionValue) uint8_t Inline[InlineCount * sizeof(ExpressionValue)];
	ExpressionValue* Values = reinterpret_cast<ExpressionValue*>(Inline);
	size_t Count = 0;
	size_t Capacity = InlineCount;
};

class Frame
{
public:
	static ExpressionValue Call(UFunction* func, UObject* instance, Array<ExpressionValue> args);
	static ExpressionValue Call(UFunction* func, UObject* instance, CallArguments& args);
	static const Array<UProperty*>& CallParms(UFunction* func);
	static std::string GetCallstack();
	static std::string GetDisassembly(Expression* statement);

	static bool AddBreakpoint(const NameString& cls, const NameString& func, const NameString& state = {}, int statementIndex = 0);

	static std::function<void()> RunDebugger;
	static Array<Breakpoint> Breakpoints;
	static Array<Frame*> Callstack;
	static FrameRunState RunState;
	static Frame* StepFrame;
	static Expression* StepExpression;
	static std::string ExceptionText;

	static void Break();
	static void Resume();
	static void StepInto();
	static void StepOver();
	static void StepOut();
	static void ThrowException(const std::string& text);

	static std::unique_ptr<Iterator> CreatedIterator;

	Frame(UObject* instance, UStruct* func);

	// A native function's place on the call stack. Natives take their
	// arguments directly and never Run, so there are no locals to allocate.
	struct NoLocals {};
	Frame(UObject* instance, UStruct* func, NoLocals) : Object(instance), Func(func) {}

	void SetState(UStruct* func);

	void GotoLabel(const NameString& label);
	void Tick();

	std::string GetName();

	LatentRunState LatentState = LatentRunState::Continue;

	LocalVariables Variables;
	UObject* Object = nullptr;
	UStruct* Func = nullptr;
	size_t StatementIndex = 0;
	Array<std::unique_ptr<Iterator>> Iterators;

private:
	ExpressionEvalResult Run();
	void ProcessSwitch(const ExpressionValue& condition);
	static void ClassifyStatement(Expression* statement, const Bytecode& code);

	static ExpressionValue CallNative(UFunction* func, UObject* instance, CallArguments& args);
	static ExpressionValue CallScript(UFunction* func, UObject* instance, CallArguments& args);
	static void TraceCall(UFunction* func, UObject* instance, const CallArguments& args);

	struct ActiveCallStackFrame
	{
		ActiveCallStackFrame(Frame* frame) { Frame::Callstack.push_back(frame); }
		~ActiveCallStackFrame() { Frame::Callstack.pop_back(); }
	};
};
