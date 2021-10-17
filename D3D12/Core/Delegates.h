/*
DOCUMENTATION

// CONFIG

Override default asset
#define DELEGATE_ASSERT(expression, ...)

Override default static_assert
#define DELEGATE_STATIC_ASSERT(expression, msg)

Set inline allocator size (default: 32)
#define DELEGATE_INLINE_ALLOCATION_SIZE

Reassign allocation functions:
Delegates::SetAllocationCallbacks(allocFunction, freeFunc);


// USAGE

## Classes ##
- ```Delegate<RetVal, Args>```
- ```MulticastDelegate<Args>```

## Features ##
- Support for:
	- Static/Global methods
	- Member functions
	- Lambda's
	- std::shared_ptr
- Delegate object is allocated inline if it is under 32 bytes
- Add payload to delegate during bind-time
- Move operations enable optimization

## Example Usage ##

### Delegate ###

Delegate<int, float> del;
del.BindLambda([](float a, int payload)
{
	std::cout << "Lambda delegate parameter: " << a << std::endl;
	std::cout << "Lambda delegate payload: " << payload << std::endl;
	return 10;
}, 50);
std::cout << "Lambda delegate return value: " << del.Execute(20) << std::endl;

Output:
Lambda delegate parameter: 20
Lambda delegate payload: 50
Lambda delegate return value: 10

### MulticastDelegate ###

struct Foo
{
	void Bar(float a, int payload)
	{
		std::cout << "Raw delegate parameter: " << a << std::endl;
		std::cout << "Raw delegate payload: " << payload << std::endl;
	}
};
MulticastDelegate<float> del;
del.AddLambda([](float a, int payload)
{
	std::cout << "Lambda delegate parameter: " << a << std::endl;
	std::cout << "Lambda delegate payload: " << payload << std::endl;
}, 90);

Foo foo;
del.AddRaw(&foo, &Foo::Bar, 10);
del.Broadcast(20);

Output:
Lambda delegate parameter: 20
Lambda delegate payload: 90
Raw delegate parameter: 20
Raw delegate payload: 10

*/

#ifndef CPP_DELEGATES
#define CPP_DELEGATES

#include <vector>
#include <memory>
#include <tuple>

///////////////////////////////////////////////////////////////
//////////////////// DEFINES SECTION //////////////////////////
///////////////////////////////////////////////////////////////

#ifndef DELEGATE_ASSERT
#include <assert.h>
#define DELEGATE_ASSERT(expression, ...) assert(expression)
#endif

#ifndef DELEGATE_STATIC_ASSERT
#if __cplusplus <= 199711L
#define DELEGATE_STATIC_ASSERT(expression, msg) static_assert(expression, msg)
#else
#define DELEGATE_STATIC_ASSERT(expression, msg)
#endif
#endif

//The allocation size of delegate data.
//Delegates larger than this will be heap allocated.
#ifndef DELEGATE_INLINE_ALLOCATION_SIZE
#define DELEGATE_INLINE_ALLOCATION_SIZE 32
#endif

#define DECLARE_DELEGATE(name, ...) \
using name = Delegate<void, __VA_ARGS__>

#define DECLARE_DELEGATE_RET(name, retValue, ...) \
using name = Delegate<retValue, __VA_ARGS__>

#define DECLARE_MULTICAST_DELEGATE(name, ...) \
using name = MulticastDelegate<__VA_ARGS__>; \
using name ## Delegate = MulticastDelegate<__VA_ARGS__>::DelegateT

#define DECLARE_EVENT(name, ownerType, ...) \
class name : public MulticastDelegate<__VA_ARGS__> \
{ \
private: \
	friend class ownerType; \
	using MulticastDelegate::Broadcast; \
	using MulticastDelegate::RemoveAll; \
	using MulticastDelegate::Remove; \
};

///////////////////////////////////////////////////////////////
/////////////////// INTERNAL SECTION //////////////////////////
///////////////////////////////////////////////////////////////

#if __cplusplus >= 201703L
#define NO_DISCARD [[nodiscard]]
#else
#define NO_DISCARD		
#endif

namespace _DelegatesInteral
{
	template<bool IsConst, typename Object, typename RetVal, typename ...Args>
	struct MemberFunction;

	template<typename Object, typename RetVal, typename ...Args>
	struct MemberFunction<true, Object, RetVal, Args...>
	{
		using Type = RetVal(Object::*)(Args...) const;
	};

	template<typename Object, typename RetVal, typename ...Args>
	struct MemberFunction<false, Object, RetVal, Args...>
	{
		using Type = RetVal(Object::*)(Args...);
	};

	static void* (*Alloc)(size_t size) = [](size_t size) { return malloc(size); };
	static void(*Free)(void* pPtr) = [](void* pPtr) { free(pPtr); };
	template<typename T>
	void DelegateDeleteFunc(T* pPtr)
	{
		pPtr->~T();
		DelegateFreeFunc(pPtr);
	}
}

namespace Delegates
{
	using AllocateCallback = void* (*)(size_t size);
	using FreeCallback = void(*)(void* pPtr);
	inline void SetAllocationCallbacks(AllocateCallback allocateCallback, FreeCallback freeCallback)
	{
		_DelegatesInteral::Alloc = allocateCallback;
		_DelegatesInteral::Free = freeCallback;
	}
}

class IDelegateBase
{
public:
	IDelegateBase() = default;
	virtual ~IDelegateBase() noexcept = default;
	virtual const void* GetOwner() const { return nullptr; }
	virtual void Clone(void* pDestination) = 0;
};

//Base type for delegates
template<typename RetVal, typename... Args>
class IDelegate : public IDelegateBase
{
public:
	virtual RetVal Execute(Args&&... args) = 0;
};

template<typename RetVal, typename... Args2>
class StaticDelegate;

template<typename RetVal, typename... Args, typename... Args2>
class StaticDelegate<RetVal(Args...), Args2...> : public IDelegate<RetVal, Args...>
{
public:
	using DelegateFunction = RetVal(*)(Args..., Args2...);

	StaticDelegate(DelegateFunction function, Args2&&... payload)
		: m_Function(function), m_Payload(std::forward<Args2>(payload)...)
	{}

	StaticDelegate(DelegateFunction function, const std::tuple<Args2...>& payload)
		: m_Function(function), m_Payload(payload)
	{}

	virtual RetVal Execute(Args&&... args) override
	{
		return Execute_Internal(std::forward<Args>(args)..., std::index_sequence_for<Args2...>());
	}

	virtual void Clone(void* pDestination) override
	{
		new (pDestination) StaticDelegate(m_Function, m_Payload);
	}

private:
	template<std::size_t... Is>
	RetVal Execute_Internal(Args&&... args, std::index_sequence<Is...>)
	{
		return m_Function(std::forward<Args>(args)..., std::get<Is>(m_Payload)...);
	}

	DelegateFunction m_Function;
	std::tuple<Args2...> m_Payload;
};

template<bool IsConst, typename T, typename RetVal, typename... Args2>
class RawDelegate;

template<bool IsConst, typename T, typename RetVal, typename... Args, typename... Args2>
class RawDelegate<IsConst, T, RetVal(Args...), Args2...> : public IDelegate<RetVal, Args...>
{
public:
	using DelegateFunction = typename _DelegatesInteral::MemberFunction<IsConst, T, RetVal, Args..., Args2...>::Type;

	RawDelegate(T* pObject, DelegateFunction function, Args2&&... payload)
		: m_pObject(pObject), m_Function(function), m_Payload(std::forward<Args2>(payload)...)
	{}

	RawDelegate(T* pObject, DelegateFunction function, const std::tuple<Args2...>& payload)
		: m_pObject(pObject), m_Function(function), m_Payload(payload)
	{}

	virtual RetVal Execute(Args&&... args) override
	{
		return Execute_Internal(std::forward<Args>(args)..., std::index_sequence_for<Args2...>());
	}
	virtual const void* GetOwner() const override
	{
		return m_pObject;
	}

	virtual void Clone(void* pDestination) override
	{
		new (pDestination) RawDelegate(m_pObject, m_Function, m_Payload);
	}

private:
	template<std::size_t... Is>
	RetVal Execute_Internal(Args&&... args, std::index_sequence<Is...>)
	{
		return (m_pObject->*m_Function)(std::forward<Args>(args)..., std::get<Is>(m_Payload)...);
	}

	T* m_pObject;
	DelegateFunction m_Function;
	std::tuple<Args2...> m_Payload;
};

template<typename TLambda, typename RetVal, typename... Args>
class LambdaDelegate;

template<typename TLambda, typename RetVal, typename... Args, typename... Args2>
class LambdaDelegate<TLambda, RetVal(Args...), Args2...> : public IDelegate<RetVal, Args...>
{
public:
	explicit LambdaDelegate(TLambda&& lambda, Args2&&... payload)
		: m_Lambda(std::forward<TLambda>(lambda)),
		m_Payload(std::forward<Args2>(payload)...)
	{}

	explicit LambdaDelegate(const TLambda& lambda, const std::tuple<Args2...>& payload)
		: m_Lambda(lambda),
		m_Payload(payload)
	{}

	RetVal Execute(Args&&... args) override
	{
		return Execute_Internal(std::forward<Args>(args)..., std::index_sequence_for<Args2...>());
	}

	virtual void Clone(void* pDestination) override
	{
		new (pDestination) LambdaDelegate(m_Lambda, m_Payload);
	}

private:
	template<std::size_t... Is>
	RetVal Execute_Internal(Args&&... args, std::index_sequence<Is...>)
	{
		return (RetVal)((m_Lambda)(std::forward<Args>(args)..., std::get<Is>(m_Payload)...));
	}

	TLambda m_Lambda;
	std::tuple<Args2...> m_Payload;
};

template<bool IsConst, typename T, typename RetVal, typename... Args>
class SPDelegate;

template<bool IsConst, typename RetVal, typename T, typename... Args, typename... Args2>
class SPDelegate<IsConst, T, RetVal(Args...), Args2...> : public IDelegate<RetVal, Args...>
{
public:
	using DelegateFunction = typename _DelegatesInteral::MemberFunction<IsConst, T, RetVal, Args..., Args2...>::Type;

	SPDelegate(std::shared_ptr<T> pObject, DelegateFunction pFunction, Args2&&... payload)
		: m_pObject(pObject),
		m_pFunction(pFunction),
		m_Payload(std::forward<Args2>(payload)...)
	{}

	SPDelegate(std::weak_ptr<T> pObject, DelegateFunction pFunction, const std::tuple<Args2...>& payload)
		: m_pObject(pObject),
		m_pFunction(pFunction),
		m_Payload(payload)
	{}

	virtual RetVal Execute(Args&&... args) override
	{
		return Execute_Internal(std::forward<Args>(args)..., std::index_sequence_for<Args2...>());
	}

	virtual const void* GetOwner() const override
	{
		return m_pObject.expired() ? nullptr : m_pObject.lock().get();
	}

	virtual void Clone(void* pDestination) override
	{
		new (pDestination) SPDelegate(m_pObject, m_pFunction, m_Payload);
	}

private:
	template<std::size_t... Is>
	RetVal Execute_Internal(Args&&... args, std::index_sequence<Is...>)
	{
		if (m_pObject.expired())
		{
			return RetVal();
		}
		else
		{
			std::shared_ptr<T> pPinned = m_pObject.lock();
			return (pPinned.get()->*m_pFunction)(std::forward<Args>(args)..., std::get<Is>(m_Payload)...);
		}
	}

	std::weak_ptr<T> m_pObject;
	DelegateFunction m_pFunction;
	std::tuple<Args2...> m_Payload;
};

//A handle to a delegate used for a multicast delegate
//Static ID so that every handle is unique
class DelegateHandle
{
public:
	constexpr DelegateHandle() noexcept
		: m_Id(INVALID_ID)
	{
	}

	explicit DelegateHandle(bool /*generateId*/) noexcept
		: m_Id(GetNewID())
	{
	}

	~DelegateHandle() noexcept = default;
	DelegateHandle(const DelegateHandle& other) = default;
	DelegateHandle& operator=(const DelegateHandle& other) = default;

	DelegateHandle(DelegateHandle&& other) noexcept
		: m_Id(other.m_Id)
	{
		other.Reset();
	}

	DelegateHandle& operator=(DelegateHandle&& other) noexcept
	{
		m_Id = other.m_Id;
		other.Reset();
		return *this;
	}

	operator bool() const noexcept
	{
		return IsValid();
	}

	bool operator==(const DelegateHandle& other) const noexcept
	{
		return m_Id == other.m_Id;
	}

	bool operator<(const DelegateHandle& other) const noexcept
	{
		return m_Id < other.m_Id;
	}

	bool IsValid() const noexcept
	{
		return m_Id != INVALID_ID;
	}

	void Reset() noexcept
	{
		m_Id = INVALID_ID;
	}

	constexpr static const unsigned int INVALID_ID = (unsigned int)~0;
private:
	unsigned int m_Id;
	static unsigned int CURRENT_ID;

	static int GetNewID()
	{
		unsigned int output = DelegateHandle::CURRENT_ID++;
		if (DelegateHandle::CURRENT_ID == INVALID_ID)
		{
			DelegateHandle::CURRENT_ID = 0;
		}
		return output;
	}
};

template<size_t MaxStackSize>
class InlineAllocator
{
public:
	//Constructor
	constexpr InlineAllocator() noexcept
		: m_Size(0)
	{
		DELEGATE_STATIC_ASSERT(MaxStackSize > sizeof(void*), "MaxStackSize is smaller or equal to the size of a pointer. This will make the use of an InlineAllocator pointless. Please increase the MaxStackSize.");
	}

	//Destructor
	~InlineAllocator() noexcept
	{
		Free();
	}

	//Copy constructor
	InlineAllocator(const InlineAllocator& other)
		: m_Size(0)
	{
		if (other.HasAllocation())
		{
			memcpy(Allocate(other.m_Size), other.GetAllocation(), other.m_Size);
		}
		m_Size = other.m_Size;
	}

	//Copy assignment operator
	InlineAllocator& operator=(const InlineAllocator& other)
	{
		if (other.HasAllocation())
		{
			memcpy(Allocate(other.m_Size), other.GetAllocation(), other.m_Size);
		}
		m_Size = other.m_Size;
		return *this;
	}

	//Move constructor
	InlineAllocator(InlineAllocator&& other) noexcept
		: m_Size(other.m_Size)
	{
		other.m_Size = 0;
		if (m_Size > MaxStackSize)
		{
			std::swap(pPtr, other.pPtr);
		}
		else
		{
			memcpy(Buffer, other.Buffer, m_Size);
		}
	}

	//Move assignment operator
	InlineAllocator& operator=(InlineAllocator&& other) noexcept
	{
		Free();
		m_Size = other.m_Size;
		other.m_Size = 0;
		if (m_Size > MaxStackSize)
		{
			std::swap(pPtr, other.pPtr);
		}
		else
		{
			memcpy(Buffer, other.Buffer, m_Size);
		}
		return *this;
	}

	//Allocate memory of given size
	//If the size is over the predefined threshold, it will be allocated on the heap
	void* Allocate(const size_t size)
	{
		if (m_Size != size)
		{
			Free();
			m_Size = size;
			if (size > MaxStackSize)
			{
				pPtr = _DelegatesInteral::Alloc(size);
				return pPtr;
			}
		}
		return (void*)Buffer;
	}

	//Free the allocated memory
	void Free()
	{
		if (m_Size > MaxStackSize)
		{
			_DelegatesInteral::Free(pPtr);
		}
		m_Size = 0;
	}

	//Return the allocated memory either on the stack or on the heap
	void* GetAllocation() const
	{
		if (HasAllocation())
		{
			return HasHeapAllocation() ? pPtr : (void*)Buffer;
		}
		else
		{
			return nullptr;
		}
	}

	size_t GetSize() const
	{
		return m_Size;
	}

	bool HasAllocation() const
	{
		return m_Size > 0;
	}

	bool HasHeapAllocation() const
	{
		return m_Size > MaxStackSize;
	}

private:
	//If the allocation is smaller than the threshold, Buffer is used
	//Otherwise pPtr is used together with a separate dynamic allocation
	union
	{
		char Buffer[MaxStackSize];
		void* pPtr;
	};
	size_t m_Size;
};

class DelegateBase
{
public:
	//Default constructor
	constexpr DelegateBase() noexcept
		: m_Allocator()
	{}

	//Default destructor
	virtual ~DelegateBase() noexcept
	{
		Release();
	}

	//Copy contructor
	DelegateBase(const DelegateBase& other)
	{
		if (other.m_Allocator.HasAllocation())
		{
			m_Allocator.Allocate(other.m_Allocator.GetSize());
			other.GetDelegate()->Clone(m_Allocator.GetAllocation());
		}
	}

	//Copy assignment operator
	DelegateBase& operator=(const DelegateBase& other)
	{
		Release();
		if (other.m_Allocator.HasAllocation())
		{
			m_Allocator.Allocate(other.m_Allocator.GetSize());
			other.GetDelegate()->Clone(m_Allocator.GetAllocation());
		}
		return *this;
	}

	//Move constructor
	DelegateBase(DelegateBase&& other) noexcept
		: m_Allocator(std::move(other.m_Allocator))
	{}

	//Move assignment operator
	DelegateBase& operator=(DelegateBase&& other) noexcept
	{
		Release();
		m_Allocator = std::move(other.m_Allocator);
		return *this;
	}

	//Gets the owner of the deletage
	//Only valid for SPDelegate and RawDelegate.
	//Otherwise returns nullptr by default
	const void* GetOwner() const
	{
		if (m_Allocator.HasAllocation())
		{
			return GetDelegate()->GetOwner();
		}
		return nullptr;
	}

	size_t GetSize() const
	{
		return m_Allocator.GetSize();
	}

	//Clear the bound delegate if it is bound to the given object.
	//Ignored when pObject is a nullptr
	void ClearIfBoundTo(void* pObject)
	{
		if (pObject != nullptr && IsBoundTo(pObject))
		{
			Release();
		}
	}

	//Clear the bound delegate if it exists
	void Clear()
	{
		Release();
	}

	//If the allocator has a size, it means it's bound to something
	bool IsBound() const
	{
		return m_Allocator.HasAllocation();
	}

	bool IsBoundTo(void* pObject) const
	{
		if (pObject == nullptr || m_Allocator.HasAllocation() == false)
		{
			return false;
		}
		return GetDelegate()->GetOwner() == pObject;
	}

protected:
	void Release()
	{
		if (m_Allocator.HasAllocation())
		{
			GetDelegate()->~IDelegateBase();
			m_Allocator.Free();
		}
	}

	IDelegateBase* GetDelegate() const
	{
		return static_cast<IDelegateBase*>(m_Allocator.GetAllocation());
	}

	//Allocator for the delegate itself.
	//Delegate gets allocated when its is smaller or equal than 64 bytes in size.
	//Can be changed by preference
	InlineAllocator<DELEGATE_INLINE_ALLOCATION_SIZE> m_Allocator;
};

//Delegate that can be bound to by just ONE object
template<typename RetVal, typename... Args>
class Delegate : public DelegateBase
{
private:
	template<typename T, typename... Args2>
	using ConstMemberFunction = typename _DelegatesInteral::MemberFunction<true, T, RetVal, Args..., Args2...>::Type;
	template<typename T, typename... Args2>
	using NonConstMemberFunction = typename _DelegatesInteral::MemberFunction<false, T, RetVal, Args..., Args2...>::Type;

public:
	using IDelegateT = IDelegate<RetVal, Args...>;

	//Create delegate using member function
	template<typename T, typename... Args2>
	NO_DISCARD static Delegate CreateRaw(T* pObj, NonConstMemberFunction<T, Args2...> pFunction, Args2... args)
	{
		Delegate handler;
		handler.Bind<RawDelegate<false, T, RetVal(Args...), Args2...>>(pObj, pFunction, std::forward<Args2>(args)...);
		return handler;
	}

	template<typename T, typename... Args2>
	NO_DISCARD static Delegate CreateRaw(T* pObj, ConstMemberFunction<T, Args2...> pFunction, Args2... args)
	{
		Delegate handler;
		handler.Bind<RawDelegate<true, T, RetVal(Args...), Args2...>>(pObj, pFunction, std::forward<Args2>(args)...);
		return handler;
	}

	//Create delegate using global/static function
	template<typename... Args2>
	NO_DISCARD static Delegate CreateStatic(RetVal(*pFunction)(Args..., Args2...), Args2... args)
	{
		Delegate handler;
		handler.Bind<StaticDelegate<RetVal(Args...), Args2...>>(pFunction, std::forward<Args2>(args)...);
		return handler;
	}

	//Create delegate using std::shared_ptr
	template<typename T, typename... Args2>
	NO_DISCARD static Delegate CreateSP(const std::shared_ptr<T>& pObject, NonConstMemberFunction<T, Args2...> pFunction, Args2... args)
	{
		Delegate handler;
		handler.Bind<SPDelegate<false, T, RetVal(Args...), Args2...>>(pObject, pFunction, std::forward<Args2>(args)...);
		return handler;
	}

	template<typename T, typename... Args2>
	NO_DISCARD static Delegate CreateSP(const std::shared_ptr<T>& pObject, ConstMemberFunction<T, Args2...> pFunction, Args2... args)
	{
		Delegate handler;
		handler.Bind<SPDelegate<true, T, RetVal(Args...), Args2...>>(pObject, pFunction, std::forward<Args2>(args)...);
		return handler;
	}

	//Create delegate using a lambda
	template<typename TLambda, typename... Args2>
	NO_DISCARD static Delegate CreateLambda(TLambda&& lambda, Args2... args)
	{
		Delegate handler;
		handler.Bind<LambdaDelegate<TLambda, RetVal(Args...), Args2...>>(std::forward<TLambda>(lambda), std::forward<Args2>(args)...);
		return handler;
	}

	//Bind a member function
	template<typename T, typename... Args2>
	void BindRaw(T* pObject, NonConstMemberFunction<T, Args2...> pFunction, Args2&&... args)
	{
		DELEGATE_STATIC_ASSERT(!std::is_const<T>::value, "Cannot bind a non-const function on a const object");
		*this = CreateRaw<T, Args2... >(pObject, pFunction, std::forward<Args2>(args)...);
	}

	template<typename T, typename... Args2>
	void BindRaw(T* pObject, ConstMemberFunction<T, Args2...> pFunction, Args2&&... args)
	{
		*this = CreateRaw<T, Args2... >(pObject, pFunction, std::forward<Args2>(args)...);
	}

	//Bind a static/global function
	template<typename... Args2>
	void BindStatic(RetVal(*pFunction)(Args..., Args2...), Args2&&... args)
	{
		*this = CreateStatic<Args2... >(pFunction, std::forward<Args2>(args)...);
	}

	//Bind a lambda
	template<typename LambdaType, typename... Args2>
	void BindLambda(LambdaType&& lambda, Args2&&... args)
	{
		*this = CreateLambda<LambdaType, Args2... >(std::forward<LambdaType>(lambda), std::forward<Args2>(args)...);
	}

	//Bind a member function with a shared_ptr object
	template<typename T, typename... Args2>
	void BindSP(std::shared_ptr<T> pObject, NonConstMemberFunction<T, Args2...> pFunction, Args2&&... args)
	{
		DELEGATE_STATIC_ASSERT(!std::is_const<T>::value, "Cannot bind a non-const function on a const object");
		*this = CreateSP<T, Args2... >(pObject, pFunction, std::forward<Args2>(args)...);
	}

	template<typename T, typename... Args2>
	void BindSP(std::shared_ptr<T> pObject, ConstMemberFunction<T, Args2...> pFunction, Args2&&... args)
	{
		*this = CreateSP<T, Args2... >(pObject, pFunction, std::forward<Args2>(args)...);
	}

	//Execute the delegate with the given parameters
	RetVal Execute(Args... args) const
	{
		DELEGATE_ASSERT(m_Allocator.HasAllocation(), "Delegate is not bound");
		return ((IDelegateT*)GetDelegate())->Execute(std::forward<Args>(args)...);
	}

	RetVal ExecuteIfBound(Args... args) const
	{
		if (IsBound())
		{
			return ((IDelegateT*)GetDelegate())->Execute(std::forward<Args>(args)...);
		}
		return RetVal();
	}

private:
	template<typename T, typename... Args3>
	void Bind(Args3&&... args)
	{
		Release();
		void* pAlloc = m_Allocator.Allocate(sizeof(T));
		new (pAlloc) T(std::forward<Args3>(args)...);
	}
};

//Delegate that can be bound to by MULTIPLE objects
template<typename... Args>
class MulticastDelegate : public DelegateBase
{
public:
	using DelegateT = Delegate<void, Args...>;

private:
	struct DelegateHandlerPair
	{
		DelegateHandle Handle;
		DelegateT Callback;
		DelegateHandlerPair() : Handle(false) {}
		DelegateHandlerPair(const DelegateHandle& handle, const DelegateT& callback) : Handle(handle), Callback(callback) {}
		DelegateHandlerPair(const DelegateHandle& handle, DelegateT&& callback) : Handle(handle), Callback(std::move(callback)) {}
	};
	template<typename T, typename... Args2>
	using ConstMemberFunction = typename _DelegatesInteral::MemberFunction<true, T, void, Args..., Args2...>::Type;
	template<typename T, typename... Args2>
	using NonConstMemberFunction = typename _DelegatesInteral::MemberFunction<false, T, void, Args..., Args2...>::Type;

public:
	//Default constructor
	constexpr MulticastDelegate()
		: m_Locks(0)
	{
	}

	//Default destructor
	~MulticastDelegate() noexcept = default;

	//Default copy constructor
	MulticastDelegate(const MulticastDelegate& other) = default;

	//Defaul copy assignment operator
	MulticastDelegate& operator=(const MulticastDelegate& other) = default;

	//Move constructor
	MulticastDelegate(MulticastDelegate&& other) noexcept
		: m_Events(std::move(other.m_Events)),
		m_Locks(std::move(other.m_Locks))
	{
	}

	//Move assignment operator
	MulticastDelegate& operator=(MulticastDelegate&& other) noexcept
	{
		m_Events = std::move(other.m_Events);
		m_Locks = std::move(other.m_Locks);
		return *this;
	}

	template<typename T>
	DelegateHandle operator+=(T&& l)
	{
		return Add(DelegateT::CreateLambda(std::move(l)));
	}

	//Add delegate with the += operator
	DelegateHandle operator+=(DelegateT&& handler) noexcept
	{
		return Add(std::forward<DelegateT>(handler));
	}

	//Remove a delegate using its DelegateHandle
	bool operator-=(DelegateHandle& handle)
	{
		return Remove(handle);
	}

	DelegateHandle Add(DelegateT&& handler) noexcept
	{
		//Favour an empty space over a possible array reallocation
		for (size_t i = 0; i < m_Events.size(); ++i)
		{
			if (m_Events[i].Handle.IsValid() == false)
			{
				m_Events[i] = DelegateHandlerPair(DelegateHandle(true), std::move(handler));
				return m_Events[i].Handle;
			}
		}
		m_Events.emplace_back(DelegateHandle(true), std::move(handler));
		return m_Events.back().Handle;
	}

	//Bind a member function
	template<typename T, typename... Args2>
	DelegateHandle AddRaw(T* pObject, NonConstMemberFunction<T, Args2...> pFunction, Args2&&... args)
	{
		return Add(DelegateT::CreateRaw(pObject, pFunction, std::forward<Args2>(args)...));
	}

	template<typename T, typename... Args2>
	DelegateHandle AddRaw(T* pObject, ConstMemberFunction<T, Args2...> pFunction, Args2&&... args)
	{
		return Add(DelegateT::CreateRaw(pObject, pFunction, std::forward<Args2>(args)...));
	}

	//Bind a static/global function
	template<typename... Args2>
	DelegateHandle AddStatic(void(*pFunction)(Args..., Args2...), Args2&&... args)
	{
		return Add(DelegateT::CreateStatic(pFunction, std::forward<Args2>(args)...));
	}

	//Bind a lambda
	template<typename LambdaType, typename... Args2>
	DelegateHandle AddLambda(LambdaType&& lambda, Args2&&... args)
	{
		return Add(DelegateT::CreateLambda(std::forward<LambdaType>(lambda), std::forward<Args2>(args)...));
	}

	//Bind a member function with a shared_ptr object
	template<typename T, typename... Args2>
	DelegateHandle AddSP(std::shared_ptr<T> pObject, NonConstMemberFunction<T, Args2...> pFunction, Args2&&... args)
	{
		return Add(DelegateT::CreateSP(pObject, pFunction, std::forward<Args2>(args)...));
	}

	template<typename T, typename... Args2>
	DelegateHandle AddSP(std::shared_ptr<T> pObject, ConstMemberFunction<T, Args2...> pFunction, Args2&&... args)
	{
		return Add(DelegateT::CreateSP(pObject, pFunction, std::forward<Args2>(args)...));
	}

	//Removes all handles that are bound from a specific object
	//Ignored when pObject is null
	//Note: Only works on Raw and SP bindings
	void RemoveObject(void* pObject)
	{
		if (pObject != nullptr)
		{
			for (size_t i = 0; i < m_Events.size(); ++i)
			{
				if (m_Events[i].Callback.GetOwner() == pObject)
				{
					if (IsLocked())
					{
						m_Events[i].Clear();
					}
					else
					{
						std::swap(m_Events[i], m_Events[m_Events.size() - 1]);
						m_Events.pop_back();
					}
				}
			}
		}
	}

	//Remove a function from the event list by the handle
	bool Remove(DelegateHandle& handle)
	{
		if (handle.IsValid())
		{
			for (size_t i = 0; i < m_Events.size(); ++i)
			{
				if (m_Events[i].Handle == handle)
				{
					if (IsLocked())
					{
						m_Events[i].Callback.Clear();
					}
					else
					{
						std::swap(m_Events[i], m_Events[m_Events.size() - 1]);
						m_Events.pop_back();
					}
					handle.Reset();
					return true;
				}
			}
		}
		return false;
	}

	bool IsBoundTo(const DelegateHandle& handle) const
	{
		if (handle.IsValid())
		{
			for (size_t i = 0; i < m_Events.size(); ++i)
			{
				if (m_Events[i].Handle == handle)
				{
					return true;
				}
			}
		}
		return false;
	}

	//Remove all the functions bound to the delegate
	void RemoveAll()
	{
		if (IsLocked())
		{
			for (DelegateHandlerPair& handler : m_Events)
			{
				handler.Callback.Clear();
			}
		}
		else
		{
			m_Events.clear();
		}
	}

	void Compress(const size_t maxSpace = 0)
	{
		if (IsLocked() == false)
		{
			size_t toDelete = 0;
			for (size_t i = 0; i < m_Events.size() - toDelete; ++i)
			{
				if (m_Events[i].Handle.IsValid() == false)
				{
					std::swap(m_Events[i], m_Events[toDelete]);
					++toDelete;
				}
			}
			if (toDelete > maxSpace)
			{
				m_Events.resize(m_Events.size() - toDelete);
			}
		}
	}

	//Execute all functions that are bound
	void Broadcast(Args ...args)
	{
		Lock();
		for (size_t i = 0; i < m_Events.size(); ++i)
		{
			if (m_Events[i].Handle.IsValid())
			{
				m_Events[i].Callback.Execute(std::forward<Args>(args)...);
			}
		}
		Unlock();
	}

	size_t GetSize() const
	{
		return m_Events.size();
	}

private:
	void Lock()
	{
		++m_Locks;
	}

	void Unlock()
	{
		//Unlock() should never be called more than Lock()!
		DELEGATE_ASSERT(m_Locks > 0);
		--m_Locks;
	}

	//Returns true is the delegate is currently broadcasting
	//If this is true, the order of the array should not be changed otherwise this causes undefined behaviour
	bool IsLocked() const
	{
		return m_Locks > 0;
	}

	std::vector<DelegateHandlerPair> m_Events;
	unsigned int m_Locks;
};

#endif
